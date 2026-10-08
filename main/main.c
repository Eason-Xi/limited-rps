// main/main.c —— 限定猜拳（《赌博默示录》）多人联机固件入口。
//
// 同一份固件，开机选择角色：
//   * 选手：找到庄家开的赌局入座，用自己的设备暗中出牌；手牌页长按 OK 可以和面前的人"碰拳"直接开打，
//     也可以从名单里挑人挑战。
//   * 庄家：裁判，持有唯一可信的赌局状态；可以把全场手牌实时推给电脑看板。
// 两种联机方式（设置里切换，切换后重启；全场设备要用同一种）：
//   * 直连（默认）：设备之间用 ESP-NOW 直接通信，不需要电脑和路由器。昵称在设置里登记：设备重启后开热点，
//     手机扫码连上、在网页里填写；入座后各选手每隔几秒广播自己的昵称。看板只能用 USB 线接庄家。
//   * 电脑服务：连现场 Wi-Fi，经电脑 hub（tools/kj_hub）中继。第一次开机（或"重新配网"）进入配网模式：设备开热点，
//     手机网页里选现场 Wi-Fi；昵称扫码打开 hub 的网页登记；看板经 Wi-Fi 的 TCP（USB 串口作兜底）。
//
// 线程模型：按键回调（esp_timer 任务）、收包（kj_net 任务 / Wi-Fi 任务里的 ESP-NOW 回调）、热点网页的 HTTP /
// 事件回调都只把事件拷贝进队列；唯一的应用任务 kj_app 串行处理协议、规则、界面状态机，并在持锁时刷新 LVGL。
// 例外：热点登记昵称时 HTTP 任务要查昵称字库，它自己持 LVGL 锁（见 name_check）。
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"

#include "kj_board.h"
#include "kj_client.h"
#include "kj_flow.h"
#include "kj_fonts.h"
#include "kj_hubc.h"
#include "kj_hubproto.h"
#include "kj_model.h"
#include "kj_names.h"
#include "kj_net.h"
#include "kj_persist.h"
#include "kj_prov.h"
#include "kj_prov_form.h"
#include "kj_radio.h"
#include "kj_server.h"
#include "kj_sound.h"
#include "kj_store.h"
#include "kj_ui.h"

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "kj_app";

#define KJ_FW_VERSION       "1.2.0"
#define APP_QUEUE_DEPTH     32
#define HUB_QUEUE_DEPTH     6
#define HUB_MSG_MAX         (KH_HDR + 200)   // 最大的控制消息（NAMES）约 190 字节
#define APP_LOOP_MS         20
#define RENDER_MIN_MS       30
#define BATTERY_POLL_MS     15000
#define DIM_AFTER_MS        60000
#define DIM_PERCENT         15
#define BOARD_GAME_MS       1000
#define BOARD_HELLO_MS      5000
#define BOARD_FULL_SYNC_MS  15000
#define BOARD_LINES_PER_LOOP 6
#define PERSIST_DEBOUNCE_MS 1500
#define REG_DONE_SHOW_MS    2500
#define PROV_RESTART_MS     2500
#define RESTART_DELAY_MS    400     // 先让界面显示"正在重启…"
#define HEAP_LOG_MS         60000

typedef enum { MSG_KEY = 1, MSG_RX, MSG_NET, MSG_LINE, MSG_PROV } msg_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t key;          // MSG_KEY：按键；MSG_NET / MSG_PROV：事件
    int8_t rssi;
    uint8_t len;
    uint8_t mac[6];
    uint8_t data[KJ_FRAME_MAX];   // MSG_RX：游戏帧；MSG_LINE：命令行；MSG_NET / MSG_PROV：int 参数
} app_msg_t;

typedef struct {
    uint16_t len;
    uint32_t src_ip;
    uint8_t data[HUB_MSG_MAX];
} hub_msg_t;

typedef enum { MODE_TITLE = 0, MODE_PLAYER, MODE_HOST, MODE_SETTINGS, MODE_REGISTER, MODE_PROVISION } app_mode_t;

static QueueHandle_t s_queue, s_hubq;   // s_hubq 只在电脑服务模式创建
static app_mode_t s_mode;
static uint8_t s_conn;                  // kj_conn_t：开机时从 NVS 读出，切换后重启
static uint32_t s_restart_ms;           // 非 0：到这个时刻重启
static kj_flow_t s_flow;
static kj_client_t s_client;
static kj_server_t *s_server;           // 只有庄家才分配（约 8 KB）
static kj_hubc_t s_hubc;
static kj_ui_model_t s_model;
static uint8_t s_mac[6];
static int s_battery = -1;
static bool s_battery_ok;
static uint32_t s_battery_ms;
static uint32_t s_last_activity;
static bool s_dimmed;
static uint32_t s_last_render;
static bool s_dirty = true;
static uint32_t s_heap_ms;

// 直连
static bool s_radio_ok;
static kj_names_t *s_nametab;           // 昵称表（约 4 KB，只在直连模式分配）

// 电脑服务
static bool s_have_cred;
static char s_ssid[KJ_WIFI_SSID_MAX + 1];
static uint32_t s_hub_ip_set;
static uint16_t s_hub_tcp_set;
static uint32_t s_hint_saved;
static uint8_t s_last_net;

// 选手
static uint16_t s_auto_room;
static bool s_auto_tried;
static uint16_t s_saved_room;
static kj_room_entry_t s_rooms[KJ_CLIENT_MAX_ROOMS];
static kj_opponent_t s_opps[KJ_UI_OPP_MAX];

// 登记昵称
static app_mode_t s_after_reg;
static char s_reg_token[KJ_REG_TOKEN_LEN + 1];
static char s_reg_url[KJ_UI_TEXT_LEN];
static uint32_t s_reg_done_ms;

// 热点页（配网 / 直连模式登记昵称）
static uint8_t s_prov_kind;             // kj_prov_kind_t
static bool s_name_then_play;           // 登记昵称结束（或取消）后直接进入选手
static uint8_t s_prov_state;
static char s_prov_ssid[16], s_prov_pass[12], s_prov_qr[64], s_prov_target[33];
static uint32_t s_prov_ok_ms;

