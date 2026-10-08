// main/kj_rules.c —— 限定猜拳裁判规则引擎（纯 C，主机测试见 tests/test_kj_rules.c）。
#include "kj_rules.h"
#include "kj_bump.h"

#include <string.h>

#define PEER_NONE 0xFF

static bool idx_ok(const kj_game_t *g, int idx)
{
    return idx >= 0 && idx < KJ_MAX_PLAYERS && g->players[idx].used;
}

static void push_event(kj_game_t *g, uint8_t kind, uint8_t a, uint8_t b, uint32_t now_ms)
{
    uint8_t slot;
    if (g->ev_count < KJ_EVENT_RING) {
        slot = (uint8_t)((g->ev_head + g->ev_count) % KJ_EVENT_RING);
        g->ev_count++;
    } else {
        // 满了丢最旧的一条：看板日志不是权威状态，选手行会单独全量刷新。
        slot = g->ev_head;
        g->ev_head = (uint8_t)((g->ev_head + 1) % KJ_EVENT_RING);
    }
    kj_event_t *e = &g->events[slot];
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    e->a = a;
    e->b = b;
    e->ca = KJ_CARD_NONE;
    e->cb = KJ_CARD_NONE;
    e->t_ms = now_ms;
}

static kj_event_t *last_event(kj_game_t *g)
{
    if (g->ev_count == 0) return NULL;
    return &g->events[(g->ev_head + g->ev_count - 1) % KJ_EVENT_RING];
}

void kj_rules_touch(kj_game_t *g, int idx)
{
    if (idx < 0 || idx >= KJ_MAX_PLAYERS) return;
    g->rev++;
    g->players[idx].view_ver++;
    g->board_dirty[idx / 32] |= 1u << (idx % 32);
}

static void touch_all(kj_game_t *g)
{
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
        if (g->players[i].used) kj_rules_touch(g, i);
    }
    g->phase_ver++;
}

void kj_rules_notify(kj_game_t *g, int idx, kj_notice_t n)
{
    if (!idx_ok(g, idx)) return;
    g->players[idx].notice = (uint8_t)n;
    g->players[idx].notice_seq++;
    kj_rules_touch(g, idx);
}

static void set_status(kj_game_t *g, int idx, uint8_t status, uint32_t now_ms)
{
    kj_player_t *p = &g->players[idx];
    if (p->status != status) g->phase_ver++;   // 空闲名单可能变化
    p->status = status;
    p->since_ms = now_ms;
    kj_rules_touch(g, idx);
}

static void deal(kj_player_t *p)
{
    for (int c = 0; c < KJ_CARD_TYPES; c++) p->cards[c] = KJ_CARDS_PER_TYPE;
    p->stars = KJ_START_STARS;
    p->wins = p->losses = p->draws = 0;
    p->peer = PEER_NONE;
    p->locked = KJ_CARD_NONE;
    p->final_reason = KJ_FINAL_NONE;
    p->res_duel_id = 0;
    p->res_my = p->res_opp = KJ_CARD_NONE;
    p->res_outcome = KJ_OUT_NONE;
    p->res_opp_no = KJ_NO_NONE;
}

static void clear_hand(kj_player_t *p)
{
    memset(p->cards, 0, sizeof(p->cards));
    p->stars = 0;
    p->wins = p->losses = p->draws = 0;
    p->peer = PEER_NONE;
    p->locked = KJ_CARD_NONE;
    p->final_reason = KJ_FINAL_NONE;
    p->res_duel_id = 0;
    p->res_my = p->res_opp = KJ_CARD_NONE;
    p->res_outcome = KJ_OUT_NONE;
    p->res_opp_no = KJ_NO_NONE;
}

void kj_rules_init(kj_game_t *g)
{
    memset(g, 0, sizeof(*g));
    g->phase = KJ_PHASE_LOBBY;
    g->game_id = 1;
    g->next_duel_id = 1;
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
        g->players[i].peer = PEER_NONE;
        g->players[i].locked = KJ_CARD_NONE;
    }
}

