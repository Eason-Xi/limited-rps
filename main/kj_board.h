// main/kj_board.h —— 庄家设备与电脑看板之间的串口文本协议（USB Serial/JTAG）。纯 C。
//
// 设备 → 电脑：每行以 "@KJ " 开头，后接一个 JSON 对象，看板忽略其他日志行。
//   {"t":"hello",...}  启动 / 同步时的身份行（赌局号、固件与协议版本、庄家 MAC、联机方式、人数上限）
//   {"t":"g",...}      赌局汇总（阶段、人数、全场剩余牌、星星总数）
//   {"t":"p",...}      某位选手的完整状态（含手牌、星星、对决中的暗牌、设备 MAC）；"gone":1 表示已移除；
//                      直连模式下还带 "name"（选手设备广播的昵称；电脑服务模式由 hub 的登记表提供，不带）
//   {"t":"e",...}      事件（挑战、应战、碰拳配对、亮牌结算、过关、出局…），用于看板日志；
//                      "x" 为附加数值（碰拳配对时两人按下时刻之差 ms，碰拳失败的原因）
//   {"t":"ack",...}    看板命令的执行结果
// 电脑 → 设备：每行一条命令 "@KJ start|end|new|reset|bot+|bot-|sync|kick <编号>"。
// 看板只给庄家 / 观众看，不要让选手看到（含暗牌）。
#pragma once

#include "kj_proto.h"
#include "kj_rules.h"
#include "kj_server.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KJ_BOARD_PREFIX   "@KJ "
#define KJ_BOARD_LINE_MAX 320

const char *kj_board_status_name(uint8_t status);
const char *kj_board_phase_name(uint8_t phase);
const char *kj_board_card_name(uint8_t card);      // "r" / "s" / "p" / ""
const char *kj_board_event_name(uint8_t kind);
const char *kj_board_notice_name(uint8_t notice);
const char *kj_board_cmd_name(kj_cmd_t cmd);

// 以下函数写入一整行（含结尾换行），返回长度；缓冲区不足返回 0。
// link 为当前联机方式（"espnow" / "wifi"），mac 为庄家设备的 STA MAC。
size_t kj_board_hello_line(uint16_t room, const char *fw, const uint8_t mac[6], const char *link, char *buf,
                           size_t cap);
size_t kj_board_game_line(const kj_game_t *g, uint16_t room, uint32_t now_ms, char *buf, size_t cap);
// name 为 NULL 时不输出 "name" 字段；非 NULL（可为空串）时按 JSON 转义输出。
size_t kj_board_player_line(const kj_game_t *g, int idx, uint32_t now_ms, const char *name, char *buf,
                            size_t cap);
size_t kj_board_event_line(const kj_event_t *e, char *buf, size_t cap);
size_t kj_board_ack_line(kj_cmd_t cmd, int arg, kj_notice_t result, char *buf, size_t cap);

// 解析一行看板命令（允许首尾空白与 \r）。不是命令或格式错误返回 false。
bool kj_board_parse_command(const char *line, kj_cmd_t *cmd, int *arg);

// 4 位十六进制赌局号（大写），out 至少 5 字节。
void kj_board_room_text(uint16_t room, char out[5]);