// 庄家看板与持久化
static char s_cmd_line[64];
static size_t s_cmd_len;
static uint32_t s_board_game_ms, s_board_hello_ms, s_board_sync_ms;
static uint16_t s_board_phase_ver;
static uint32_t s_saved_rev, s_rev_changed_ms, s_seen_rev;
static uint8_t s_persist_buf[KJ_PERSIST_MAX];

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void log_heap(const char *when)
{
    ESP_LOGI(TAG, "heap %s: free=%u min=%u largest=%u", when,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

// ---------------------------------------------------------------------------
// 回调（只入队）
// ---------------------------------------------------------------------------

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    int key = -1;
    if (btn == BSP_BTN_UP && ev == BSP_BTN_PRESS) key = KJ_KEY_UP;
    if (btn == BSP_BTN_DOWN && ev == BSP_BTN_PRESS) key = KJ_KEY_DOWN;
    if (btn == BSP_BTN_OK && (ev == BSP_BTN_CLICK || ev == BSP_BTN_DOUBLE)) key = KJ_KEY_OK;
    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) key = KJ_KEY_OK_LONG;
    if (key < 0 || !s_queue) return;
    app_msg_t m = { .kind = MSG_KEY, .key = (uint8_t)key };
    (void)xQueueSend(s_queue, &m, 0);
}

static void on_net_frame(const uint8_t src[6], int8_t rssi, const uint8_t *data, int len)
{
    if (!s_queue || len <= 0 || len > KJ_FRAME_MAX) return;
    app_msg_t m = { .kind = MSG_RX, .rssi = rssi, .len = (uint8_t)len };
    memcpy(m.mac, src, 6);
    memcpy(m.data, data, (size_t)len);
    (void)xQueueSend(s_queue, &m, 0);   // 满了丢弃：协议层会重发
}

static void on_net_ctl(const uint8_t *dgram, int len, uint32_t src_ip)
{
    if (!s_hubq || len <= 0 || len > HUB_MSG_MAX) return;
    hub_msg_t m = { .len = (uint16_t)len, .src_ip = src_ip };
    memcpy(m.data, dgram, (size_t)len);
    (void)xQueueSend(s_hubq, &m, 0);
}

static void on_net_event(uint8_t ev, int arg)
{
    if (!s_queue) return;
    app_msg_t m = { .kind = MSG_NET, .key = ev };
    memcpy(m.data, &arg, sizeof(arg));
    (void)xQueueSend(s_queue, &m, 0);
}

static void on_board_line(const char *line, int len)
{
    if (!s_queue || len <= 0 || len >= KJ_FRAME_MAX) return;
    app_msg_t m = { .kind = MSG_LINE, .len = (uint8_t)len };
    memcpy(m.data, line, (size_t)len);
    m.data[len] = '\0';
    (void)xQueueSend(s_queue, &m, 0);
}

static void on_prov_event(uint8_t ev, int arg)
{
    if (!s_queue) return;
    app_msg_t m = { .kind = MSG_PROV, .key = ev };
    memcpy(m.data, &arg, sizeof(arg));
    (void)xQueueSend(s_queue, &m, 0);
}

// ---------------------------------------------------------------------------
// 发送、提示音、背光、联网状态
// ---------------------------------------------------------------------------

static void flush_outbox(kj_outbox_t *o)
{
    for (int i = 0; i < o->count; i++) {
        if (s_conn == KJ_CONN_DIRECT) {
            kj_radio_send(&o->items[i]);
        } else {
            kj_net_send_frame(&o->items[i]);
        }
    }
    kj_outbox_clear(o);
}

static void cue(kj_cue_t c)
{
    if (c == KJ_CUE_NONE) return;
    kj_sound_play(c);
    if (c != KJ_CUE_KEY) s_last_activity = now_ms();   // 有事发生时点亮屏幕
}

static void wake_screen(void)
{
    s_last_activity = now_ms();
    if (s_dimmed) {
        s_dimmed = false;
        bsp_display_backlight(100);
    }
}

static void update_backlight(uint32_t now)
{
    if (s_mode == MODE_REGISTER || s_mode == MODE_PROVISION) s_last_activity = now;   // 二维码页保持常亮
    if (!s_dimmed && (uint32_t)(now - s_last_activity) >= DIM_AFTER_MS) {
        s_dimmed = true;
        bsp_display_backlight(DIM_PERCENT);
    } else if (s_dimmed && (uint32_t)(now - s_last_activity) < DIM_AFTER_MS) {
        s_dimmed = false;
        bsp_display_backlight(100);
    }
}

static void restart_soon(uint32_t now)
{
    kj_flow_toast(&s_flow, KJ_TOAST_RESTARTING, now);
    s_restart_ms = now + RESTART_DELAY_MS;
    if (s_restart_ms == 0) s_restart_ms = 1;
    s_dirty = true;
}

static uint8_t net_status(uint32_t now)
{
    if (s_conn == KJ_CONN_DIRECT) return s_radio_ok ? KJ_NET_DIRECT : KJ_NET_NO_RADIO;
    if (!s_have_cred) return KJ_NET_NO_WIFI;
    if (!kj_net_has_ip()) return KJ_NET_CONNECTING;
    if (kj_hubc_state(&s_hubc, now) != KJ_HUB_OK) return KJ_NET_SEARCHING;
    return s_hubc.incompatible ? KJ_NET_OLD : KJ_NET_OK;
}

static void model_env(kj_model_env_t *env, uint32_t now)
{
    env->conn = s_conn;
    env->channel = KJ_RADIO_CHANNEL;
    env->net = net_status(now);
    env->ip = kj_net_ip();
    env->hub_ip = s_hubc.hub_ip;
    env->ssid = s_ssid;
    env->my_name = s_hubc.my_name;
    env->fw = KJ_FW_VERSION;
    env->dev_id = (uint16_t)((s_mac[4] << 8) | s_mac[5]);
}

static bool name_lookup(void *ctx, uint16_t room, uint8_t no, const char **name)
{
    (void)ctx;
    if (s_conn == KJ_CONN_DIRECT) {
        if (!s_nametab) return false;
        const uint8_t *mac = NULL;
        if (s_mode == MODE_HOST && s_server) {   // 庄家：只认座位登记的那台设备自己报的昵称
            const kj_player_t *p = kj_rules_player_by_no(&s_server->game, no);
            if (!p || p->is_bot) return false;
            mac = p->mac;
        }
        return kj_names_get(s_nametab, room, no, mac, name);
    }
    uint8_t flags;
    return kj_hubc_name(&s_hubc, room, no, now_ms(), name, &flags);
}

static const kj_names_if_t s_names = { .lookup = name_lookup, .ctx = NULL };