const kj_player_t *kj_rules_player_by_no(const kj_game_t *g, uint8_t no)
{
    int idx = kj_idx_of(no);
    return idx_ok(g, idx) ? &g->players[idx] : NULL;
}

int kj_rules_find_mac(const kj_game_t *g, const uint8_t mac[6])
{
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
        const kj_player_t *p = &g->players[i];
        if (p->used && !p->is_bot && memcmp(p->mac, mac, 6) == 0) return i;
    }
    return -1;
}

int kj_rules_player_count(const kj_game_t *g)
{
    int n = 0;
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) n += g->players[i].used ? 1 : 0;
    return n;
}

uint8_t kj_rules_total_cards(const kj_player_t *p)
{
    return (uint8_t)(p->cards[0] + p->cards[1] + p->cards[2]);
}

bool kj_rules_is_final(uint8_t status)
{
    return status == KJ_ST_CLEARED || status == KJ_ST_ELIMINATED || status == KJ_ST_FAILED;
}

bool kj_rules_online(const kj_game_t *g, int idx, uint32_t now_ms)
{
    if (!idx_ok(g, idx)) return false;
    const kj_player_t *p = &g->players[idx];
    if (p->is_bot) return true;
    return (uint32_t)(now_ms - p->last_seen_ms) < KJ_ONLINE_TIMEOUT_MS;
}

int kj_rules_compare(uint8_t a, uint8_t b)
{
    if (a == b) return -1;
    // 石头胜剪刀、剪刀胜布、布胜石头：(a + 1) % 3 == b 时 a 胜。
    return ((a + 1) % KJ_CARD_TYPES == b) ? 0 : 1;
}

bool kj_rules_available(const kj_game_t *g, int idx, uint32_t now_ms)
{
    return g->phase == KJ_PHASE_RUNNING && idx_ok(g, idx) &&
           g->players[idx].status == KJ_ST_IDLE && kj_rules_online(g, idx, now_ms);
}

static int free_slot(const kj_game_t *g)
{
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
        if (!g->players[i].used) return i;
    }
    return -1;
}

static void init_slot(kj_game_t *g, int idx, uint32_t now_ms)
{
    kj_player_t *p = &g->players[idx];
    uint16_t keep_ver = p->view_ver;   // 版本单调递增，复用槽位时不回退
    memset(p, 0, sizeof(*p));
    p->used = 1;
    p->view_ver = keep_ver;
    p->peer = PEER_NONE;
    p->locked = KJ_CARD_NONE;
    p->last_seen_ms = now_ms;
    p->since_ms = now_ms;
    p->online = 1;
    if (g->phase == KJ_PHASE_RUNNING) {
        deal(p);
        p->status = KJ_ST_IDLE;
    } else {
        clear_hand(p);
        p->status = KJ_ST_WAITING;
    }
    g->phase_ver++;
    kj_rules_touch(g, idx);
}

int kj_rules_join(kj_game_t *g, const uint8_t mac[6], uint32_t now_ms)
{
    int idx = kj_rules_find_mac(g, mac);
    if (idx >= 0) {
        g->players[idx].last_seen_ms = now_ms;
        kj_rules_touch(g, idx);
        push_event(g, KJ_EV_REJOIN, kj_no_of(idx), 0, now_ms);
        return idx;
    }
    idx = free_slot(g);
    if (idx < 0) return -1;
    init_slot(g, idx, now_ms);
    memcpy(g->players[idx].mac, mac, 6);
    push_event(g, KJ_EV_JOIN, kj_no_of(idx), 0, now_ms);
    return idx;
}

int kj_rules_add_bot(kj_game_t *g, uint32_t now_ms)
{
    int idx = free_slot(g);
    if (idx < 0) return -1;
    init_slot(g, idx, now_ms);
    g->players[idx].is_bot = 1;
    push_event(g, KJ_EV_BOT_ADD, kj_no_of(idx), 0, now_ms);
    return idx;
}

