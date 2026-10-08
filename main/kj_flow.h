// main/kj_flow.h —— 选手 / 庄家设备的界面状态机：由协议状态推导当前页面，
// 把三个按键翻译成请求或本地动作，并产出提示音、toast；另有首页、设置、登记昵称、热点页（配网 / 登记昵称）的按键处理。
// 纯 C，主机测试见 tests/test_kj_flow.c。
//
// 按键约定（全应用统一）：
//   ▲ / ▼ 按下   在列表 / 牌之间移动（按下即响应）
//   OK   单击   确认当前页面的主要动作
//   OK   长按   返回 / 撤回 / 拒绝 / 放弃（每页底部有提示）；唯一例外是手牌页的长按 = 碰拳
#pragma once

#include "kj_client.h"
#include "kj_rules.h"
#include "kj_server.h"

#include <stdbool.h>
#include <stdint.h>

// 联机方式（开机时从 NVS 读出；切换后重启生效）
typedef enum {
    KJ_CONN_DIRECT = 0,   // 直连：设备之间用 ESP-NOW 直接通信，不需要电脑和路由器（默认）
    KJ_CONN_HUB = 1,      // 电脑服务：连现场 Wi-Fi，经 tools/kj_hub 中继
} kj_conn_t;

typedef enum {
    KJ_KEY_UP = 0,
    KJ_KEY_DOWN,
    KJ_KEY_OK,
    KJ_KEY_OK_LONG,
} kj_key_t;

typedef enum {
    KJ_PAGE_TITLE = 0,   // 选择角色
    KJ_PAGE_ROOMS,       // 找赌局
    KJ_PAGE_SEAT,        // 已入座，等待开局 / 下一局
    KJ_PAGE_HAND,        // 手牌主页（空闲）
    KJ_PAGE_OPPONENTS,   // 选择对手
    KJ_PAGE_WAIT,        // 已发出挑战，等待应战
    KJ_PAGE_CHALLENGED,  // 收到挑战
    KJ_PAGE_CHOOSE,      // 出牌
    KJ_PAGE_REVEAL,      // 亮牌结算
    KJ_PAGE_FINAL,       // 过关 / 出局 / 失败
    KJ_PAGE_HOST,        // 庄家面板
    KJ_PAGE_BUMP,        // 已碰拳，等庄家配对
    KJ_PAGE_MATCHED,     // 碰拳配对成功，倒计时后开打
    KJ_PAGE_SETTINGS,    // 设置：昵称 / 重新配网 / 联机方式 / 本机信息
    KJ_PAGE_REGISTER,    // 登记昵称：电脑服务模式扫码；直连模式先说明，再重启进热点登记
    KJ_PAGE_PROVISION,   // 热点页：设备开热点，手机网页里选 Wi-Fi（配网）或填写昵称（直连模式登记）
    KJ_PAGE_HOST_ROSTER, // 庄家的选手名单
    KJ_PAGE_COUNT,
} kj_page_t;

typedef enum {
    KJ_CUE_NONE = 0,
    KJ_CUE_KEY,          // 轻点
    KJ_CUE_ALERT,        // 收到挑战
    KJ_CUE_DUEL,         // 对决开始
    KJ_CUE_LOCK,         // 出牌
    KJ_CUE_WIN,
    KJ_CUE_LOSE,
    KJ_CUE_DRAW,
    KJ_CUE_CLEARED,
    KJ_CUE_OUT,          // 出局 / 失败
    KJ_CUE_NOTICE,       // toast
    KJ_CUE_ERROR,
    KJ_CUE_BUMP,         // 碰拳
    KJ_CUE_MATCH,        // 碰拳配对成功
    KJ_CUE_COUNT,
} kj_cue_t;

