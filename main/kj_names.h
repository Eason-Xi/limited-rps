// main/kj_names.h —— 直连模式（不经电脑）的昵称表。纯 C，主机测试见 tests/test_kj_names.c。
//
// 没有电脑服务的登记表时，昵称只存在各自的选手设备里：入座的选手每隔几秒广播一次 NAME 帧
//（赌局号 + 编号 + 昵称），庄家和同一赌局的其他选手按编号记下来，界面需要时直接查表。
//   * 一张表只记一个赌局。选手自己的座位变了（换赌局、被移出后重新入座、庄家清空后重新编号）就整表清空，
//     几秒内由各人的广播重新填满；同一编号以最新一次广播为准。
//   * 庄家查表时带上座位登记的 MAC：只认这台设备自己报的昵称（座位换了人，旧昵称自动失效）。
//   * 广播时机：入座、编号或昵称变化后立即发一次，之后每 KJ_NAMES_TAG_MS（±20% 抖动，避免大家同时发）一次。
#pragma once

#include "kj_proto.h"

#include <stdbool.h>
#include <stdint.h>

#define KJ_NAMES_TAG_MS 3000

typedef struct {
    uint8_t known;                 // 收到过这个编号的广播
    uint8_t mac[6];                // 广播它的设备
    char name[KJ_NAME_MAX + 1];    // 空串 = 对方没有登记昵称
} kj_name_entry_t;

typedef struct {
    uint16_t room;                 // 本表记的是哪个赌局（0 = 还没有）
    kj_name_entry_t seat[KJ_MAX_PLAYERS];
    // 本机（选手）的座位与广播
    uint16_t seat_room;
    uint8_t seat_no;
    bool seated;                   // 上一轮是否在座（重新入座时立即广播一次）
    char tag_name[KJ_NAME_MAX + 1];
    bool tag_due;
    uint32_t next_tag_ms;
    uint32_t rng;
} kj_names_t;

void kj_names_init(kj_names_t *t, uint32_t seed);
// 改记某个赌局：与当前不同就清空整表（room = 0 时不变）。庄家开局时调用一次。
void kj_names_set_room(kj_names_t *t, uint16_t room);
// 记下一条 NAME 广播（mac 为发送方）。不属于当前赌局的忽略。返回表是否有变化。
bool kj_names_store(kj_names_t *t, const uint8_t mac[6], uint16_t room, const kj_name_t *n);
// 查某个编号的昵称：收到过返回 true，*name 指向表内（可能是空串 = 对方没有昵称）。
// mac 非 NULL 时还要求是这台设备报的（庄家用座位登记的 MAC 校验）。
bool kj_names_get(const kj_names_t *t, uint16_t room, uint8_t no, const uint8_t *mac, const char **name);
// 选手每轮调用：room / no 为当前入座的赌局与编号（未入座传 0），my_mac / my_name 为本机。
// 自己的座位变了会清空整表；到时间就往 out 里放一条广播（mac = NULL）。
void kj_names_tick(kj_names_t *t, uint16_t room, uint8_t no, const uint8_t my_mac[6], const char *my_name,
                   uint32_t now_ms, kj_outbox_t *out);
