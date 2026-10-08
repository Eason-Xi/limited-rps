// main/kj_ui.h —— 限定猜拳的 LVGL 界面（240×320 竖屏，自绘，不使用 baseline demo 外壳）。
//
// 平台层每轮把状态整理成 kj_ui_model_t 交给 kj_ui_render()；界面层按"签名"判断是否需要重建
// 当前页面，倒计时 / 电量 / 计时等只做局部刷新。本模块只依赖 LVGL 与纯逻辑头文件，
// 因此可以在电脑上用 tools/render_kj_preview.py 渲染出与板上一致的截图。
#pragma once

#include "kj_client.h"
#include "kj_flow.h"
#include "kj_hubproto.h"
#include "kj_rules.h"

#include <stdbool.h>
#include <stdint.h>

#define KJ_UI_OPP_MAX 32
#define KJ_UI_OPP_ROWS 4          // 选对手页一屏显示的行数
#define KJ_UI_W 240
#define KJ_UI_H 320
#define KJ_UI_NAME_LEN (KJ_NAME_MAX + 1)
#define KJ_UI_TEXT_LEN 72          // 二维码内容 / 网址等

// 联网状态（首页、找赌局、设置、庄家面板显示）
typedef enum {
    KJ_NET_NO_WIFI = 0,   // 电脑服务模式：没有 Wi-Fi 凭据（需要配网）
    KJ_NET_CONNECTING,    // 电脑服务模式：正在连 Wi-Fi
    KJ_NET_SEARCHING,     // 电脑服务模式：已连上 Wi-Fi，正在找电脑服务
    KJ_NET_OK,            // 电脑服务模式：已找到电脑服务
    KJ_NET_OLD,           // 电脑服务模式：电脑服务与本固件协议不兼容
    KJ_NET_DIRECT,        // 直连模式：无线已就绪（不需要电脑和路由器）
    KJ_NET_NO_RADIO,      // 直连模式：无线启动失败
} kj_net_status_t;

// 庄家看板链路
typedef enum {
    KJ_BOARD_NONE = 0,
    KJ_BOARD_USB,         // 只有 USB 串口连着电脑
    KJ_BOARD_WIFI,        // 已通过 Wi-Fi 连上电脑服务
} kj_board_link_t;

// 热点页的用途
typedef enum {
    KJ_PROV_KIND_WIFI = 0,    // 配网：选现场 Wi-Fi（电脑服务模式）
    KJ_PROV_KIND_NAME,        // 登记昵称（直连模式）
} kj_prov_kind_t;

// 热点页进度（登记昵称只用到 WAIT_PHONE / PHONE_IN / OK）
typedef enum {
    KJ_PROV_WAIT_PHONE = 0,   // 等手机连热点
    KJ_PROV_PHONE_IN,         // 手机已连上热点，等提交
    KJ_PROV_TRYING,           // 正在用新凭据连接
    KJ_PROV_OK,               // 成功（连上 Wi-Fi / 昵称已保存），即将重启
    KJ_PROV_FAILED,           // 连接失败（密码错误 / 找不到网络），可在手机上重试
} kj_prov_state_t;

typedef struct {
    uint8_t no;
    uint8_t is_bot;
    uint8_t online;
    uint8_t status;
    uint8_t stars;
    uint8_t cards;
    char name[KJ_UI_NAME_LEN];
} kj_roster_row_t;

typedef struct {
    uint8_t page;                 // kj_page_t
    int8_t battery;               // 0..100；-1 = 读不到
    uint32_t now_ms;
    uint8_t toast;                // kj_toast_t
    bool disconnected;            // 已入座但听不到庄家
    // 联网与本机
    uint8_t conn;                 // kj_conn_t
    uint8_t channel;              // 直连模式的无线信道
    uint8_t net;                  // kj_net_status_t
    uint32_t ip, hub_ip;          // IPv4（网络字节序）
    char ssid[KJ_UI_NAME_LEN + 9];
    char my_name[KJ_UI_NAME_LEN];
    char fw[16];
    uint16_t dev_id;              // MAC 最后两字节（设备背面 / 路由器列表里认得出来）
    // 首页 / 设置
    uint8_t title_sel;
    uint8_t settings_sel;         // settings_items 里的序号
    uint8_t settings_items[KJ_SET_COUNT];   // kj_settings_item_t，按显示顺序
    uint8_t settings_count;
    bool settings_confirm;        // 正在确认切换联机方式
    // 登记 / 热点页
    uint8_t reg_state;            // kh_reg_state_t；0 = 还没连上电脑服务（直连模式不用）
    uint8_t prov_kind;            // kj_prov_kind_t
    uint8_t prov_state;           // kj_prov_state_t
    char qr[KJ_UI_TEXT_LEN];      // 二维码内容（登记网址 / WIFI: 配网串）
    char line1[KJ_UI_TEXT_LEN];   // 二维码下方的文字（网址 / 热点名）
    char line2[KJ_UI_TEXT_LEN];   // （热点口令 / 正在连接的 Wi-Fi 名）
    // 找赌局
    kj_room_entry_t rooms[KJ_CLIENT_MAX_ROOMS];
    uint8_t room_count;
    uint8_t room_sel;
    bool joining;
    // 选手
    uint16_t room;
    kj_view_t view;
    uint8_t seated;
    kj_opponent_t opps[KJ_UI_OPP_MAX];
    uint8_t opp_count;
    uint8_t opp_sel;
    uint8_t opp_first;            // 选对手页第一行
    char opp_names[KJ_UI_OPP_ROWS][KJ_UI_NAME_LEN];
    char peer_name[KJ_UI_NAME_LEN];   // 当前对手 / 挑战者 / 亮牌时的对手
    uint8_t card_sel;
    uint8_t deadline_s;           // 挑战 / 碰拳倒计时（实时）
    // 庄家
    uint16_t host_room;
    uint8_t host_phase;
    uint32_t host_phase_s;        // 当前阶段已持续的秒数
    kj_summary_t host_sum;
    uint8_t host_sel;
    int8_t host_confirm;
    uint8_t host_enabled;         // bit i = kj_host_item_t i 可用
    uint8_t board;                // kj_board_link_t
    kj_roster_row_t roster[KJ_ROSTER_ROWS];
    uint8_t roster_count;         // 本屏行数
    uint8_t roster_first;
    uint8_t roster_total;
} kj_ui_model_t;

// 在 LVGL 初始化之后、持有 LVGL 锁时调用。
void kj_ui_init(void);
// 渲染模型（持有 LVGL 锁时调用）。调用方须先 memset 模型再填写，签名按字节计算。
void kj_ui_render(const kj_ui_model_t *m);
// toast 文案
const char *kj_ui_toast_text(uint8_t toast);