// ---------------------------------------------------------------------------
// 电脑 hub：发现、保活、昵称、登记
// ---------------------------------------------------------------------------

static void hub_rx(uint32_t now)
{
    if (!s_hubq) return;
    hub_msg_t hm;
    while (xQueueReceive(s_hubq, &hm, 0) == pdTRUE) {
        kh_env_t e;
        if (!kh_env_decode(hm.data, hm.len, &e)) continue;
        kj_hubc_on_msg(&s_hubc, &e, hm.src_ip, now);
        kj_hubc_note_rx(&s_hubc, now);
        s_dirty = true;
    }
}

static void hub_tick(uint32_t now)
{
    if (s_conn != KJ_CONN_HUB || !kj_net_started()) return;
    kj_hubc_out_t out[KJ_HUBC_OUT_MAX];
    int n = kj_hubc_tick(&s_hubc, now, out, KJ_HUBC_OUT_MAX);
    for (int i = 0; i < n; i++) kj_net_send_ctl(out[i].kind, out[i].ip, out[i].room, out[i].data, out[i].len);
    if (kj_hubc_state(&s_hubc, now) == KJ_HUB_OK &&
        (s_hubc.hub_ip != s_hub_ip_set || s_hubc.tcp_port != s_hub_tcp_set)) {
        s_hub_ip_set = s_hubc.hub_ip;
        s_hub_tcp_set = s_hubc.tcp_port;
        kj_net_set_hub(s_hub_ip_set, s_hub_tcp_set);
        if (s_hub_ip_set != s_hint_saved) {   // 下次开机先试这个地址
            s_hint_saved = s_hub_ip_set;
            kj_store_set_hub_hint(s_hint_saved);
        }
        log_heap("hub found");
    }
    if (kj_hubc_take_my_name_changed(&s_hubc)) {
        kj_store_set_name(s_hubc.my_name, s_hubc.my_rev);
        if (s_mode != MODE_REGISTER) kj_flow_toast(&s_flow, KJ_TOAST_NAME_UPDATED, now);
        s_dirty = true;
    }
    uint8_t net = net_status(now);
    if (net != s_last_net) {
        if (net == KJ_NET_OLD) kj_flow_toast(&s_flow, KJ_TOAST_OLD_FW, now);
        s_last_net = net;
        s_dirty = true;
    }
}

// ---------------------------------------------------------------------------
// 直连：选手广播的昵称
// ---------------------------------------------------------------------------

// 庄家只收座位登记的那台设备自己报的昵称；选手收同一赌局里所有人的。
static void names_rx(const app_msg_t *m)
{
    kj_frame_t f;
    if (!s_nametab || !kj_proto_decode(m->data, m->len, &f) || f.type != KJ_F_NAME) return;
    if (s_mode == MODE_HOST && s_server) {
        const kj_player_t *p = kj_rules_player_by_no(&s_server->game, f.u.name.no);
        if (f.room != s_server->room || !p || p->is_bot || memcmp(p->mac, m->mac, 6) != 0) return;
        if (kj_names_store(s_nametab, m->mac, f.room, &f.u.name)) {
            kj_rules_mark_dirty(&s_server->game, kj_idx_of(f.u.name.no));   // 看板行带上新昵称
            s_dirty = true;
        }
    } else if (s_mode == MODE_PLAYER) {
        if (kj_names_store(s_nametab, m->mac, f.room, &f.u.name)) s_dirty = true;
    }
}

// ---------------------------------------------------------------------------
// 选手
// ---------------------------------------------------------------------------

static void enter_player(void)
{
    kj_client_init(&s_client, esp_random());
    s_auto_room = kj_store_get_room();
    s_saved_room = s_auto_room;
    s_auto_tried = false;
    s_mode = MODE_PLAYER;
    if (s_conn == KJ_CONN_HUB) {
        kj_net_set_host(false);
        kj_hubc_set_role(&s_hubc, KH_ROLE_PLAYER, 0);
    }
    ESP_LOGI(TAG, "role: player (last room %04X)", s_auto_room);
}

static void player_ctx(kj_player_ctx_t *ctx, uint32_t now)
{
    ctx->client = &s_client;
    ctx->rooms = s_rooms;
    ctx->room_count = kj_client_rooms(&s_client, now, s_rooms, KJ_CLIENT_MAX_ROOMS);
    ctx->opps = s_opps;
    ctx->opp_count = kj_client_opponents(&s_client, s_opps, KJ_UI_OPP_MAX);
    ctx->connected = kj_client_connected(&s_client, now);
    ctx->now_ms = now;
}

static void player_key(kj_key_t key, uint32_t now)
{
    kj_player_ctx_t ctx;
    player_ctx(&ctx, now);
    // 先让状态机消化同一批里刚到的视图（亮牌 / 通知的提示音不能丢），再处理按键。
    kj_cue_t pending = KJ_CUE_NONE;
    kj_flow_player_update(&s_flow, &ctx, &pending);
    cue(pending);
    kj_action_t a = kj_flow_player_key(&s_flow, &ctx, key);
    kj_cue_t c = pending == KJ_CUE_NONE ? KJ_CUE_KEY : KJ_CUE_NONE;
    switch (a.kind) {
    case KJ_ACT_JOIN:
        s_auto_tried = true;
        kj_client_join(&s_client, a.room, now);
        break;
    case KJ_ACT_LEAVE:
        kj_client_leave(&s_client);
        kj_store_set_room(0);
        s_saved_room = 0;
        s_auto_tried = true;
        break;
    case KJ_ACT_TO_TITLE:
        kj_client_leave(&s_client);
        if (s_conn == KJ_CONN_HUB) kj_hubc_set_role(&s_hubc, KH_ROLE_NONE, 0);
        s_mode = MODE_TITLE;
        break;
    case KJ_ACT_REQUEST:
        if (!kj_client_request(&s_client, a.op, a.arg, now)) {
            c = KJ_CUE_ERROR;
            if (a.op == KJ_OP_BUMP) kj_flow_bump_rejected(&s_flow);
        } else if (a.op == KJ_OP_PLAY) {
            c = KJ_CUE_LOCK;
        } else if (a.op == KJ_OP_BUMP) {
            c = KJ_CUE_BUMP;
        }
        break;
    default:
        break;
    }
    cue(c);
}

