// main/kj_store.c —— NVS 读写（命名空间 "kjrps"：游戏与昵称；"kjnet"：联机方式与联网）。
#include "kj_store.h"
#include "kj_flow.h"   // kj_conn_t

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>

static const char *TAG = "kj_store";
static const char *NS = "kjrps";
static const char *NS_NET = "kjnet";
static bool s_ready;

esp_err_t kj_store_init(void)
{
    // 不擦除：初始化失败时只是不保存（角色 / 赌局快照 / 射频校准都会退化为不缓存），
    // 绝不为了让初始化通过而清掉用户数据。
    esp_err_t err = nvs_flash_init();
    s_ready = err == ESP_OK;
    if (!s_ready) ESP_LOGE(TAG, "NVS init failed (%s); running without persistence", esp_err_to_name(err));
    return err;
}

static uint32_t get_u32(const char *ns, const char *key, uint32_t def)
{
    nvs_handle_t h;
    uint32_t v = def;
    if (!s_ready || nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return def;
    if (nvs_get_u32(h, key, &v) != ESP_OK) v = def;
    nvs_close(h);
    return v;
}

static void set_u32(const char *ns, const char *key, uint32_t v)
{
    nvs_handle_t h;
    if (!s_ready || nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return;
    uint32_t old;
    if (nvs_get_u32(h, key, &old) != ESP_OK || old != v) {
        if (nvs_set_u32(h, key, v) == ESP_OK) nvs_commit(h);
    }
    nvs_close(h);
}

static bool get_str(const char *ns, const char *key, char *out, size_t cap)
{
    nvs_handle_t h;
    out[0] = '\0';
    if (!s_ready || nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = cap;
    esp_err_t err = nvs_get_str(h, key, out, &len);
    nvs_close(h);
    if (err != ESP_OK) out[0] = '\0';
    return err == ESP_OK;
}

uint8_t kj_store_get_conn(void)
{
    return get_u32(NS_NET, "conn", KJ_CONN_DIRECT) == KJ_CONN_HUB ? KJ_CONN_HUB : KJ_CONN_DIRECT;
}

void kj_store_set_conn(uint8_t conn) { set_u32(NS_NET, "conn", conn == KJ_CONN_HUB ? KJ_CONN_HUB : KJ_CONN_DIRECT); }
uint8_t kj_store_get_role(void) { return get_u32(NS, "role", 0) ? 1 : 0; }
void kj_store_set_role(uint8_t role) { set_u32(NS, "role", role ? 1 : 0); }
uint16_t kj_store_get_room(void) { return (uint16_t)get_u32(NS, "room", 0); }
void kj_store_set_room(uint16_t room) { set_u32(NS, "room", room); }

esp_err_t kj_store_save_game(const uint8_t *blob, size_t len)
{
    nvs_handle_t h;
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, "game", blob, len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

size_t kj_store_load_game(uint8_t *blob, size_t cap)
{
    nvs_handle_t h;
    if (!s_ready || nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return 0;
    size_t len = cap;
    esp_err_t err = nvs_get_blob(h, "game", blob, &len);
    nvs_close(h);
    return err == ESP_OK ? len : 0;
}

bool kj_store_get_wifi(kj_wifi_cred_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!get_str(NS_NET, "ssid", out->ssid, sizeof(out->ssid)) || !out->ssid[0]) return false;
    get_str(NS_NET, "pass", out->pass, sizeof(out->pass));   // 开放网络没有密码
    return true;
}

esp_err_t kj_store_set_wifi(const kj_wifi_cred_t *cred)
{
    nvs_handle_t h;
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_open(NS_NET, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, "ssid", cred->ssid);
    if (err == ESP_OK) err = nvs_set_str(h, "pass", cred->pass);
    if (err == ESP_OK) err = nvs_set_u32(h, "hub", 0);   // 换了网络：旧的 hub 地址作废
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

uint32_t kj_store_get_hub_hint(void) { return get_u32(NS_NET, "hub", 0); }
void kj_store_set_hub_hint(uint32_t ip) { set_u32(NS_NET, "hub", ip); }
void kj_store_request_prov(bool on) { set_u32(NS_NET, "prov", on ? 1 : 0); }

bool kj_store_take_prov_request(void)
{
    if (!get_u32(NS_NET, "prov", 0)) return false;
    set_u32(NS_NET, "prov", 0);   // 只生效一次：配网中途断电也不会一直卡在配网页
    return true;
}

void kj_store_request_name_ap(bool then_play) { set_u32(NS_NET, "nameap", then_play ? 2 : 1); }

bool kj_store_take_name_ap_request(bool *then_play)
{
    uint32_t v = get_u32(NS_NET, "nameap", 0);
    if (!v) return false;
    set_u32(NS_NET, "nameap", 0);   // 同样只生效一次
    if (then_play) *then_play = v == 2;
    return true;
}

void kj_store_request_play(bool on) { set_u32(NS, "play", on ? 1 : 0); }

bool kj_store_take_play_request(void)
{
    if (!get_u32(NS, "play", 0)) return false;
    set_u32(NS, "play", 0);
    return true;
}

void kj_store_get_name(char out[KJ_NAME_MAX + 1], uint32_t *rev)
{
    get_str(NS, "name", out, KJ_NAME_MAX + 1);
    if (rev) *rev = get_u32(NS, "name_rev", 0);
}

void kj_store_set_name(const char *name, uint32_t rev)
{
    nvs_handle_t h;
    if (!s_ready || nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_str(h, "name", name ? name : "") == ESP_OK && nvs_set_u32(h, "name_rev", rev) == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
}