// 与另一位选手绑定在一起的状态（挑战、应战、对决、碰拳配对）。
static bool engaged(uint8_t status)
{
    return status == KJ_ST_CHALLENGING || status == KJ_ST_CHALLENGED || status == KJ_ST_DUEL ||
           status == KJ_ST_MATCHED;
}

// 取消 idx 当前的碰拳 / 挑战 / 对决（不消耗手牌），双方回到空闲并通知对方。
static void abort_engagement(kj_game_t *g, int idx, kj_notice_t peer_notice, uint32_t now_ms)
{
    kj_player_t *p = &g->players[idx];
    uint8_t st = p->status;
    if (st == KJ_ST_BUMPING) {   // 还没配上对手，只有自己
        set_status(g, idx, KJ_ST_IDLE, now_ms);
        return;
    }
    if (!engaged(st)) return;
    int peer = p->peer;
    p->peer = PEER_NONE;
    p->locked = KJ_CARD_NONE;
    set_status(g, idx, g->phase == KJ_PHASE_RUNNING ? KJ_ST_IDLE : p->status, now_ms);
    if (idx_ok(g, peer) && g->players[peer].peer == idx) {
        kj_player_t *q = &g->players[peer];
        q->peer = PEER_NONE;
        q->locked = KJ_CARD_NONE;
        set_status(g, peer, KJ_ST_IDLE, now_ms);
        if (peer_notice != KJ_N_NONE) kj_rules_notify(g, peer, peer_notice);
    }
}

kj_notice_t kj_rules_remove(kj_game_t *g, int idx, uint32_t now_ms)
{
    if (!idx_ok(g, idx)) return KJ_N_INVALID;
    abort_engagement(g, idx, KJ_N_ABORTED, now_ms);
    kj_rules_touch(g, idx);
    uint16_t ver = g->players[idx].view_ver;
    memset(&g->players[idx], 0, sizeof(kj_player_t));
    g->players[idx].view_ver = ver;
    g->players[idx].peer = PEER_NONE;
    g->players[idx].locked = KJ_CARD_NONE;
    g->phase_ver++;
    push_event(g, KJ_EV_REMOVE, kj_no_of(idx), 0, now_ms);
    return KJ_N_NONE;
}

kj_notice_t kj_rules_remove_bot(kj_game_t *g, uint32_t now_ms)
{
    // 优先移除编号最大的、没有卷入挑战 / 对决的电脑选手。
    for (int i = KJ_MAX_PLAYERS - 1; i >= 0; i--) {
        const kj_player_t *p = &g->players[i];
        if (!p->used || !p->is_bot || engaged(p->status)) continue;
        return kj_rules_remove(g, i, now_ms);
    }
    return KJ_N_BUSY;
}

kj_notice_t kj_rules_start(kj_game_t *g, uint32_t now_ms)
{
    if (g->phase != KJ_PHASE_LOBBY) return KJ_N_INVALID;
    if (kj_rules_player_count(g) < 2) return KJ_N_INVALID;
    g->phase = KJ_PHASE_RUNNING;
    g->phase_since_ms = now_ms;
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
        kj_player_t *p = &g->players[i];
        if (!p->used) continue;
        deal(p);
        p->status = KJ_ST_IDLE;
        p->since_ms = now_ms;
    }
    touch_all(g);
    push_event(g, KJ_EV_START, 0, 0, now_ms);
    return KJ_N_NONE;
}

kj_notice_t kj_rules_end(kj_game_t *g, uint32_t now_ms)
{
    if (g->phase != KJ_PHASE_RUNNING) return KJ_N_INVALID;
    // 先撤销所有未结算的挑战 / 对决（已出的暗牌退回手中）。
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
        if (g->players[i].used) abort_engagement(g, i, KJ_N_NONE, now_ms);
    }
    g->phase = KJ_PHASE_ENDED;
    g->phase_since_ms = now_ms;
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
        kj_player_t *p = &g->players[i];
        if (!p->used || kj_rules_is_final(p->status)) continue;
        if (p->status == KJ_ST_WAITING) continue;   // 理论上不会出现
        // 时间到：仍有手牌即失败。手牌已空的情况已在结算时定局。
        p->final_reason = KJ_FINAL_TIME_UP;
        p->status = KJ_ST_FAILED;
        p->since_ms = now_ms;
        push_event(g, KJ_EV_FAILED, kj_no_of(i), 0, now_ms);
    }
    touch_all(g);
    push_event(g, KJ_EV_END, 0, 0, now_ms);
    return KJ_N_NONE;
}