static void player_tick(uint32_t now)
{
    kj_outbox_t out;
    kj_outbox_clear(&out);
    kj_client_tick(&s_client, now, &out);
    if (s_conn == KJ_CONN_DIRECT && s_nametab) {   // 入座后每隔几秒广播自己的昵称
        bool seated = s_client.link == KJ_LINK_JOINED;
        kj_names_tick(s_nametab, seated ? s_client.room : 0, seated ? s_client.view.no : 0, s_mac, s_hubc.my_name,
                      now, &out);
    }
    flush_outbox(&out);

    uint8_t why = 0;
    if (kj_client_take_kicked(&s_client, &why)) {
        kj_flow_toast(&s_flow, why == KJ_N_FULL ? KJ_TOAST_FULL : KJ_TOAST_KICKED, now);
        cue(KJ_CUE_NOTICE);
        s_dirty = true;
    }
    if (kj_client_take_req_failed(&s_client)) {
        kj_flow_toast(&s_flow, KJ_TOAST_NO_REPLY, now);
        cue(KJ_CUE_ERROR);
        s_dirty = true;
    }
    if (kj_client_take_view_changed(&s_client)) s_dirty = true;

    kj_player_ctx_t ctx;
    player_ctx(&ctx, now);
    // 开机后自动回到上次的赌局（主动离座后不再自动入座）
    if (!s_auto_tried && s_auto_room && s_client.link == KJ_LINK_IDLE) {
        for (int i = 0; i < ctx.room_count; i++) {
            if (s_rooms[i].room == s_auto_room) {
                s_auto_tried = true;
                kj_client_join(&s_client, s_auto_room, now);
                break;
            }
        }
    }
    if (s_client.link == KJ_LINK_JOINED && s_client.room != s_saved_room) {
        s_saved_room = s_client.room;
        kj_store_set_room(s_saved_room);
    }
    if (s_conn == KJ_CONN_HUB) {
        kj_hubc_set_role(&s_hubc, KH_ROLE_PLAYER, s_client.link == KJ_LINK_JOINED ? s_client.room : 0);
    }
    kj_cue_t c = KJ_CUE_NONE;
    kj_page_t page = kj_flow_player_update(&s_flow, &ctx, &c);
    cue(c);
    kj_model_env_t env;
    model_env(&env, now);
    kj_model_player(&s_model, &s_flow, &ctx, page, &env, &s_names, s_battery);
}

// ---------------------------------------------------------------------------
// 庄家
// ---------------------------------------------------------------------------

static void board_write(const char *line, size_t n)
{
    if (!n) return;
    fwrite(line, 1, n, stdout);
    clearerr(stdout);   // 电脑未连接时写入会失败；清掉错误标志，连上后继续输出
    kj_net_board_write(line, n);
}

static void board_resync(void)
{
    s_board_hello_ms = 0;   // 立刻补发身份行与汇总，并全量输出选手行
    s_board_game_ms = 0;
    if (s_server) kj_rules_mark_all_dirty(&s_server->game);
}

static void enter_host(void)
{
    s_server = calloc(1, sizeof(kj_server_t));
    if (!s_server) {
        ESP_LOGE(TAG, "no memory for host state");
        kj_flow_toast(&s_flow, KJ_TOAST_RADIO_FAIL, now_ms());
        return;
    }
    uint16_t room = (uint16_t)((s_mac[4] << 8) | s_mac[5]);
    if (room == 0) room = 1;
    kj_server_init(s_server, room, esp_random());
    size_t n = kj_store_load_game(s_persist_buf, sizeof(s_persist_buf));
    if (n && kj_persist_load(&s_server->game, s_persist_buf, n, now_ms())) {
        kj_flow_toast(&s_flow, KJ_TOAST_RESTORED, now_ms());
        ESP_LOGI(TAG, "restored game: %d players, phase %d", kj_rules_player_count(&s_server->game),
                 s_server->game.phase);
    }
    s_saved_rev = s_seen_rev = s_server->game.rev;

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG || CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG
    // USB 串口看板作兜底：安装驱动后日志改走驱动缓冲，并能非阻塞地读取看板命令。
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 512;
    cfg.tx_buffer_size = 2048;
    if (usb_serial_jtag_driver_install(&cfg) == ESP_OK) {
        usb_serial_jtag_vfs_use_driver();
    } else {
        ESP_LOGW(TAG, "usb serial driver install failed; usb board commands unavailable");
    }
#endif
    setvbuf(stdout, NULL, _IOLBF, 0);
    s_mode = MODE_HOST;
    if (s_conn == KJ_CONN_HUB) {
        kj_net_set_host(true);
        kj_net_board_enable(true);
        kj_hubc_set_role(&s_hubc, KH_ROLE_HOST, room);
    } else if (s_nametab) {
        kj_names_set_room(s_nametab, room);
    }
    board_resync();
    ESP_LOGI(TAG, "role: host, room %04X", room);
    log_heap("host ready");
}

static void host_command(kj_cmd_t cmd, int arg, bool from_board, uint32_t now)
{
    kj_notice_t r = kj_server_command(s_server, cmd, arg, now);
    if (from_board) {
        char line[KJ_BOARD_LINE_MAX];
        board_write(line, kj_board_ack_line(cmd, arg, r, line, sizeof(line)));
    }
    if (cmd == KJ_CMD_SYNC) board_resync();
    if (r != KJ_N_NONE && !from_board) {
        kj_flow_toast(&s_flow, cmd == KJ_CMD_START ? KJ_TOAST_NEED_TWO : KJ_TOAST_INVALID, now);
        cue(KJ_CUE_ERROR);
    }
    s_dirty = true;
}

static void host_board_line(const char *line, uint32_t now)
{
    kj_cmd_t cmd;
    int arg;
    if (s_server && kj_board_parse_command(line, &cmd, &arg)) host_command(cmd, arg, true, now);
}

static void host_poll_serial(uint32_t now)
{
    if (!usb_serial_jtag_is_driver_installed()) return;
    uint8_t buf[64];
    int n;
    while ((n = usb_serial_jtag_read_bytes(buf, sizeof(buf), 0)) > 0) {
        for (int i = 0; i < n; i++) {
            char ch = (char)buf[i];
            if (ch == '\n' || ch == '\r') {
                if (s_cmd_len) {
                    s_cmd_line[s_cmd_len] = '\0';
                    host_board_line(s_cmd_line, now);
                }
                s_cmd_len = 0;
            } else if (s_cmd_len < sizeof(s_cmd_line) - 1) {
                s_cmd_line[s_cmd_len++] = ch;
            } else {
                s_cmd_len = 0;   // 超长行丢弃
            }
        }
    }
}

