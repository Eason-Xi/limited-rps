// main/kj_board.c —— 看板串口文本协议（纯 C，主机测试见 tests/test_kj_board.c）。
#include "kj_board.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *kj_board_status_name(uint8_t status)
{
    static const char *const names[KJ_ST_COUNT] = {
        "waiting", "idle", "challenging", "challenged", "duel", "cleared", "out", "failed", "bumping", "matched",
    };
    return status < KJ_ST_COUNT ? names[status] : "?";
}

const char *kj_board_phase_name(uint8_t phase)
{
    switch (phase) {
    case KJ_PHASE_LOBBY: return "lobby";
    case KJ_PHASE_RUNNING: return "run";
    case KJ_PHASE_ENDED: return "ended";
    default: return "?";
    }
}

const char *kj_board_card_name(uint8_t card)
{
    switch (card) {
    case KJ_ROCK: return "r";
    case KJ_SCISSORS: return "s";
    case KJ_PAPER: return "p";
    default: return "";
    }
}

const char *kj_board_event_name(uint8_t kind)
{
    switch (kind) {
    case KJ_EV_JOIN: return "join";
    case KJ_EV_REJOIN: return "rejoin";
    case KJ_EV_BOT_ADD: return "bot";
    case KJ_EV_REMOVE: return "remove";
    case KJ_EV_START: return "start";
    case KJ_EV_END: return "end";
    case KJ_EV_NEW_GAME: return "new";
    case KJ_EV_RESET: return "reset";
    case KJ_EV_CHALLENGE: return "challenge";
    case KJ_EV_CANCEL: return "cancel";
    case KJ_EV_DECLINE: return "decline";
    case KJ_EV_TIMEOUT: return "timeout";
    case KJ_EV_ACCEPT: return "accept";
    case KJ_EV_LOCK: return "lock";
    case KJ_EV_RESULT: return "result";
    case KJ_EV_WITHDRAW: return "withdraw";
    case KJ_EV_ABORT: return "abort";
    case KJ_EV_CLEARED: return "cleared";
    case KJ_EV_ELIMINATED: return "eliminated";
    case KJ_EV_FAILED: return "failed";
    case KJ_EV_MATCH: return "match";
    case KJ_EV_MATCH_CANCEL: return "match_cancel";
    case KJ_EV_BUMP_FAIL: return "bump_fail";
    default: return "?";
    }
}

const char *kj_board_notice_name(uint8_t notice)
{
    static const char *const names[KJ_N_COUNT] = {
        "", "declined", "cancelled", "timeout", "withdrawn", "aborted", "busy",
        "not_running", "no_card", "invalid", "full", "bump_alone", "bump_crowd", "match_cancelled",
    };
    return notice < KJ_N_COUNT ? names[notice] : "?";
}

const char *kj_board_cmd_name(kj_cmd_t cmd)
{
    switch (cmd) {
    case KJ_CMD_START: return "start";
    case KJ_CMD_END: return "end";
    case KJ_CMD_NEW_GAME: return "new";
    case KJ_CMD_RESET: return "reset";
    case KJ_CMD_BOT_ADD: return "bot+";
    case KJ_CMD_BOT_REMOVE: return "bot-";
    case KJ_CMD_KICK: return "kick";
    case KJ_CMD_SYNC: return "sync";
    default: return "";
    }
}

void kj_board_room_text(uint16_t room, char out[5])
{
    snprintf(out, 5, "%04X", (unsigned)room);
}

static size_t finish(int n, size_t cap)
{
    return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
}

size_t kj_board_hello_line(uint16_t room, const char *fw, const uint8_t mac[6], const char *link, char *buf,
                           size_t cap)
{
    char rt[5];
    kj_board_room_text(room, rt);
    static const uint8_t zero[6] = { 0 };
    const uint8_t *m = mac ? mac : zero;
    int n = snprintf(buf, cap,
                     KJ_BOARD_PREFIX "{\"t\":\"hello\",\"app\":\"limited-rps\",\"fw\":\"%s\",\"proto\":%d,"
                     "\"room\":\"%s\",\"mac\":\"%02x%02x%02x%02x%02x%02x\",\"link\":\"%s\","
                     "\"max\":%d,\"cards\":%d,\"stars\":%d}\n",
                     fw ? fw : "", KJ_PROTO_VERSION, rt, m[0], m[1], m[2], m[3], m[4], m[5], link ? link : "",
                     KJ_MAX_PLAYERS, KJ_CARDS_PER_TYPE, KJ_START_STARS);
    return finish(n, cap);
}