void kj_rules_new_game(kj_game_t *g, uint32_t now_ms)
{
    g->phase = KJ_PHASE_LOBBY;
    g->phase_since_ms = now_ms;
    g->game_id++;
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
        kj_player_t *p = &g->players[i];
        if (!p->used) continue;
        clear_hand(p);
        p->status = KJ_ST_WAITING;
        p->since_ms = now_ms;
    }
    touch_all(g);
    push_event(g, KJ_EV_NEW_GAME, 0, 0, now_ms);
}

void kj_rules_reset(kj_game_t *g, uint32_t now_ms)
{
    uint16_t game_id = g->game_id;
    uint16_t phase_ver = g->phase_ver;
    uint32_t rev = g->rev;
    uint16_t vers[KJ_MAX_PLAYERS];
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) vers[i] = g->players[i].view_ver;
    kj_rules_init(g);
    g->game_id = (uint16_t)(game_id + 1);
    g->phase_ver = (uint16_t)(phase_ver + 1);
    g->rev = rev + 1;
    g->phase_since_ms = now_ms;
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) g->players[i].view_ver = (uint16_t)(vers[i] + 1);
    kj_rules_mark_all_dirty(g);
    push_event(g, KJ_EV_RESET, 0, 0, now_ms);
}

// ---------------------------------------------------------------------------
// 选手操作
// ---------------------------------------------------------------------------

kj_notice_t kj_rules_challenge(kj_game_t *g, int idx, uint8_t target_no, uint32_t now_ms)
{
    if (!idx_ok(g, idx)) return KJ_N_INVALID;
    if (g->phase != KJ_PHASE_RUNNING) return KJ_N_NOT_RUNNING;
    if (g->players[idx].status != KJ_ST_IDLE) return KJ_N_INVALID;
    int t = kj_idx_of(target_no);
    if (t == idx || !kj_rules_available(g, t, now_ms)) return KJ_N_BUSY;
    kj_player_t *a = &g->players[idx];
    kj_player_t *b = &g->players[t];
    a->peer = (uint8_t)t;
    b->peer = (uint8_t)idx;
    set_status(g, idx, KJ_ST_CHALLENGING, now_ms);
    set_status(g, t, KJ_ST_CHALLENGED, now_ms);
    push_event(g, KJ_EV_CHALLENGE, kj_no_of(idx), target_no, now_ms);
    return KJ_N_NONE;
}

kj_notice_t kj_rules_cancel(kj_game_t *g, int idx, uint32_t now_ms)
{
    if (!idx_ok(g, idx)) return KJ_N_INVALID;
    uint8_t st = g->players[idx].status;
    if (st != KJ_ST_CHALLENGING && st != KJ_ST_MATCHED) return KJ_N_INVALID;
    uint8_t peer_no = kj_no_of(g->players[idx].peer);
    bool match = st == KJ_ST_MATCHED;
    abort_engagement(g, idx, match ? KJ_N_MATCH_CANCELLED : KJ_N_CANCELLED, now_ms);
    push_event(g, match ? KJ_EV_MATCH_CANCEL : KJ_EV_CANCEL, kj_no_of(idx), peer_no, now_ms);
    return KJ_N_NONE;
}

// 两人进入对决（应战成功 / 碰拳倒计时结束），返回对决编号。
static uint16_t start_duel(kj_game_t *g, int ia, int ib, uint32_t now_ms)
{
    uint16_t duel = g->next_duel_id++;
    if (g->next_duel_id == 0) g->next_duel_id = 1;
    kj_player_t *a = &g->players[ia];
    kj_player_t *b = &g->players[ib];
    a->duel_id = b->duel_id = duel;
    a->locked = b->locked = KJ_CARD_NONE;
    set_status(g, ia, KJ_ST_DUEL, now_ms);
    set_status(g, ib, KJ_ST_DUEL, now_ms);
    return duel;
}

