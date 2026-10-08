// main/kj_proto.h —— 限定猜拳的游戏帧格式（直连模式下是 ESP-NOW 载荷，电脑服务模式下是经 hub 中继的 UDP 载荷）。
// 纯 C，主机测试见 tests/test_kj_proto.c。
//
// 所有帧都以 6 字节头开始：'K' 'J' 版本 类型 赌局号(2 字节小端)。
//   ROOM  庄家每秒广播：阶段、人数、空闲选手位图、电脑选手位图（编号 1..128 → 位 0..127）
//   HELLO 选手每 1.5 s 心跳（ESP-NOW 下广播，附近的选手据此按信号强弱排序对手；经 hub 时只单播给庄家）；
//         收到新视图后立即单播给庄家作确认
//   REQ   选手 → 庄家单播请求（入座 / 挑战 / 应战 / 出牌 / 碰拳…），带序号，庄家去重；
//         age_ms = 请求发起到这次发送经过的时间（庄家据此还原碰拳的按下时刻），
//         boot = 选手本次开机的随机数（设备重启后去重基准随之重置）
//   VIEW  庄家 → 选手单播视图（只含自己的手牌，对手只透露"是否已出牌"），带庄家开机随机数 epoch
//   NAME  只在直连模式：入座的选手每隔几秒广播"我是几号、昵称是什么"，庄家和同一赌局的选手据此显示昵称
//         （电脑服务模式下昵称由 hub 的登记表提供，不发这种帧）。见 kj_names.h
// 可靠性由应用层负责：请求按序号重发直到视图里的 ack_seq 追上；视图按版本号重发直到 HELLO 确认。
// 经 UDP 中继时帧可能乱序：选手只接受同一 epoch 内版本号不倒退的视图。
//
// 版本 2：REQ 增加 age_ms / boot，VIEW 增加 epoch，新增 KJ_OP_BUMP。不接受版本 1 的帧。
// NAME 是后加的帧类型，不认识它的设备会整帧丢弃，所以没有升版本。
#pragma once

#include "kj_rules.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KJ_PROTO_VERSION 2
#define KJ_FRAME_HEADER  6
#define KJ_FRAME_MAX     64
#define KJ_BITMAP_BYTES  (KJ_MAX_PLAYERS / 8)

// 昵称（两种联机方式共用，与 tools/kj_charset.py 一致）
#define KJ_NAME_MAX       24     // UTF-8 字节上限（不含结尾 0）
#define KJ_NAME_MAX_UNITS 16     // 显示宽度上限：汉字记 2、ASCII 记 1（约 8 个汉字）

typedef enum {
    KJ_F_ROOM = 1,
    KJ_F_HELLO = 2,
    KJ_F_REQ = 3,
    KJ_F_VIEW = 4,
    KJ_F_NAME = 5,
} kj_frame_type_t;

typedef enum {
    KJ_OP_JOIN = 1,
    KJ_OP_CHALLENGE,   // arg = 对手编号
    KJ_OP_CANCEL,
    KJ_OP_ACCEPT,
    KJ_OP_DECLINE,
    KJ_OP_PLAY,        // arg = kj_card_t
    KJ_OP_WITHDRAW,
    KJ_OP_BUMP,        // 碰拳（CANCEL 也用来取消碰拳配对）
    KJ_OP_COUNT,
} kj_op_t;

#define KJ_HELLO_JOINED 0x01

typedef struct {
    uint8_t phase;
    uint16_t game_id;
    uint16_t phase_ver;
    uint8_t seated;
    uint8_t online;
    uint8_t avail[KJ_BITMAP_BYTES];   // 当前可被挑战的选手
    uint8_t bots[KJ_BITMAP_BYTES];    // 电脑选手
} kj_room_t;

typedef struct {
    uint8_t no;
    uint8_t flags;
    uint16_t view_ver;
} kj_hello_t;

typedef struct {
    uint16_t seq;
    uint8_t op;
    uint8_t arg;
    uint16_t age_ms;   // 请求发起到本次发送经过的毫秒数（重发时递增，封顶 0xFFFF）
    uint16_t boot;     // 选手本次开机的随机数（非 0）
} kj_req_t;

// NAME 载荷：no u8, flags u8（保留，0）, len u8, name[len]（UTF-8，不含控制字符；len = 0 表示没有昵称）
typedef struct {
    uint8_t no;
    uint8_t flags;
    char name[KJ_NAME_MAX + 1];
} kj_name_t;

typedef struct {
    uint8_t type;
    uint16_t room;
    union {
        kj_room_t room_info;
        kj_hello_t hello;
        kj_req_t req;
        kj_view_t view;
        kj_name_t name;
    } u;
} kj_frame_t;

// 编码到 buf，返回长度；cap 不足返回 0。
size_t kj_proto_encode(const kj_frame_t *f, uint8_t *buf, size_t cap);
// 解码并校验魔数 / 版本 / 长度 / 类型；失败返回 false。
bool kj_proto_decode(const uint8_t *buf, size_t len, kj_frame_t *out);
// 只看帧头：魔数与版本对得上时返回帧类型，否则返回 0（用来在完整解码前分流）。
uint8_t kj_proto_peek_type(const uint8_t *buf, size_t len);

static inline bool kj_bit_get(const uint8_t *bm, uint8_t no)
{
    if (no == 0 || no > KJ_MAX_PLAYERS) return false;
    return (bm[(no - 1) / 8] >> ((no - 1) % 8)) & 1u;
}

static inline void kj_bit_set(uint8_t *bm, uint8_t no)
{
    if (no == 0 || no > KJ_MAX_PLAYERS) return;
    bm[(no - 1) / 8] |= (uint8_t)(1u << ((no - 1) % 8));
}

// 待发送帧队列（纯逻辑层产出，平台层负责实际发送）。
typedef struct {
    uint8_t mac[6];
    uint8_t broadcast;
    uint8_t len;
    uint8_t data[KJ_FRAME_MAX];
} kj_out_t;

#define KJ_OUTBOX_MAX 24

typedef struct {
    kj_out_t items[KJ_OUTBOX_MAX];
    int count;
    int dropped;
} kj_outbox_t;

static inline void kj_outbox_clear(kj_outbox_t *o) { o->count = 0; o->dropped = 0; }
// mac == NULL 表示广播。队列满或编码失败返回 false。
bool kj_outbox_push(kj_outbox_t *o, const uint8_t *mac, const kj_frame_t *f);

// 序号比较（16 位回绕）：a 比 b 新返回 > 0。
static inline int kj_seq_diff(uint16_t a, uint16_t b) { return (int16_t)(uint16_t)(a - b); }