static void host_board(uint32_t now)
{
    kj_game_t *g = &s_server->game;
    char line[KJ_BOARD_LINE_MAX];
    if (kj_net_board_take_overflow()) board_resync();   // TCP 缓冲放不下丢过行：补发全量
    if (s_board_hello_ms == 0 || (uint32_t)(now - s_board_hello_ms) >= BOARD_HELLO_MS) {
        s_board_hello_ms = now ? now : 1;
        const char *link = s_conn == KJ_CONN_DIRECT ? "espnow" : "wifi";
        board_write(line, kj_board_hello_line(s_server->room, KJ_FW_VERSION, s_mac, link, line, sizeof(line)));
    }
    if ((uint32_t)(now - s_board_sync_ms) >= BOARD_FULL_SYNC_MS) {
        s_board_sync_ms = now;
        kj_rules_mark_all_dirty(g);   // 新打开的看板最多等 15 s 就能拿到全量
    }
    // 汇总行在前：电脑服务据此知道局号，再记录后面的选手行与事件
    if (s_board_game_ms == 0 || (uint32_t)(now - s_board_game_ms) >= BOARD_GAME_MS ||
        g->phase_ver != s_board_phase_ver) {
        s_board_game_ms = now ? now : 1;
        s_board_phase_ver = g->phase_ver;
        board_write(line, kj_board_game_line(g, s_server->room, now, line, sizeof(line)));
    }
    kj_event_t e;
    while (kj_rules_pop_event(g, &e)) board_write(line, kj_board_event_line(&e, line, sizeof(line)));
    int lines = 0;
    for (int i = 0; i < KJ_MAX_PLAYERS && lines < BOARD_LINES_PER_LOOP; i++) {
        if (!kj_rules_take_dirty(g, i)) continue;
        // 从没用过的空座位不输出（只有被移除的座位需要发 gone）
        if (!g->players[i].used && g->players[i].view_ver == 0) continue;
        // 直连模式没有 hub 的登记表：看板行带上选手设备广播的昵称（还没收到就不带）
        const char *name = NULL;
        if (s_conn == KJ_CONN_DIRECT && s_nametab && g->players[i].used && !g->players[i].is_bot) {
            kj_names_get(s_nametab, s_server->room, kj_no_of(i), g->players[i].mac, &name);
        }
        board_write(line, kj_board_player_line(g, i, now, name, line, sizeof(line)));
        lines++;
    }
}

static void host_persist(uint32_t now)
{
    kj_game_t *g = &s_server->game;
    if (g->rev != s_seen_rev) {
        s_seen_rev = g->rev;
        s_rev_changed_ms = now;
        s_dirty = true;
    }
    if (g->rev == s_saved_rev || (uint32_t)(now - s_rev_changed_ms) < PERSIST_DEBOUNCE_MS) return;
    size_t n = kj_persist_save(g, s_persist_buf, sizeof(s_persist_buf));
    if (n && kj_store_save_game(s_persist_buf, n) == ESP_OK) {
        s_saved_rev = g->rev;
    } else {
        s_saved_rev = g->rev;   // 写失败也不反复重试刷 Flash；下次变化再写
        ESP_LOGW(TAG, "game snapshot save failed");
    }
}

static void host_key(kj_key_t key, uint32_t now)
{
    kj_flow_host_sync(&s_flow, &s_server->game);
    kj_action_t a = kj_flow_host_key(&s_flow, &s_server->game, key, now);
    if (a.kind == KJ_ACT_HOST_CMD) {
        host_command(a.cmd, 0, false, now);
        cue(KJ_CUE_DUEL);
    } else {
        cue(a.arg ? KJ_CUE_ERROR : KJ_CUE_KEY);
    }
}

static uint8_t board_link(void)
{
    if (s_conn == KJ_CONN_HUB && kj_net_board_connected()) return KJ_BOARD_WIFI;
    if (usb_serial_jtag_is_driver_installed() && usb_serial_jtag_is_connected()) return KJ_BOARD_USB;
    return KJ_BOARD_NONE;
}

static void host_tick(uint32_t now)
{
    host_poll_serial(now);
    kj_outbox_t out;
    kj_outbox_clear(&out);
    kj_server_tick(s_server, now, &out);
    flush_outbox(&out);
    host_board(now);
    host_persist(now);
    kj_flow_host_sync(&s_flow, &s_server->game);
    kj_model_env_t env;
    model_env(&env, now);
    kj_model_host(&s_model, &s_flow, s_server, now, s_battery, board_link(), &env, &s_names);
}

static void host_rx(const app_msg_t *m, uint32_t now)
{
    kj_outbox_t out;
    kj_outbox_clear(&out);
    kj_server_on_frame(s_server, m->mac, m->rssi, m->data, m->len, now, &out);
    flush_outbox(&out);
}

// ---------------------------------------------------------------------------
// 登记昵称
// ---------------------------------------------------------------------------

static void new_reg_token(uint32_t now)
{
    kj_reg_token(esp_random(), esp_random(), s_reg_token);
    kj_hubc_reg_start(&s_hubc, s_reg_token, now);
    s_reg_done_ms = 0;
}

static void enter_register(app_mode_t after, uint32_t now)
{
    s_after_reg = after;
    s_mode = MODE_REGISTER;
    if (s_conn == KJ_CONN_HUB) new_reg_token(now);   // 直连模式没有电脑服务：页面只做说明，OK 后重启进热点登记
}

static void leave_register(void)
{
    if (s_conn == KJ_CONN_HUB) kj_hubc_reg_stop(&s_hubc);
    if (s_after_reg == MODE_PLAYER) {
        enter_player();
    } else {
        s_mode = s_after_reg;
    }
}

static void register_tick(uint32_t now)
{
    if (s_conn == KJ_CONN_DIRECT) {
        kj_model_env_t env;
        model_env(&env, now);
        kj_model_register(&s_model, &s_flow, &env, KH_REG_INVALID, NULL, s_battery, now);
        return;
    }
    bool hub = kj_hubc_state(&s_hubc, now) == KJ_HUB_OK;
    uint8_t state = hub ? s_hubc.reg_state : KH_REG_INVALID;
    if (hub && s_hubc.reg_state == KH_REG_INVALID) new_reg_token(now);   // 过期：自动换一个
    if (state == KH_REG_DONE && s_reg_done_ms == 0) {
        s_reg_done_ms = now ? now : 1;
        cue(KJ_CUE_CLEARED);
    }
    if (s_reg_done_ms && (uint32_t)(now - s_reg_done_ms) >= REG_DONE_SHOW_MS) {
        leave_register();
        return;
    }
    s_reg_url[0] = '\0';
    if (hub) {
        char ip[16];
        kj_ip_text(s_hubc.hub_ip, ip);
        snprintf(s_reg_url, sizeof(s_reg_url), "http://%s:%u/j/%s", ip, (unsigned)s_hubc.http_port, s_reg_token);
    }
    kj_model_env_t env;
    model_env(&env, now);
    kj_model_register(&s_model, &s_flow, &env, state, s_reg_url, s_battery, now);
}