kj_notice_t kj_rules_respond(kj_game_t *g, int idx, bool accept, uint32_t now_ms)
{
    if (!idx_ok(g, idx) || g->players[idx].status != KJ_ST_CHALLENGED) return KJ_N_INVALID;
    int peer = g->players[idx].peer;
    if (!idx_ok(g, peer) || g->players[peer].peer != idx ||
        g->players[peer].status != KJ_ST_CHALLENGING) {
        abort_engagement(g, idx, KJ_N_NONE, now_ms);
        return KJ_N_INVALID;
    }
    if (!accept) {
        abort_engagement(g, idx, KJ_N_DECLINED, now_ms);
        push_event(g, KJ_EV_DECLINE, kj_no_of(idx), kj_no_of(peer), now_ms);
        return KJ_N_NONE;
    }
    uint16_t duel = start_duel(g, peer, idx, now_ms);   // peer 是挑战者
    push_event(g, KJ_EV_ACCEPT, kj_no_of(peer), kj_no_of(idx), now_ms);
    kj_event_t *e = last_event(g);
    if (e) e->duel_id = duel;
    return KJ_N_NONE;
}

kj_notice_t kj_rules_bump(kj_game_t *g, int idx, uint32_t press_ms, uint32_t now_ms)
{
    if (!idx_ok(g, idx) || g->players[idx].is_bot) return KJ_N_INVALID;
    if (g->phase != KJ_PHASE_RUNNING) return KJ_N_NOT_RUNNING;
    if (g->players[idx].status != KJ_ST_IDLE) return KJ_N_INVALID;
    // 在路上太久的请求：同伴那边早已结算，按"没碰到"处理。
    if ((uint32_t)(now_ms - press_ms) > KJ_BUMP_MAX_AGE_MS) return KJ_N_BUMP_ALONE;
    set_status(g, idx, KJ_ST_BUMPING, now_ms);
    g->players[idx].since_ms = press_ms;   // BUMPING 期间 since_ms 记录按下时刻
    return KJ_N_NONE;
}

static void bump_fail(kj_game_t *g, int idx, kj_notice_t why, uint16_t reason, uint32_t now_ms)
{
    set_status(g, idx, KJ_ST_IDLE, now_ms);
    kj_rules_notify(g, idx, why);
    push_event(g, KJ_EV_BUMP_FAIL, kj_no_of(idx), 0, now_ms);
    kj_event_t *e = last_event(g);
    if (e) e->aux = reason;
}

static void settle_bumps(kj_game_t *g, uint32_t now_ms)
{
    kj_bump_cand_t cand[KJ_BUMP_MAX];
    int n = 0;
    for (int i = 0; i < KJ_MAX_PLAYERS && n < KJ_BUMP_MAX; i++) {
        const kj_player_t *p = &g->players[i];
        if (p->used && p->status == KJ_ST_BUMPING) {
            cand[n].idx = (uint8_t)i;
            cand[n].t = p->since_ms;
            n++;
        }
    }
    if (n == 0) return;
    kj_bump_out_t res[KJ_BUMP_MAX];
    kj_bump_resolve(cand, n, now_ms, res);
    for (int k = 0; k < n; k++) {
        int i = cand[k].idx;
        switch (res[k].verdict) {
        case KJ_BUMP_PAIR: {
            int j = res[k].peer;
            if (i > j) break;   // 每一对只处理一次
            g->players[i].peer = (uint8_t)j;
            g->players[j].peer = (uint8_t)i;
            set_status(g, i, KJ_ST_MATCHED, now_ms);
            set_status(g, j, KJ_ST_MATCHED, now_ms);
            push_event(g, KJ_EV_MATCH, kj_no_of(i), kj_no_of(j), now_ms);
            kj_event_t *e = last_event(g);
            if (e) e->aux = res[k].dt;
            break;
        }
        case KJ_BUMP_ALONE: bump_fail(g, i, KJ_N_BUMP_ALONE, KJ_BUMP_FAIL_ALONE, now_ms); break;
        case KJ_BUMP_CROWD: bump_fail(g, i, KJ_N_BUMP_CROWD, KJ_BUMP_FAIL_CROWD, now_ms); break;
        default: break;
        }
    }
}