typedef enum {
    KJ_TOAST_NONE = 0,
    KJ_TOAST_DECLINED, KJ_TOAST_CANCELLED, KJ_TOAST_TIMEOUT, KJ_TOAST_WITHDRAWN, KJ_TOAST_ABORTED,
    KJ_TOAST_BUSY, KJ_TOAST_NOT_RUNNING, KJ_TOAST_NO_CARD, KJ_TOAST_INVALID, KJ_TOAST_FULL,
    KJ_TOAST_KICKED, KJ_TOAST_NO_REPLY, KJ_TOAST_RESTORED, KJ_TOAST_NEED_TWO, KJ_TOAST_DONE,
    KJ_TOAST_RADIO_FAIL, KJ_TOAST_BUMP_ALONE, KJ_TOAST_BUMP_CROWD, KJ_TOAST_MATCH_CANCELLED,
    KJ_TOAST_NAME_UPDATED, KJ_TOAST_NEED_HUB, KJ_TOAST_OLD_FW, KJ_TOAST_NO_WIFI, KJ_TOAST_RESTARTING,
    KJ_TOAST_COUNT,
} kj_toast_t;

#define KJ_TOAST_MS        2600
#define KJ_REVEAL_AUTO_MS  12000   // 亮牌页无人操作也会在这么久后自动收起
#define KJ_BUMP_LOCAL_MS    2500   // 发出碰拳后最多这么久没有结果就回到手牌页

typedef enum {
    KJ_ACT_NONE = 0,
    KJ_ACT_REQUEST,      // 发送 op/arg 请求
    KJ_ACT_JOIN,         // 入座 room
    KJ_ACT_LEAVE,        // 本地离座，回到找赌局
    KJ_ACT_TO_TITLE,     // 回到选择角色
    KJ_ACT_ROLE,         // 首页选定角色：arg = 0 选手 / 1 庄家
    KJ_ACT_HOST_CMD,     // 庄家命令：cmd
    KJ_ACT_SETTINGS,     // 首页 → 设置
    KJ_ACT_SET_ITEM,     // 设置页选定一项：arg = kj_settings_item_t（KJ_SET_NAME / KJ_SET_WIFI）
    KJ_ACT_SET_CONN,     // 设置页确认切换联机方式：arg = 新的 kj_conn_t（调用方保存后重启）
    KJ_ACT_BACK,         // 返回首页（设置 / 登记）
    KJ_ACT_REG_REFRESH,  // 登记页（电脑服务）：换一个二维码
    KJ_ACT_REG_START,    // 登记页（直连）：重启进入热点登记
    KJ_ACT_PROV_SKIP,    // 热点页：先跳过 / 取消
} kj_action_kind_t;

// 设置页的项（编号固定；实际显示哪些、什么顺序见 kj_flow_settings_items）
typedef enum {
    KJ_SET_NAME = 0,     // 登记 / 修改昵称
    KJ_SET_WIFI,         // 重新配网（只在电脑服务模式）
    KJ_SET_CONN,         // 改用另一种联机方式（先确认，再重启）
    KJ_SET_BACK,
    KJ_SET_COUNT,
} kj_settings_item_t;

#define KJ_TITLE_ITEMS 3   // 选手 / 庄家 / 设置
#define KJ_ROSTER_ROWS 5   // 庄家名单一屏显示的行数

typedef struct {
    uint8_t kind;
    uint8_t op;
    uint8_t arg;
    uint16_t room;
    kj_cmd_t cmd;
} kj_action_t;

// 选手侧每轮的输入快照（由平台层从 kj_client_t 生成）。
typedef struct {
    const kj_client_t *client;
    const kj_room_entry_t *rooms;
    int room_count;
    const kj_opponent_t *opps;
    int opp_count;
    bool connected;
    uint32_t now_ms;
} kj_player_ctx_t;

typedef enum {
    KJ_HM_START = 0, KJ_HM_END, KJ_HM_NEW, KJ_HM_BOT_ADD, KJ_HM_BOT_DEL, KJ_HM_RESET, KJ_HM_ROSTER, KJ_HM_COUNT,
} kj_host_item_t;