// ---------------------------------------------------------------------------
// 热点页：配网（电脑服务模式）/ 登记昵称（直连模式）
// ---------------------------------------------------------------------------

static void start_audio(void)
{
    static bool started;
    if (started) return;
    started = true;
    if (bsp_audio_init() != ESP_OK) {
        ESP_LOGW(TAG, "audio init failed; running silent");
        return;
    }
    if (kj_sound_init() != ESP_OK) ESP_LOGW(TAG, "sound unavailable");
}

static bool name_glyph(void *ctx, uint32_t cp)
{
    (void)ctx;
    return kj_fonts_name_has(cp);
}

// 登记昵称：校验手机提交的表单。运行在 HTTP 任务里；查字库会写字体的查找缓存，所以持 LVGL 锁。
static bool name_check(const char *body, size_t n, char name[KJ_NAME_MAX + 1], const char **err, char *bad,
                       size_t bad_cap)
{
    if (!bsp_lvgl_lock(1000)) {
        if (err) *err = "busy";
        return false;
    }
    bool ok = kj_name_form_parse(body, n, name_glyph, NULL, name, err, bad, bad_cap);
    bsp_lvgl_unlock();
    return ok;
}

static void start_hotspot(uint8_t kind)
{
    s_mode = MODE_PROVISION;
    s_prov_kind = kind;
    s_prov_state = KJ_PROV_WAIT_PHONE;
    log_heap("before hotspot");
    esp_err_t err = kind == KJ_PROV_KIND_NAME
                        ? kj_prov_start(KJ_PROV_MODE_NAME, on_prov_event, s_hubc.my_name, name_check)
                        : kj_prov_start(KJ_PROV_MODE_WIFI, on_prov_event, NULL, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "hotspot start failed: %s", esp_err_to_name(err));
        kj_flow_toast(&s_flow, KJ_TOAST_RADIO_FAIL, now_ms());
    }
    kj_prov_get_ap(s_prov_ssid, s_prov_pass, s_prov_qr);
    log_heap("hotspot started");
}

static void prov_event(uint8_t ev, uint32_t now)
{
    switch (ev) {
    case KJ_PROV_EV_PHONE_IN:
        if (s_prov_state == KJ_PROV_WAIT_PHONE) s_prov_state = KJ_PROV_PHONE_IN;
        break;
    case KJ_PROV_EV_PHONE_OUT:
        if (s_prov_state == KJ_PROV_PHONE_IN) s_prov_state = KJ_PROV_WAIT_PHONE;
        break;
    case KJ_PROV_EV_TRYING:
        kj_prov_get_target(s_prov_target);
        s_prov_state = KJ_PROV_TRYING;
        break;
    case KJ_PROV_EV_OK: {
        kj_wifi_cred_t cred;
        uint32_t hub = 0;
        if (kj_prov_take_result(&cred, &hub) && kj_store_set_wifi(&cred) == ESP_OK) {
            if (hub) kj_store_set_hub_hint(hub);
            s_prov_state = KJ_PROV_OK;
            s_prov_ok_ms = now ? now : 1;
        } else {
            s_prov_state = KJ_PROV_FAILED;
        }
        memset(&cred, 0, sizeof(cred));
        break;
    }
    case KJ_PROV_EV_FAILED: s_prov_state = KJ_PROV_FAILED; break;
    case KJ_PROV_EV_NAME_OK: {
        char name[KJ_NAME_MAX + 1], old[KJ_NAME_MAX + 1];
        uint32_t rev = 0;
        if (kj_prov_take_name(name)) {
            kj_store_get_name(old, &rev);
            kj_store_set_name(name, rev + 1);
            snprintf(s_hubc.my_name, sizeof(s_hubc.my_name), "%s", name);
            s_prov_state = KJ_PROV_OK;
            s_prov_ok_ms = now ? now : 1;
            ESP_LOGI(TAG, "nickname saved (%u bytes)", (unsigned)strlen(name));
        }
        break;
    }
    default: break;
    }
    s_dirty = true;
}

static void provision_tick(uint32_t now)
{
    bool name = s_prov_kind == KJ_PROV_KIND_NAME;
    if (s_prov_ok_ms && (uint32_t)(now - s_prov_ok_ms) >= (name ? REG_DONE_SHOW_MS : PROV_RESTART_MS)) {
        ESP_LOGI(TAG, "%s saved; restarting", name ? "nickname" : "wifi");
        if (name && s_name_then_play) kj_store_request_play(true);
        esp_restart();   // 重启进入游戏模式（热点与游戏的内存峰值隔开）
    }
    kj_model_env_t env;
    model_env(&env, now);
    kj_model_provision(&s_model, &s_flow, &env, s_prov_kind, s_prov_state, s_prov_qr, s_prov_ssid,
                       s_prov_state == KJ_PROV_TRYING ? s_prov_target : s_prov_pass, s_battery, now);
}

static void provision_skip(void)
{
    if (s_prov_kind == KJ_PROV_KIND_NAME) {   // 取消登记：直接重启回游戏
        if (s_name_then_play) kj_store_request_play(true);
        esp_restart();
    }
    if (s_have_cred) esp_restart();   // 还有原来的凭据：直接按原来的 Wi-Fi 重启
    kj_prov_stop();                   // 没有凭据：留在首页（界面会提示未配置 Wi-Fi）
    start_audio();
    s_mode = MODE_TITLE;
}

// ---------------------------------------------------------------------------
// 应用任务
// ---------------------------------------------------------------------------