// 手牌 / 星星变化后判定是否定局。
static void finalize(kj_game_t *g, int idx, uint32_t now_ms)
{
    kj_player_t *p = &g->players[idx];
    if (p->stars == 0) {
        p->status = KJ_ST_ELIMINATED;
        p->since_ms = now_ms;
        push_event(g, KJ_EV_ELIMINATED, kj_no_of(idx), 0, now_ms);
    } else if (kj_rules_total_cards(p) == 0) {
        if (p->stars >= KJ_CLEAR_STARS) {
            p->status = KJ_ST_CLEARED;
            push_event(g, KJ_EV_CLEARED, kj_no_of(idx), 0, now_ms);
        } else {
            p->status = KJ_ST_FAILED;
            p->final_reason = KJ_FINAL_NO_CARDS;
            push_event(g, KJ_EV_FAILED, kj_no_of(idx), 0, now_ms);
        }
        p->since_ms = now_ms;
    }
    g->phase_ver++;
    kj_rules_touch(g, idx);
}

static void resolve(kj_game_t *g, int ia, int ib, uint32_t now_ms)
{
    kj_player_t *a = &g->players[ia];
    kj_player_t *b = &g->players[ib];
    uint8_t ca = a->locked, cb = b->locked;
    a->cards[ca]--;
    b->cards[cb]--;
    int cmp = kj_rules_compare(ca, cb);
    uint8_t winner = KJ_NO_NONE;
    if (cmp == 0) {
        a->stars++;
        b->stars--;
        a->wins++;
        b->losses++;
        a->res_outcome = KJ_OUT_WIN;
        b->res_outcome = KJ_OUT_LOSE;
        winner = kj_no_of(ia);
    } else if (cmp == 1) {
        b->stars++;
        a->stars--;
        b->wins++;
        a->losses++;
        a->res_outcome = KJ_OUT_LOSE;
        b->res_outcome = KJ_OUT_WIN;
        winner = kj_no_of(ib);
    } else {
        a->draws++;
        b->draws++;
        a->res_outcome = b->res_outcome = KJ_OUT_DRAW;
    }
    a->res_duel_id = b->res_duel_id = a->duel_id;
    a->res_my = ca;
    a->res_opp = cb;
    a->res_opp_no = kj_no_of(ib);
    b->res_my = cb;
    b->res_opp = ca;
    b->res_opp_no = kj_no_of(ia);

    push_event(g, KJ_EV_RESULT, kj_no_of(ia), kj_no_of(ib), now_ms);
    kj_event_t *e = last_event(g);
    if (e) {
        e->ca = ca;
        e->cb = cb;
        e->winner = winner;
        e->duel_id = a->duel_id;
    }

    a->peer = b->peer = PEER_NONE;
    a->locked = b->locked = KJ_CARD_NONE;
    set_status(g, ia, KJ_ST_IDLE, now_ms);
    set_status(g, ib, KJ_ST_IDLE, now_ms);
    finalize(g, ia, now_ms);
    finalize(g, ib, now_ms);
}

