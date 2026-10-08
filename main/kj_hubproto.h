// main/kj_hubproto.h —— 设备与电脑 hub（tools/kj_hub）之间的 UDP 线协议。纯 C，主机测试见
// tests/test_kj_hubproto.c；Python 一侧在 tools/kj_hub/wire.py，两边常量由契约测试与
// tests/data/kj_hub_vectors.txt 的黄金向量保持一致。
//
// 所有数据报都以 24 字节信封开头（小端）：
//   0  'K' 'H'        魔数
//   2  ver            KH_VERSION
//   3  kind           KH_K_*
//   4  src[6]         发送方 STA MAC（hub 自己用 00 00 + hub_id）
//   10 dst[6]         对端 MAC；FF×6 = 本赌局广播；00×6 = 发给 hub 本身
//   16 room           赌局号（0 = 未入座 / 不相关）
//   18 seq            发送方自增序号（排错用）
//   20 rssi           发送方到路由器的信号强度（int8，0 = 未知）
//   21 flags          bit0 = 发送方是庄家
//   22 len            载荷长度
//   24 payload[len]
// 载荷 ≤ KH_PAYLOAD_MAX，整个数据报不超过一个以太网 MTU（设备侧不做 IP 分片重组）。
//
// kind：
//   FRAME      双向：中继的游戏帧（kj_proto，≤ KJ_FRAME_MAX）。hub 按 dst 转发；
//              庄家的广播（信标）扇出给所有设备，选手的广播只转给本赌局的庄家。
//   DISCOVER   设备 → hub（找不到 hub 时广播，找到后每 5 s 单播一次作保活）：
//              hub_proto u8, game_proto u8, role u8, boot u16, fw[12], name_rev u32, name_len u8, name
//   OFFER      hub → 设备（回复 DISCOVER；hub 也每 2 s 广播一次）：
//              hub_id u32, http_port u16, tcp_port u16, roster_rev u32, flags u8, name_rev u32, name_len u8, name
//              flags：bit0 = 名字字段对你有效（照此更新本机昵称，空 = 已被清除）；bit1 = 协议不兼容
//   REG        设备 → hub（登记页显示期间每秒一次）：token[8]
//   REG_STATE  hub → 设备：token[8], state u8, name_rev u32, name_len u8, name
//   NAME_GET   设备 → hub：count u8 (≤ KH_NAMES_MAX), nos[count]（赌局号取信封里的 room）
//   NAMES      hub → 设备：roster_rev u32, count u8, {no u8, flags u8, len u8, name[len]} × count
//              flags：bit0 = 电脑选手，bit1 = 没有登记昵称
#pragma once

#include "kj_proto.h"   // KJ_NAME_MAX、KJ_FRAME_MAX

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KH_VERSION       1
#define KH_HDR           24
#define KH_PAYLOAD_MAX   480
#define KH_DGRAM_MAX     (KH_HDR + KH_PAYLOAD_MAX)

#define KH_PORT_HUB      47101   // hub 收 UDP 的端口
#define KH_PORT_DEVICE   47102   // 设备收 UDP 的端口
#define KH_PORT_TCP      47103   // 庄家看板 TCP（hub 可在 OFFER 里改）
#define KH_PORT_HTTP     47180   // hub 网页（登记页 / 看板，hub 可在 OFFER 里改）

#define KJ_REG_TOKEN_LEN 8       // 登记 token：base32（A-Z、2-7）
#define KH_FW_LEN        12
#define KH_NAMES_MAX     6       // 一次查询 / 回复的昵称条数上限

#define KH_FLAG_HOST     0x01    // 信封 flags：发送方是庄家

typedef enum {
    KH_K_FRAME = 1,
    KH_K_DISCOVER = 2,
    KH_K_OFFER = 3,
    KH_K_REG = 4,
    KH_K_REG_STATE = 5,
    KH_K_NAME_GET = 6,
    KH_K_NAMES = 7,
    KH_K_COUNT,
} kh_kind_t;

