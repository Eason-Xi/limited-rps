// main/kj_rules.h —— 限定猜拳（《赌博默示录》）裁判规则引擎：纯 C，无 ESP-IDF / LVGL 依赖。
//
// 原作规则：
//   * 开局每人 石头/剪刀/布 各 4 张、星星 3 颗；
//   * 两人自愿对决：各出一张牌同时亮出，胜者从败者处拿走 1 颗星，平局不动；出过的牌作废；
//   * 星星归零立即出局；手牌全部出完且星星 ≥ 3 颗即过关，出完但不足 3 颗即失败；
//   * 庄家宣布赌局结束（原作的时限）时，仍有手牌的人判负。
//
// 开始对决有两种方式：从名单里挑人发起挑战、对方应战；或两人面对面同时长按"碰拳"，
// 庄家按按下时刻配对（见 kj_bump.h），双方看到对手后倒计时自动开打，期间任一方可取消。
//
// 引擎只由主机（庄家设备）持有，是唯一可信状态；选手设备只拿到投影出来的视图（kj_view_t）。
// 所有时间参数都是单调毫秒（uint32 回绕安全：只做差值比较）。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KJ_MAX_PLAYERS      128   // 选手编号 1..128（协议里用 128 位位图广播空闲名单）
#define KJ_CARDS_PER_TYPE   4
#define KJ_START_STARS      3
#define KJ_CLEAR_STARS      3
#define KJ_NO_NONE          0     // "无选手" 的编号
#define KJ_EVENT_RING       32

// 超时（毫秒）
#define KJ_ONLINE_TIMEOUT_MS     6000   // 超过这么久没收到选手任何帧视为离线
#define KJ_CHALLENGE_TIMEOUT_MS 20000   // 挑战等待应战的时限
#define KJ_DUEL_OFFLINE_ABORT_MS 15000  // 对决中一方离线这么久，对决作废（不消耗手牌）
#define KJ_MATCH_COUNTDOWN_MS    3000   // 碰拳配对成功后自动开打前的倒计时

typedef enum {
    KJ_ROCK = 0,
    KJ_SCISSORS = 1,
    KJ_PAPER = 2,
    KJ_CARD_TYPES = 3,
    KJ_CARD_NONE = 0xFF,
} kj_card_t;

typedef enum {
    KJ_PHASE_LOBBY = 0,    // 入座中，尚未发牌
    KJ_PHASE_RUNNING = 1,  // 赌局进行中
    KJ_PHASE_ENDED = 2,    // 已宣布结束
} kj_phase_t;

typedef enum {
    KJ_ST_WAITING = 0,     // 已入座，等待开局
    KJ_ST_IDLE,            // 空闲，可以挑战 / 被挑战
    KJ_ST_CHALLENGING,     // 已向 peer 发起挑战，等待应战
    KJ_ST_CHALLENGED,      // 收到 peer 的挑战，等待自己应战
    KJ_ST_DUEL,            // 对决中（选牌 / 已出牌等待对方）
    KJ_ST_CLEARED,         // 过关：手牌出完且星星 ≥ 3
    KJ_ST_ELIMINATED,      // 出局：星星归零
    KJ_ST_FAILED,          // 失败：手牌出完但星星不足，或时间到仍有手牌
    // 以下为后加的状态（追加在末尾，旧快照里的数值保持不变）
    KJ_ST_BUMPING,         // 已碰拳，等庄家配对（不到 1 秒）
    KJ_ST_MATCHED,         // 碰拳配对成功，倒计时后自动进入对决；任一方可取消
    KJ_ST_COUNT,
} kj_status_t;

// 结算 / 拒绝原因：每个选手视图里带最近一条，选手端以 toast 显示一次。
typedef enum {
    KJ_N_NONE = 0,
    KJ_N_DECLINED,      // 你的挑战被拒绝
    KJ_N_CANCELLED,     // 对方撤回了挑战
    KJ_N_TIMEOUT,       // 挑战无人应战，已作废
    KJ_N_WITHDRAWN,     // 对方放弃了对决
    KJ_N_ABORTED,       // 对方断线，对决作废
    KJ_N_BUSY,          // 对方正忙或不存在
    KJ_N_NOT_RUNNING,   // 赌局未在进行
    KJ_N_NO_CARD,       // 没有这张牌
    KJ_N_INVALID,       // 当前状态不能这样操作
    KJ_N_FULL,          // 赌局已满
    KJ_N_BUMP_ALONE,    // 碰拳没碰到对手
    KJ_N_BUMP_CROWD,    // 同时碰拳的人太多，没能配对
    KJ_N_MATCH_CANCELLED, // 对方取消了碰拳配对
    KJ_N_COUNT,
} kj_notice_t;

typedef enum {
    KJ_OUT_NONE = 0,
    KJ_OUT_WIN,
    KJ_OUT_LOSE,
    KJ_OUT_DRAW,
} kj_outcome_t;