kj_notice_t kj_rules_play(kj_game_t *g, int idx, uint8_t card, uint32_t now_ms)
{
    if (!idx_ok(g, idx) || g->players[idx].status != KJ_ST_DUEL) return KJ_N_INVALID;
    kj_player_t *p = &g->players[idx];
    if (card >= KJ_CARD_TYPES) return KJ_N_INVALID;
    if (p->locked != KJ_CARD_NONE) return KJ_N_INVALID;
    if (p->cards[card] == 0) return KJ_N_NO_CARD;
    int peer = p->peer;
    if (!idx_ok(g, peer) || g->players[peer].peer != idx || g->players[peer].status != KJ_ST_DUEL) {
        abort_engagement(g, idx, KJ_N_NONE, now_ms);
        return KJ_N_INVALID;
    }
    p->locked = card;
    kj_rules_touch(g, idx);
    kj_rules_touch(g, peer);   // 对方视图里的 "对方已出牌"
    push_event(g, KJ_EV_LOCK, kj_no_of(idx), kj_no_of(peer), now_ms);
    kj_event_t *e = last_event(g);
    if (e) {
        e->ca = card;
        e->duel_id = p->duel_id;
    }
    if (g->players[peer].locked != KJ_CARD_NONE) resolve(g, peer, idx, now_ms);
    return KJ_N_NONE;
}

kj_notice_t kj_rules_withdraw(kj_game_t *g, int idx, uint32_t now_ms)
{
    if (!idx_ok(g, idx) || g->players[idx].status != KJ_ST_DUEL) return KJ_N_INVALID;
    if (g->players[idx].locked != KJ_CARD_NONE) return KJ_N_INVALID;   // 出了牌就不能反悔
    uint8_t peer_no = kj_no_of(g->players[idx].peer);
    abort_engagement(g, idx, KJ_N_WITHDRAWN, now_ms);
    push_event(g, KJ_EV_WITHDRAW, kj_no_of(idx), peer_no, now_ms);
    return KJ_N_NONE;
}

void kj_rules_seen(kj_game_t *g, int idx, int8_t rssi, uint32_t now_ms)
{
    if (!idx_ok(g, idx)) return;
    kj_player_t *p = &g->players[idx];
    p->last_seen_ms = now_ms;
    p->rssi = rssi;
    if (!p->online) {
        p->online = 1;
        g->phase_ver++;                 // 空闲名单变化
        g->board_dirty[idx / 32] |= 1u << (idx % 32);
    }
}

void kj_rules_tick(kj_game_t *g, uint32_t now_ms)
{
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
        kj_player_t *p = &g->players[i];
        if (!p->used) continue;
        // 在线状态翻转只影响看板与空闲名单，不改变选手视图。
        bool online = kj_rules_online(g, i, now_ms);
        if (online != (p->online != 0)) {
            p->online = online ? 1 : 0;
            g->phase_ver++;
            g->board_dirty[i / 32] |= 1u << (i % 32);
        }
        if (p->status == KJ_ST_CHALLENGED &&
            (uint32_t)(now_ms - p->since_ms) >= KJ_CHALLENGE_TIMEOUT_MS) {
            int peer = p->peer;
            abort_engagement(g, i, KJ_N_TIMEOUT, now_ms);
            kj_rules_notify(g, i, KJ_N_TIMEOUT);
            push_event(g, KJ_EV_TIMEOUT, idx_ok(g, peer) ? kj_no_of(peer) : 0, kj_no_of(i), now_ms);
            continue;
        }
        if (p->status == KJ_ST_MATCHED && (uint32_t)(now_ms - p->since_ms) >= KJ_MATCH_COUNTDOWN_MS) {
            // 倒计时结束：自动开打（双方的 since_ms 相同，先遇到的一方负责）。
            int peer = p->peer;
            if (idx_ok(g, peer) && g->players[peer].peer == i && g->players[peer].status == KJ_ST_MATCHED) {
                start_duel(g, i, peer, now_ms);
            } else {
                abort_engagement(g, i, KJ_N_NONE, now_ms);
            }
            continue;
        }
        if (engaged(p->status) && !online && !p->is_bot &&
            (uint32_t)(now_ms - p->last_seen_ms) >= KJ_DUEL_OFFLINE_ABORT_MS) {
            int peer = p->peer;
            abort_engagement(g, i, KJ_N_ABORTED, now_ms);
            push_event(g, KJ_EV_ABORT, kj_no_of(i), idx_ok(g, peer) ? kj_no_of(peer) : 0, now_ms);
        }
    }
    settle_bumps(g, now_ms);
}