typedef enum {
    KH_ROLE_NONE = 0,
    KH_ROLE_PLAYER = 1,
    KH_ROLE_HOST = 2,
} kh_role_t;

#define KH_OFFER_NAME_VALID   0x01
#define KH_OFFER_INCOMPATIBLE 0x02

typedef enum {
    KH_REG_INVALID = 0,   // token 不存在或已过期
    KH_REG_WAITING = 1,   // 等手机扫码
    KH_REG_OPENED = 2,    // 手机已打开登记页
    KH_REG_DONE = 3,      // 已登记，带昵称
} kh_reg_state_t;

#define KH_NAME_BOT     0x01
#define KH_NAME_UNKNOWN 0x02

typedef struct {
    uint8_t kind;
    uint8_t src[6];
    uint8_t dst[6];
    uint16_t room;
    uint16_t seq;
    int8_t rssi;
    uint8_t flags;
    uint16_t len;
    const uint8_t *payload;   // 解码时指向输入缓冲区内部
} kh_env_t;

typedef struct {
    uint8_t hub_proto, game_proto, role;
    uint16_t boot;
    char fw[KH_FW_LEN + 1];
    uint32_t name_rev;
    char name[KJ_NAME_MAX + 1];
} kh_discover_t;

typedef struct {
    uint32_t hub_id;
    uint16_t http_port, tcp_port;
    uint32_t roster_rev;
    uint8_t flags;
    uint32_t name_rev;
    char name[KJ_NAME_MAX + 1];
} kh_offer_t;

typedef struct {
    char token[KJ_REG_TOKEN_LEN + 1];
    uint8_t state;
    uint32_t name_rev;
    char name[KJ_NAME_MAX + 1];
} kh_reg_state_msg_t;

typedef struct {
    uint8_t no, flags;
    char name[KJ_NAME_MAX + 1];
} kh_name_entry_t;

typedef struct {
    uint32_t roster_rev;
    uint8_t count;
    kh_name_entry_t e[KH_NAMES_MAX];
} kh_names_t;

extern const uint8_t KH_MAC_BROADCAST[6];
extern const uint8_t KH_MAC_HUB[6];

// 信封：编码整个数据报（头 + payload）；不合法或缓冲区不足返回 0。
size_t kh_env_encode(const kh_env_t *e, uint8_t *buf, size_t cap);
// 校验魔数 / 版本 / kind / 长度；payload 指向 buf 内部。
bool kh_env_decode(const uint8_t *buf, size_t len, kh_env_t *out);

// 各控制消息的载荷编解码（返回载荷长度；0 = 失败）。名字一律校验 UTF-8 并按字符边界截断到 KJ_NAME_MAX。
size_t kh_discover_encode(const kh_discover_t *m, uint8_t *buf, size_t cap);
bool kh_discover_decode(const uint8_t *p, size_t n, kh_discover_t *out);
size_t kh_offer_encode(const kh_offer_t *m, uint8_t *buf, size_t cap);
bool kh_offer_decode(const uint8_t *p, size_t n, kh_offer_t *out);
size_t kh_reg_encode(const char token[KJ_REG_TOKEN_LEN], uint8_t *buf, size_t cap);
bool kh_reg_decode(const uint8_t *p, size_t n, char token[KJ_REG_TOKEN_LEN + 1]);
size_t kh_reg_state_encode(const kh_reg_state_msg_t *m, uint8_t *buf, size_t cap);
bool kh_reg_state_decode(const uint8_t *p, size_t n, kh_reg_state_msg_t *out);
size_t kh_name_get_encode(const uint8_t *nos, int count, uint8_t *buf, size_t cap);
int kh_name_get_decode(const uint8_t *p, size_t n, uint8_t nos[KH_NAMES_MAX]);   // 失败返回 -1
size_t kh_names_encode(const kh_names_t *m, uint8_t *buf, size_t cap);
bool kh_names_decode(const uint8_t *p, size_t n, kh_names_t *out);

// token 字符是否合法（A-Z、2-7）
bool kh_token_valid(const char *token);