typedef enum {
    KJ_FINAL_NONE = 0,
    KJ_FINAL_NO_CARDS,  // 手牌出完、星星不足
    KJ_FINAL_TIME_UP,   // 庄家宣布结束时仍有手牌
} kj_final_reason_t;

typedef struct {
    uint8_t used;
    uint8_t is_bot;
    uint8_t mac[6];
    uint8_t status;            // kj_status_t
    uint8_t cards[KJ_CARD_TYPES];
    uint8_t stars;
    uint8_t peer;              // 对手 / 挑战者 / 被挑战者的下标；0xFF 表示无
    uint8_t locked;            // 本次对决已出的牌；KJ_CARD_NONE 表示未出
    uint8_t final_reason;      // kj_final_reason_t
    uint8_t notice;            // kj_notice_t
    uint8_t notice_seq;        // 每产生一条通知 +1
    uint8_t wins, losses, draws;
    uint16_t duel_id;          // 当前对决编号
    uint16_t last_req_seq;     // 已处理的最后一个请求序号（去重）
    uint16_t boot;             // 选手设备本次开机的随机数（去重基准随它重置；不持久化，0 = 未知）
    uint16_t view_ver;         // 视图版本：任何影响该选手视图的变化都 +1
    uint32_t since_ms;         // 进入当前状态的时间
    uint32_t last_seen_ms;     // 最后一次收到该选手的帧
    int8_t rssi;               // 主机侧最后一次的信号强度（仅看板显示）
    uint8_t online;            // 上次 tick 时的在线状态（翻转时刷新看板 / 空闲名单）
    // 最近一次结算（选手端据 res_duel_id 只播放一次亮牌）
    uint16_t res_duel_id;
    uint8_t res_my, res_opp, res_outcome, res_opp_no;
} kj_player_t;

typedef enum {
    KJ_EV_JOIN = 1, KJ_EV_REJOIN, KJ_EV_BOT_ADD, KJ_EV_REMOVE,
    KJ_EV_START, KJ_EV_END, KJ_EV_NEW_GAME, KJ_EV_RESET,
    KJ_EV_CHALLENGE, KJ_EV_CANCEL, KJ_EV_DECLINE, KJ_EV_TIMEOUT, KJ_EV_ACCEPT,
    KJ_EV_LOCK, KJ_EV_RESULT, KJ_EV_WITHDRAW, KJ_EV_ABORT,
    KJ_EV_CLEARED, KJ_EV_ELIMINATED, KJ_EV_FAILED,
    KJ_EV_MATCH, KJ_EV_MATCH_CANCEL, KJ_EV_BUMP_FAIL,
} kj_event_kind_t;

#define KJ_BUMP_FAIL_ALONE 1   // KJ_EV_BUMP_FAIL 的 aux：没碰到对手
#define KJ_BUMP_FAIL_CROWD 2   // KJ_EV_BUMP_FAIL 的 aux：人太多

typedef struct {
    uint8_t kind;      // kj_event_kind_t
    uint8_t a, b;      // 选手编号（1..128），0 = 无
    uint8_t ca, cb;    // a / b 出的牌（RESULT / LOCK），否则 KJ_CARD_NONE
    uint8_t winner;    // RESULT：胜者编号，平局为 0
    uint16_t duel_id;
    uint16_t aux;      // MATCH：两人按下时刻之差（ms）；BUMP_FAIL：KJ_BUMP_FAIL_*
    uint32_t t_ms;
} kj_event_t;

typedef struct {
    uint8_t phase;             // kj_phase_t
    uint16_t game_id;          // 每次"新一局"+1
    uint16_t next_duel_id;
    uint16_t phase_ver;        // 阶段 / 人数变化时 +1（广播信标用）
    uint32_t phase_since_ms;
    uint32_t rev;              // 任何选手状态变化 +1（主机据此决定何时写 NVS）
    kj_player_t players[KJ_MAX_PLAYERS];
    // 看板脏标记：bit i 表示 players[i] 需要重新输出
    uint32_t board_dirty[KJ_MAX_PLAYERS / 32];
    kj_event_t events[KJ_EVENT_RING];
    uint8_t ev_head, ev_count;
} kj_game_t;

// 投影给某一位选手的视图（不含对手暗牌）。
typedef struct {
    uint16_t view_ver;
    uint16_t ack_seq;
    uint16_t game_id;
    uint16_t duel_id;
    uint16_t res_duel_id;
    uint8_t no;                // 自己的编号；0 = 不在本局（被移除）
    uint8_t phase;
    uint8_t status;
    uint8_t cards[KJ_CARD_TYPES];
    uint8_t stars;
    uint8_t peer_no;
    uint8_t peer_is_bot;
    uint8_t my_lock;           // 已出的牌或 KJ_CARD_NONE
    uint8_t peer_locked;       // 对手是否已出牌（不透露是哪张）
    uint8_t notice, notice_seq;
    uint8_t res_my, res_opp, res_outcome, res_opp_no;
    uint8_t final_reason;
    uint8_t wins, losses, draws;
    uint8_t deadline_s;        // 剩余秒数：挑战（CHALLENGING / CHALLENGED）或碰拳倒计时（MATCHED），否则 0
    uint16_t epoch;            // 庄家本次开机的随机数（由 kj_server 填写）：选手据此判断视图新旧
} kj_view_t;