static void choose_role(uint8_t role, uint32_t now)
{
    if (s_conn == KJ_CONN_HUB && !s_have_cred) {   // 电脑服务模式没有 Wi-Fi 就玩不了：先去设置里配网
        kj_flow_toast(&s_flow, KJ_TOAST_NO_WIFI, now);
        kj_flow_settings_open(&s_flow, KJ_SET_WIFI);
        s_mode = MODE_SETTINGS;
        cue(KJ_CUE_ERROR);
        return;
    }
    if (s_conn == KJ_CONN_DIRECT && !s_radio_ok) {
        kj_flow_toast(&s_flow, KJ_TOAST_RADIO_FAIL, now);
        cue(KJ_CUE_ERROR);
        return;
    }
    kj_store_set_role(role);
    if (role) {
        enter_host();
    } else if (!s_hubc.my_name[0]) {
        enter_register(MODE_PLAYER, now);   // 选手还没有昵称：先扫码登记（长按可跳过）
    } else {
        enter_player();
    }
}

static void handle_key(kj_key_t key, uint32_t now)
{
    if (s_restart_ms) return;   // 马上重启：不再响应按键
    if (s_dimmed) {   // 熄屏时第一下按键只负责点亮
        wake_screen();
        s_dirty = true;
        return;
    }
    wake_screen();
    s_dirty = true;
    kj_action_t a;
    switch (s_mode) {
    case MODE_TITLE:
        a = kj_flow_title_key(&s_flow, key);
        cue(KJ_CUE_KEY);
        if (a.kind == KJ_ACT_ROLE) choose_role(a.arg, now);
        if (a.kind == KJ_ACT_SETTINGS) s_mode = MODE_SETTINGS;
        break;
    case MODE_SETTINGS:
        a = kj_flow_settings_key(&s_flow, key);
        cue(KJ_CUE_KEY);
        if (a.kind == KJ_ACT_BACK) s_mode = MODE_TITLE;
        if (a.kind == KJ_ACT_SET_ITEM && a.arg == KJ_SET_NAME) {
            if (s_conn == KJ_CONN_HUB && !s_have_cred) {
                kj_flow_toast(&s_flow, KJ_TOAST_NO_WIFI, now);
            } else {
                enter_register(MODE_SETTINGS, now);
            }
        }
        if (a.kind == KJ_ACT_SET_ITEM && a.arg == KJ_SET_WIFI) {
            kj_store_request_prov(true);
            restart_soon(now);
        }
        if (a.kind == KJ_ACT_SET_CONN) {   // 换联机方式：保存后重启（电脑服务模式还没配网的话会进入配网）
            kj_store_set_conn(a.arg);
            ESP_LOGI(TAG, "connection mode -> %s", a.arg == KJ_CONN_HUB ? "hub" : "direct");
            restart_soon(now);
        }
        break;
    case MODE_REGISTER:
        a = kj_flow_register_key(&s_flow, key);
        cue(KJ_CUE_KEY);
        if (a.kind == KJ_ACT_REG_REFRESH) new_reg_token(now);
        if (a.kind == KJ_ACT_REG_START) {   // 直连：重启进入热点登记；从"我是选手"来的，登记完直接进入选手
            kj_store_request_name_ap(s_after_reg == MODE_PLAYER);
            restart_soon(now);
        }
        if (a.kind == KJ_ACT_BACK) leave_register();
        break;
    case MODE_PROVISION:
        a = kj_flow_provision_key(&s_flow, key);
        if (a.kind == KJ_ACT_PROV_SKIP) provision_skip();
        break;
    case MODE_PLAYER: player_key(key, now); break;
    case MODE_HOST: if (s_server) host_key(key, now); break;
    }
}

static void handle_msg(const app_msg_t *msg, uint32_t now)
{
    int arg = 0;
    switch (msg->kind) {
    case MSG_KEY:
        handle_key((kj_key_t)msg->key, now);
        break;
    case MSG_RX:
        if (s_conn == KJ_CONN_DIRECT) {
            if (kj_proto_peek_type(msg->data, msg->len) == KJ_F_NAME) {
                names_rx(msg);
                break;
            }
        } else {
            kj_hubc_note_rx(&s_hubc, now);   // 中继来的帧也说明 hub 还在
        }
        if (s_mode == MODE_PLAYER) {
            kj_client_on_frame(&s_client, msg->mac, msg->data, msg->len, now);
        } else if (s_mode == MODE_HOST && s_server) {
            host_rx(msg, now);
        }
        break;
    case MSG_NET:
        memcpy(&arg, msg->data, sizeof(arg));
        if (msg->key == KJ_NET_EV_GOT_IP) log_heap("got ip");
        if (msg->key == KJ_NET_EV_DISCONNECTED) ESP_LOGW(TAG, "wifi disconnected (reason %d)", arg);
        if (msg->key == KJ_NET_EV_BOARD_UP) {
            ESP_LOGI(TAG, "board link up");
            board_resync();
        }
        s_dirty = true;
        break;
    case MSG_LINE:
        if (s_mode == MODE_HOST) host_board_line((const char *)msg->data, now);
        break;
    case MSG_PROV:
        prov_event(msg->key, now);
        break;
    default:
        break;
    }
}

static void app_task(void *arg)
{
    (void)arg;
    s_last_activity = now_ms();
    for (;;) {
        app_msg_t msg;
        TickType_t wait = pdMS_TO_TICKS(APP_LOOP_MS);
        while (xQueueReceive(s_queue, &msg, wait) == pdTRUE) {
            wait = 0;
            handle_msg(&msg, now_ms());
        }
        uint32_t now = now_ms();
        if (s_restart_ms && (int32_t)(now - s_restart_ms) >= 0) {
            ESP_LOGI(TAG, "restarting");
            esp_restart();
        }
        hub_rx(now);
        hub_tick(now);
        if (s_battery_ok && (s_battery_ms == 0 || (uint32_t)(now - s_battery_ms) >= BATTERY_POLL_MS)) {
            s_battery_ms = now ? now : 1;
            s_battery = bsp_battery_soc();
        }
        kj_model_env_t env;
        switch (s_mode) {
        case MODE_TITLE:
            model_env(&env, now);
            kj_model_title(&s_model, &s_flow, &env, s_battery, now);
            break;
        case MODE_SETTINGS:
            model_env(&env, now);
            kj_model_settings(&s_model, &s_flow, &env, s_battery, now);
            break;
        case MODE_REGISTER: register_tick(now); break;
        case MODE_PROVISION: provision_tick(now); break;
        case MODE_PLAYER: player_tick(now); break;
        case MODE_HOST:
            if (s_server) {
                host_tick(now);
            } else {
                s_mode = MODE_TITLE;
            }
            break;
        }
        update_backlight(now);
        if ((uint32_t)(now - s_last_render) >= RENDER_MIN_MS || s_dirty) {
            if (bsp_lvgl_lock(100)) {
                kj_ui_render(&s_model);
                bsp_lvgl_unlock();
                s_last_render = now;
                s_dirty = false;
            }
        }
        if ((uint32_t)(now - s_heap_ms) >= HEAP_LOG_MS) {
            s_heap_ms = now;
            log_heap("periodic");
        }
    }
}