void kj_rules_view(const kj_game_t *g, int idx, uint32_t now_ms, kj_view_t *out)
{
    memset(out, 0, sizeof(*out));
    out->phase = g->phase;
    out->game_id = g->game_id;
    out->my_lock = KJ_CARD_NONE;
    out->res_my = out->res_opp = KJ_CARD_NONE;
    if (!idx_ok(g, idx)) {
        out->no = KJ_NO_NONE;
        if (idx >= 0 && idx < KJ_MAX_PLAYERS) out->view_ver = g->players[idx].view_ver;
        return;
    }
    const kj_player_t *p = &g->players[idx];
    out->view_ver = p->view_ver;
    out->ack_seq = p->last_req_seq;
    out->no = kj_no_of(idx);
    out->status = p->status;
    memcpy(out->cards, p->cards, sizeof(out->cards));
    out->stars = p->stars;
    out->duel_id = p->duel_id;
    out->my_lock = p->locked;
    out->notice = p->notice;
    out->notice_seq = p->notice_seq;
    out->res_duel_id = p->res_duel_id;
    out->res_my = p->res_my;
    out->res_opp = p->res_opp;
    out->res_outcome = p->res_outcome;
    out->res_opp_no = p->res_opp_no;
    out->final_reason = p->final_reason;
    out->wins = p->wins;
    out->losses = p->losses;
    out->draws = p->draws;
    if (idx_ok(g, p->peer)) {
        const kj_player_t *q = &g->players[p->peer];
        out->peer_no = kj_no_of(p->peer);
        out->peer_is_bot = q->is_bot;
        out->peer_locked = (p->status == KJ_ST_DUEL && q->locked != KJ_CARD_NONE) ? 1 : 0;
    }
    uint32_t limit = 0;
    if (p->status == KJ_ST_CHALLENGING || p->status == KJ_ST_CHALLENGED) limit = KJ_CHALLENGE_TIMEOUT_MS;
    if (p->status == KJ_ST_MATCHED) limit = KJ_MATCH_COUNTDOWN_MS;
    if (limit) {
        uint32_t el = now_ms - p->since_ms;
        out->deadline_s = (uint8_t)(el >= limit ? 0 : (limit - el + 999) / 1000);
    }
}

bool kj_rules_pop_event(kj_game_t *g, kj_event_t *out)
{
    if (g->ev_count == 0) return false;
    *out = g->events[g->ev_head];
    g->ev_head = (uint8_t)((g->ev_head + 1) % KJ_EVENT_RING);
    g->ev_count--;
    return true;
}

bool kj_rules_take_dirty(kj_game_t *g, int idx)
{
    uint32_t bit = 1u << (idx % 32);
    if (!(g->board_dirty[idx / 32] & bit)) return false;
    g->board_dirty[idx / 32] &= ~bit;
    return true;
}

void kj_rules_mark_all_dirty(kj_game_t *g)
{
    for (int i = 0; i < KJ_MAX_PLAYERS / 32; i++) g->board_dirty[i] = 0xFFFFFFFFu;
}

void kj_rules_mark_dirty(kj_game_t *g, int idx)
{
    if (idx < 0 || idx >= KJ_MAX_PLAYERS) return;
    g->board_dirty[idx / 32] |= 1u << (idx % 32);
}

void kj_rules_summary(const kj_game_t *g, uint32_t now_ms, kj_summary_t *out)
{
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
        const kj_player_t *p = &g->players[i];
        if (!p->used) continue;
        out->seated++;
        if (p->is_bot) out->bots++;
        if (kj_rules_online(g, i, now_ms)) out->online++;
        if (p->status == KJ_ST_DUEL) out->in_duel++;
        if (p->status == KJ_ST_CLEARED) out->cleared++;
        if (p->status == KJ_ST_ELIMINATED) out->eliminated++;
        if (p->status == KJ_ST_FAILED) out->failed++;
        out->stars += p->stars;
        if (!kj_rules_is_final(p->status)) {
            for (int c = 0; c < KJ_CARD_TYPES; c++) out->cards[c] += p->cards[c];
        }
    }
    out->in_duel /= 2;   // 每场对决两人
}