size_t kj_board_game_line(const kj_game_t *g, uint16_t room, uint32_t now_ms, char *buf, size_t cap)
{
    kj_summary_t s;
    kj_rules_summary(g, now_ms, &s);
    char rt[5];
    kj_board_room_text(room, rt);
    int n = snprintf(buf, cap,
                     KJ_BOARD_PREFIX "{\"t\":\"g\",\"room\":\"%s\",\"phase\":\"%s\",\"gid\":%u,"
                     "\"seated\":%d,\"online\":%d,\"duels\":%d,\"cleared\":%d,\"out\":%d,\"failed\":%d,"
                     "\"bots\":%d,\"cards\":[%d,%d,%d],\"stars\":%d,\"phase_ms\":%lu,\"ms\":%lu}\n",
                     rt, kj_board_phase_name(g->phase), (unsigned)g->game_id, s.seated, s.online,
                     s.in_duel, s.cleared, s.eliminated, s.failed, s.bots, s.cards[0], s.cards[1],
                     s.cards[2], s.stars, (unsigned long)(now_ms - g->phase_since_ms),
                     (unsigned long)now_ms);
    return finish(n, cap);
}

// 昵称写成 JSON 字符串内容：引号、反斜杠与控制字符转义，其余 UTF-8 原样输出。放不下返回 false。
static bool json_text(char *out, size_t cap, const char *s)
{
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        char tmp[8];
        size_t n;
        if (*p == '"' || *p == '\\') {
            tmp[0] = '\\';
            tmp[1] = (char)*p;
            n = 2;
        } else if (*p < 0x20 || *p == 0x7F) {
            n = (size_t)snprintf(tmp, sizeof(tmp), "\\u%04x", *p);
        } else {
            tmp[0] = (char)*p;
            n = 1;
        }
        if (o + n >= cap) return false;
        memcpy(out + o, tmp, n);
        o += n;
    }
    out[o] = '\0';
    return true;
}

size_t kj_board_player_line(const kj_game_t *g, int idx, uint32_t now_ms, const char *name, char *buf,
                            size_t cap)
{
    if (idx < 0 || idx >= KJ_MAX_PLAYERS) return 0;
    const kj_player_t *p = &g->players[idx];
    if (!p->used) {
        return finish(snprintf(buf, cap, KJ_BOARD_PREFIX "{\"t\":\"p\",\"no\":%d,\"gone\":1}\n",
                               kj_no_of(idx)), cap);
    }
    uint8_t peer_no = p->peer < KJ_MAX_PLAYERS ? kj_no_of(p->peer) : 0;
    const char *final = p->final_reason == KJ_FINAL_NO_CARDS ? "nocards"
                      : p->final_reason == KJ_FINAL_TIME_UP ? "timeup" : "";
    int n = snprintf(buf, cap,
                     KJ_BOARD_PREFIX "{\"t\":\"p\",\"no\":%d,\"bot\":%d,\"on\":%d,\"st\":\"%s\","
                     "\"r\":%u,\"s\":%u,\"p\":%u,\"stars\":%u,\"peer\":%u,\"lock\":\"%s\",\"duel\":%u,"
                     "\"w\":%u,\"l\":%u,\"d\":%u,\"fin\":\"%s\",\"rssi\":%d,\"id\":\"%02x%02x%02x\","
                     "\"mac\":\"%02x%02x%02x%02x%02x%02x\"",
                     kj_no_of(idx), p->is_bot ? 1 : 0, kj_rules_online(g, idx, now_ms) ? 1 : 0,
                     kj_board_status_name(p->status), p->cards[KJ_ROCK], p->cards[KJ_SCISSORS],
                     p->cards[KJ_PAPER], p->stars, peer_no, kj_board_card_name(p->locked),
                     p->status == KJ_ST_DUEL ? p->duel_id : 0, p->wins, p->losses, p->draws, final,
                     p->is_bot ? 0 : p->rssi, p->mac[3], p->mac[4], p->mac[5], p->mac[0], p->mac[1], p->mac[2],
                     p->mac[3], p->mac[4], p->mac[5]);
    if (n < 0 || (size_t)n >= cap) return 0;
    char text[KJ_NAME_MAX * 6 + 1];   // 最坏情况每个字节都要转义成 \u00XX
    if (name && json_text(text, sizeof(text), name)) {
        n += snprintf(buf + n, cap - (size_t)n, ",\"name\":\"%s\"}\n", text);
    } else {
        n += snprintf(buf + n, cap - (size_t)n, "}\n");
    }
    return finish(n, cap);
}

