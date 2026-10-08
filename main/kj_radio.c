// main/kj_radio.c —— 直连模式的 ESP-NOW 收发与对端表管理（平台层）。
#include "kj_radio.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"

#include <string.h>

static const char *TAG = "kj_radio";

#define PEER_CACHE 16   // < ESP_NOW_MAX_TOTAL_PEER_NUM，留出广播对端

static kj_radio_rx_cb_t s_cb;
static bool s_started;
static volatile uint32_t s_tx_fail;
static struct {
    uint8_t mac[6];
    uint32_t used;
    bool valid;
} s_peers[PEER_CACHE];
static uint32_t s_use_clock;
static const uint8_t BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!s_cb || !info || !info->src_addr || !data || len <= 0 || len > KJ_FRAME_MAX) return;
    int8_t rssi = info->rx_ctrl ? (int8_t)info->rx_ctrl->rssi : -100;
    s_cb(info->src_addr, rssi, data, len);
}

static void on_send(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    (void)tx_info;
    if (status != ESP_NOW_SEND_SUCCESS) s_tx_fail++;
}

static esp_err_t add_peer(const uint8_t mac[6])
{
    esp_now_peer_info_t peer = { 0 };
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;   // 0 = 当前信道
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    return esp_now_add_peer(&peer);
}

// 单播前确保对端在表里：满了就换掉最久没用的那个。
static esp_err_t ensure_peer(const uint8_t mac[6])
{
    int free_slot = -1, lru = 0;
    for (int i = 0; i < PEER_CACHE; i++) {
        if (s_peers[i].valid && memcmp(s_peers[i].mac, mac, 6) == 0) {
            s_peers[i].used = ++s_use_clock;
            return ESP_OK;
        }
        if (!s_peers[i].valid && free_slot < 0) free_slot = i;
        if (s_peers[i].valid && s_peers[i].used < s_peers[lru].used) lru = i;
    }
    int slot = free_slot;
    if (slot < 0) {
        slot = lru;
        esp_now_del_peer(s_peers[slot].mac);
        s_peers[slot].valid = false;
    }
    esp_err_t err = add_peer(mac);
    if (err == ESP_ERR_ESPNOW_EXIST) err = ESP_OK;
    if (err != ESP_OK) return err;
    memcpy(s_peers[slot].mac, mac, 6);
    s_peers[slot].used = ++s_use_clock;
    s_peers[slot].valid = true;
    return ESP_OK;
}

esp_err_t kj_radio_start(kj_radio_rx_cb_t cb)
{
    if (s_started) return ESP_OK;
    s_cb = cb;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    // 缓冲数量与 AMPDU 在 sdkconfig.defaults 里按小包裁剪过（两种联机方式共用）。
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = 0;   // 不保存任何 Wi-Fi 配置
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) goto fail;
    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) goto fail;
    if ((err = esp_wifi_start()) != ESP_OK) goto fail;
    esp_wifi_set_ps(WIFI_PS_NONE);   // 一直收：省电模式会把每个包的延迟拉到几百毫秒
    if ((err = esp_wifi_set_channel(KJ_RADIO_CHANNEL, WIFI_SECOND_CHAN_NONE)) != ESP_OK) goto fail;
    if ((err = esp_now_init()) != ESP_OK) goto fail;
    if ((err = esp_now_register_recv_cb(on_recv)) != ESP_OK) goto fail_now;
    if ((err = esp_now_register_send_cb(on_send)) != ESP_OK) goto fail_now;
    if ((err = add_peer(BROADCAST)) != ESP_OK) goto fail_now;
    s_started = true;
    ESP_LOGI(TAG, "ESP-NOW up on channel %d", KJ_RADIO_CHANNEL);
    return ESP_OK;

fail_now:
    esp_now_deinit();
fail:
    esp_wifi_stop();
    esp_wifi_deinit();
    ESP_LOGE(TAG, "radio start failed: %s", esp_err_to_name(err));
    return err;
}

bool kj_radio_started(void)
{
    return s_started;
}

esp_err_t kj_radio_send(const kj_out_t *it)
{
    if (!s_started) return ESP_ERR_INVALID_STATE;
    if (!it->broadcast) {
        esp_err_t err = ensure_peer(it->mac);
        if (err != ESP_OK) return err;
    }
    esp_err_t err = esp_now_send(it->broadcast ? BROADCAST : it->mac, it->data, it->len);
    if (err != ESP_OK) s_tx_fail++;   // 发送队列满等：直接丢，协议层会重发
    return err;
}

uint32_t kj_radio_tx_failures(void)
{
    return s_tx_fail;
}