// ---------------------------------------------------------------------------
// 基础
// ---------------------------------------------------------------------------
void kj_rules_init(kj_game_t *g);
static inline uint8_t kj_no_of(int idx) { return (uint8_t)(idx + 1); }
static inline int kj_idx_of(uint8_t no) { return (int)no - 1; }
const kj_player_t *kj_rules_player_by_no(const kj_game_t *g, uint8_t no);
int kj_rules_find_mac(const kj_game_t *g, const uint8_t mac[6]);
int kj_rules_player_count(const kj_game_t *g);
uint8_t kj_rules_total_cards(const kj_player_t *p);
bool kj_rules_is_final(uint8_t status);
bool kj_rules_online(const kj_game_t *g, int idx, uint32_t now_ms);
// 0 = a 赢 b、1 = b 赢 a、-1 = 平局
int kj_rules_compare(uint8_t a, uint8_t b);
// a 是否能挑战 / 被挑战（空闲、在线、赌局进行中）
bool kj_rules_available(const kj_game_t *g, int idx, uint32_t now_ms);

// ---------------------------------------------------------------------------
// 庄家操作（看板命令 / 主机菜单）。返回 KJ_N_NONE 表示成功。
// ---------------------------------------------------------------------------
// 选手按 MAC 入座（已在册则视为重连）。成功返回下标，赌局已满返回 -1。
int kj_rules_join(kj_game_t *g, const uint8_t mac[6], uint32_t now_ms);
int kj_rules_add_bot(kj_game_t *g, uint32_t now_ms);
kj_notice_t kj_rules_remove_bot(kj_game_t *g, uint32_t now_ms);
kj_notice_t kj_rules_remove(kj_game_t *g, int idx, uint32_t now_ms);
kj_notice_t kj_rules_start(kj_game_t *g, uint32_t now_ms);   // 需要 ≥ 2 名选手
kj_notice_t kj_rules_end(kj_game_t *g, uint32_t now_ms);
void kj_rules_new_game(kj_game_t *g, uint32_t now_ms);      // 保留选手，回到入座阶段
void kj_rules_reset(kj_game_t *g, uint32_t now_ms);         // 清空所有选手

// ---------------------------------------------------------------------------
// 选手操作。返回值写入该选手的 notice（失败时）。
// ---------------------------------------------------------------------------
kj_notice_t kj_rules_challenge(kj_game_t *g, int idx, uint8_t target_no, uint32_t now_ms);
kj_notice_t kj_rules_cancel(kj_game_t *g, int idx, uint32_t now_ms);
kj_notice_t kj_rules_respond(kj_game_t *g, int idx, bool accept, uint32_t now_ms);
kj_notice_t kj_rules_play(kj_game_t *g, int idx, uint8_t card, uint32_t now_ms);
kj_notice_t kj_rules_withdraw(kj_game_t *g, int idx, uint32_t now_ms);
// 碰拳：press_ms 为还原后的按下时刻。配对在 kj_rules_tick 里结算；取消配对用 kj_rules_cancel。
kj_notice_t kj_rules_bump(kj_game_t *g, int idx, uint32_t press_ms, uint32_t now_ms);

// 记录收到选手的帧（在线判定）。
void kj_rules_seen(kj_game_t *g, int idx, int8_t rssi, uint32_t now_ms);
// 处理超时（挑战过期、对决中断线）。
void kj_rules_tick(kj_game_t *g, uint32_t now_ms);
// 生成某选手的视图。
void kj_rules_view(const kj_game_t *g, int idx, uint32_t now_ms, kj_view_t *out);
// 给选手设置一条通知（会 bump 视图版本）。
void kj_rules_notify(kj_game_t *g, int idx, kj_notice_t n);
// 标记选手视图 / 看板变化。
void kj_rules_touch(kj_game_t *g, int idx);

// 事件队列（看板日志）。pop 成功返回 true。
bool kj_rules_pop_event(kj_game_t *g, kj_event_t *out);
// 看板脏标记
bool kj_rules_take_dirty(kj_game_t *g, int idx);
void kj_rules_mark_all_dirty(kj_game_t *g);
// 只让看板重新输出某位选手（不改视图版本，例如直连模式下收到了新昵称）
void kj_rules_mark_dirty(kj_game_t *g, int idx);

// 汇总（看板 / 主机界面）
typedef struct {
    int seated, online, in_duel, cleared, eliminated, failed, bots;
    int cards[KJ_CARD_TYPES];  // 场上（未结束选手）剩余各类牌数
    int stars;                 // 全场星星总数
} kj_summary_t;
void kj_rules_summary(const kj_game_t *g, uint32_t now_ms, kj_summary_t *out);