size_t kj_board_event_line(const kj_event_t *e, char *buf, size_t cap)
{
    int n = snprintf(buf, cap,
                     KJ_BOARD_PREFIX "{\"t\":\"e\",\"k\":\"%s\",\"a\":%u,\"b\":%u,\"ca\":\"%s\","
                     "\"cb\":\"%s\",\"w\":%u,\"duel\":%u,\"x\":%u,\"ms\":%lu}\n",
                     kj_board_event_name(e->kind), e->a, e->b, kj_board_card_name(e->ca),
                     kj_board_card_name(e->cb), e->winner, e->duel_id, e->aux, (unsigned long)e->t_ms);
    return finish(n, cap);
}

size_t kj_board_ack_line(kj_cmd_t cmd, int arg, kj_notice_t result, char *buf, size_t cap)
{
    int n = snprintf(buf, cap,
                     KJ_BOARD_PREFIX "{\"t\":\"ack\",\"cmd\":\"%s\",\"arg\":%d,\"ok\":%d,\"err\":\"%s\"}\n",
                     kj_board_cmd_name(cmd), arg, result == KJ_N_NONE ? 1 : 0,
                     kj_board_notice_name(result));
    return finish(n, cap);
}

bool kj_board_parse_command(const char *line, kj_cmd_t *cmd, int *arg)
{
    if (!line || !cmd || !arg) return false;
    while (*line == ' ' || *line == '\t') line++;
    size_t plen = strlen(KJ_BOARD_PREFIX);
    if (strncmp(line, KJ_BOARD_PREFIX, plen) != 0) return false;
    line += plen;
    char word[16];
    size_t n = 0;
    while (line[n] && !isspace((unsigned char)line[n]) && n < sizeof(word) - 1) {
        word[n] = line[n];
        n++;
    }
    word[n] = '\0';
    const char *rest = line + n;
    if (line[n] && !isspace((unsigned char)line[n])) return false;   // 单词过长
    *arg = 0;
    static const struct { const char *name; kj_cmd_t cmd; } table[] = {
        { "start", KJ_CMD_START }, { "end", KJ_CMD_END }, { "new", KJ_CMD_NEW_GAME },
        { "reset", KJ_CMD_RESET }, { "bot+", KJ_CMD_BOT_ADD }, { "bot-", KJ_CMD_BOT_REMOVE },
        { "sync", KJ_CMD_SYNC }, { "kick", KJ_CMD_KICK },
    };
    kj_cmd_t found = KJ_CMD_NONE;
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcmp(word, table[i].name) == 0) found = table[i].cmd;
    }
    if (found == KJ_CMD_NONE) return false;
    while (*rest == ' ' || *rest == '\t') rest++;
    if (found == KJ_CMD_KICK) {
        char *end = NULL;
        long v = strtol(rest, &end, 10);
        if (end == rest || v < 1 || v > KJ_MAX_PLAYERS) return false;
        rest = end;
        *arg = (int)v;
        while (*rest == ' ' || *rest == '\t') rest++;
    }
    while (*rest == '\r' || *rest == '\n' || *rest == ' ' || *rest == '\t') rest++;
    if (*rest != '\0') return false;
    *cmd = found;
    return true;
}