typedef struct {
    uint8_t conn;              // kj_conn_t
    // 首页 / 设置
    uint8_t title_sel;
    uint8_t settings_sel;      // 设置页可见项里的序号
    bool settings_confirm;     // 正在确认切换联机方式
    // 选手
    uint8_t room_sel;
    uint8_t opp_sel;
    uint8_t opp_no;            // 记住选中的编号：列表重新排序时选中项不跳
    uint8_t card_sel;
    bool picking;              // 在选择对手页
    bool bump_pending;         // 刚发出碰拳、庄家还没回结果（先显示碰拳页）
    uint32_t bump_since;
    uint16_t shown_res_duel;   // 已经看过的亮牌（对决编号）
    bool reveal_active;
    uint32_t reveal_since;
    uint16_t game_id;
    uint16_t room;
    uint8_t notice_seq;
    uint8_t last_status;
    uint8_t last_page;
    bool primed;               // 已同步过一次视图（首次入座不对历史通知响铃）
    // toast
    uint8_t toast;
    uint32_t toast_until;
    // 庄家
    uint8_t host_sel;
    int8_t host_confirm;       // 等待确认的菜单项，-1 = 无
    bool host_roster;          // 正在看选手名单
    uint8_t roster_first;      // 名单滚动位置（第一行的序号）
} kj_flow_t;

void kj_flow_init(kj_flow_t *f, uint8_t title_sel);   // 联机方式默认直连
void kj_flow_set_conn(kj_flow_t *f, uint8_t conn);
void kj_flow_toast(kj_flow_t *f, kj_toast_t t, uint32_t now_ms);
kj_toast_t kj_flow_active_toast(const kj_flow_t *f, uint32_t now_ms);
kj_toast_t kj_flow_toast_for_notice(uint8_t notice);

// 首页（▲▼ 在三项间移动，OK 选定）、设置页、登记页、热点页
kj_action_t kj_flow_title_key(kj_flow_t *f, kj_key_t key);
// 设置页当前显示的项（kj_settings_item_t，按显示顺序），返回个数：
//   直连：登记昵称 / 改用电脑服务 / 返回；电脑服务：登记昵称 / 重新配网 / 改用直连 / 返回
int kj_flow_settings_items(const kj_flow_t *f, uint8_t items[KJ_SET_COUNT]);
// 进入设置页并选中某一项（当前联机方式下没有这一项就选第一项）。
void kj_flow_settings_open(kj_flow_t *f, uint8_t item);
// 选中"改用…"后先进入确认：OK 确定（KJ_ACT_SET_CONN），其他键取消。
kj_action_t kj_flow_settings_key(kj_flow_t *f, kj_key_t key);
kj_action_t kj_flow_register_key(kj_flow_t *f, kj_key_t key);
kj_action_t kj_flow_provision_key(kj_flow_t *f, kj_key_t key);

// 选手：每轮调用，返回应显示的页面，*cue 写入需要播放的提示音（可为 NULL）。
kj_page_t kj_flow_player_update(kj_flow_t *f, const kj_player_ctx_t *ctx, kj_cue_t *cue);
kj_action_t kj_flow_player_key(kj_flow_t *f, const kj_player_ctx_t *ctx, kj_key_t key);
// 请求没能发出（已有未确认的请求）时调用：撤销本地的"碰拳中"。
void kj_flow_bump_rejected(kj_flow_t *f);
// 当前出牌页可选的牌（跳过已用完的）；没有可选返回 KJ_CARD_NONE。
uint8_t kj_flow_valid_card(const kj_view_t *v, uint8_t preferred, int direction);

// 庄家（面板菜单；选中"选手名单"后 ▲▼ 滚动名单，OK / 长按回到面板）
bool kj_flow_host_item_enabled(kj_host_item_t item, const kj_game_t *g);
kj_action_t kj_flow_host_key(kj_flow_t *f, const kj_game_t *g, kj_key_t key, uint32_t now_ms);
// 每轮调用：选中项不可用时顺延到下一个可用项；确认中的项失效时取消确认；名单滚动位置收敛。
void kj_flow_host_sync(kj_flow_t *f, const kj_game_t *g);