static void font_missing(const char *font, uint32_t cp)
{
    ESP_LOGE(TAG, "font %s lacks U+%04X", font, (unsigned)cp);
}

static void start_radio(void)
{
    s_nametab = calloc(1, sizeof(kj_names_t));   // 分配失败只是没有昵称，照样能玩
    if (s_nametab) kj_names_init(s_nametab, esp_random());
    log_heap("before radio");
    esp_err_t err = kj_radio_start(on_net_frame);   // 与电脑服务模式共用同一个"只拷贝入队"的收包回调
    s_radio_ok = err == ESP_OK;
    if (!s_radio_ok) {
        ESP_LOGE(TAG, "radio start failed: %s", esp_err_to_name(err));
        kj_flow_toast(&s_flow, KJ_TOAST_RADIO_FAIL, now_ms());
    }
    log_heap("after radio");
}

static void start_net(const kj_wifi_cred_t *cred)
{
    const kj_net_cbs_t cbs = {
        .on_frame = on_net_frame, .on_ctl = on_net_ctl, .on_event = on_net_event, .on_line = on_board_line,
    };
    log_heap("before wifi");
    esp_err_t err = kj_net_start(cred, &cbs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi start failed: %s", esp_err_to_name(err));
        kj_flow_toast(&s_flow, KJ_TOAST_RADIO_FAIL, now_ms());
    }
    log_heap("after wifi");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Limited RPS %s starting", KJ_FW_VERSION);
    kj_store_init();   // 射频校准缓存也依赖 NVS
    s_conn = kj_store_get_conn();
    bsp_i2c_init();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display/LVGL init failed (MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    s_queue = xQueueCreate(APP_QUEUE_DEPTH, sizeof(app_msg_t));
    if (s_conn == KJ_CONN_HUB) s_hubq = xQueueCreate(HUB_QUEUE_DEPTH, sizeof(hub_msg_t));
    if (!s_queue || (s_conn == KJ_CONN_HUB && !s_hubq)) {
        ESP_LOGE(TAG, "no memory for event queues");
        return;
    }
    if (bsp_button_init(on_key, NULL) != ESP_OK) ESP_LOGE(TAG, "button init failed");
    kj_fonts_init();

    // Wi-Fi 凭据：NVS 里配网保存的优先；开发时可以在本地 sdkconfig 里写 CONFIG_KJ_DEV_WIFI_*（不进仓库）。
    kj_wifi_cred_t cred;
    s_have_cred = kj_store_get_wifi(&cred);
    if (!s_have_cred && sizeof(CONFIG_KJ_DEV_WIFI_SSID) > 1) {
        memset(&cred, 0, sizeof(cred));
        snprintf(cred.ssid, sizeof(cred.ssid), "%s", CONFIG_KJ_DEV_WIFI_SSID);
        snprintf(cred.pass, sizeof(cred.pass), "%s", CONFIG_KJ_DEV_WIFI_PASSWORD);
        s_have_cred = true;
    }
    if (s_have_cred) snprintf(s_ssid, sizeof(s_ssid), "%s", cred.ssid);
    // 开机请求只在对应的联机方式下生效：电脑服务模式没有凭据就配网；直连模式可能要开热点登记昵称
    bool provision = false, name_ap = false, play = false;
    if (s_conn == KJ_CONN_HUB) {
        provision = kj_store_take_prov_request() || !s_have_cred;
    } else {
        name_ap = kj_store_take_name_ap_request(&s_name_then_play);
        play = !name_ap && kj_store_take_play_request();
    }
    if (!provision && !name_ap) start_audio();   // 热点模式不初始化音频：把内存让给热点与网页服务
    s_battery_ok = bsp_battery_init() == ESP_OK;

    kj_net_get_mac(s_mac);
    char name[KJ_NAME_MAX + 1];
    uint32_t name_rev = 0;
    kj_store_get_name(name, &name_rev);
    s_hint_saved = kj_store_get_hub_hint();
    kj_hubc_init(&s_hubc, s_mac, (uint16_t)(esp_random() | 1u), KJ_FW_VERSION, name, name_rev, s_hint_saved);
    kj_flow_init(&s_flow, kj_store_get_role());
    kj_flow_set_conn(&s_flow, s_conn);
    ESP_LOGI(TAG, "connection mode: %s", s_conn == KJ_CONN_HUB ? "hub (Wi-Fi + computer)" : "direct (ESP-NOW)");
    if (provision) {
        start_hotspot(KJ_PROV_KIND_WIFI);
    } else if (name_ap) {
        start_hotspot(KJ_PROV_KIND_NAME);
    } else if (s_conn == KJ_CONN_HUB) {
        start_net(&cred);
        s_mode = MODE_TITLE;
    } else {
        start_radio();
        s_mode = MODE_TITLE;
        if (play && s_radio_ok) {   // 刚登记完昵称（或取消登记）：回到"我是选手"
            kj_store_set_role(0);
            enter_player();
        }
    }
    memset(&cred, 0, sizeof(cred));   // 密码只留在 Wi-Fi 驱动里

    kj_model_env_t env;
    model_env(&env, now_ms());
    if (s_mode == MODE_PROVISION) {
        kj_model_provision(&s_model, &s_flow, &env, s_prov_kind, s_prov_state, s_prov_qr, s_prov_ssid, s_prov_pass,
                           -1, now_ms());
    } else {
        kj_model_title(&s_model, &s_flow, &env, -1, now_ms());
    }
    if (bsp_lvgl_lock(1000)) {
        int missing = kj_fonts_selfcheck(font_missing);
        if (missing) ESP_LOGE(TAG, "font self-check: %d missing glyph(s)", missing);
        else ESP_LOGI(TAG, "font self-check: all glyphs present");
        kj_ui_init();
        kj_ui_render(&s_model);
        bsp_lvgl_unlock();
    }
    bsp_display_backlight(100);
    log_heap("ui ready");
    if (xTaskCreate(app_task, "kj_app", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "cannot create app task");
    }
}
