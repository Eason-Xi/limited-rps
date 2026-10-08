// tests/test_kj_sim.c —— 多设备联机仿真：1 台庄家 + 多台选手 + 电脑选手，经过有丢包的模拟信道（直连，不经 hub）
// 打完整局。检查：星星守恒、手牌只减不增且与结算次数一致、挑战 / 对决 / 碰拳配对关系对称、
// 碰拳中的人很快得到结果、选手重启 / 庄家重启（NVS 恢复）后能续上、信道恢复后所有选手视图与庄家一致、
// 直连模式的昵称广播（与固件一样在收发两端截下 NAME 帧）最终让每台设备都知道其他人的昵称。
#include "kj_bump.h"
#include "kj_client.h"
#include "kj_names.h"
#include "kj_persist.h"
#include "kj_server.h"
#include "kj_test.h"

#include <string.h>

#define HUMANS 12
#define BOTS 3
#define STEP_MS 10
#define ROOM_ID 0x4B4A

typedef struct {
    kj_client_t c;
    kj_names_t names;
    char name[KJ_NAME_MAX + 1];
    uint8_t mac[6];
    uint32_t next_action_ms;
    uint32_t bump_at;        // 约好碰拳的时刻（0 = 没有）
    int bump_group;          // 约好一起碰拳的那一组（下标进 groups）
} sim_player_t;

// 约好一起碰拳的一组人：统计"两人都真的按下了"的干净配对有多少成功。
typedef struct {
    int size, sent;
    int member[3];
    bool matched;
} bump_group_t;

#define MAX_GROUPS 512
static bump_group_t groups[MAX_GROUPS];
static int group_count;

static kj_server_t server;
static kj_names_t host_names;
static sim_player_t players[HUMANS];
static const uint8_t host_mac[6] = { 0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC };
static uint32_t now_ms = 1;
static uint32_t rng = 0x12345678u;
static int loss_pct = 25;
static bool actions_enabled = true;
static long delivered, dropped;
static long bump_pairs, bump_sent, matches_seen, bump_fails_seen;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static bool lose(void) { return (int)(rnd() % 100) < loss_pct; }

static void deliver_to_player(int i, const uint8_t *src, const kj_out_t *it)
{
    if (lose()) {
        dropped++;
        return;
    }
    delivered++;
    kj_client_on_frame(&players[i].c, src, it->data, it->len, now_ms);
}

// 昵称广播的丢包用单独的随机数：不打乱其余帧的丢包序列（各个种子下的对局场景保持不变）。
static uint32_t name_rng = 0x9E3779B9u;

static bool lose_name(void)
{
    name_rng ^= name_rng << 13;
    name_rng ^= name_rng >> 17;
    name_rng ^= name_rng << 5;
    bool lost = (int)(name_rng % 100) < loss_pct;
    if (lost) dropped++;
    return lost;
}

// 直连模式的昵称广播：与固件的 names_rx 一样，庄家只收座位登记的那台设备自己报的，选手收同一赌局的。
static void route_name(const kj_out_t *it, const uint8_t *src, int src_idx)
{
    kj_frame_t f;
    if (!kj_proto_decode(it->data, it->len, &f) || f.type != KJ_F_NAME) return;
    if (!lose_name()) {
        const kj_player_t *p = kj_rules_player_by_no(&server.game, f.u.name.no);
        if (f.room == server.room && p && !p->is_bot && memcmp(p->mac, src, 6) == 0) {
            kj_names_store(&host_names, src, f.room, &f.u.name);
        }
    }
    for (int i = 0; i < HUMANS; i++) {
        if (i == src_idx || lose_name()) continue;
        kj_names_store(&players[i].names, src, f.room, &f.u.name);
    }
}

static void route(const kj_outbox_t *o, const uint8_t *src, int src_idx)
{
    for (int k = 0; k < o->count; k++) {
        const kj_out_t *it = &o->items[k];
        if (kj_proto_peek_type(it->data, it->len) == KJ_F_NAME) {
            route_name(it, src, src_idx);
            continue;
        }
        bool to_host = it->broadcast || memcmp(it->mac, host_mac, 6) == 0;
        if (to_host && src_idx >= 0) {
            if (lose()) {
                dropped++;
            } else {
                delivered++;
                kj_outbox_t reply;
                kj_outbox_clear(&reply);
                kj_server_on_frame(&server, src, (int8_t)(-40 - (int)(rnd() % 30)), it->data, it->len, now_ms, &reply);
                route(&reply, host_mac, -1);
            }
        }
        for (int i = 0; i < HUMANS; i++) {
            if (i == src_idx) continue;
            if (it->broadcast || memcmp(it->mac, players[i].mac, 6) == 0) deliver_to_player(i, src, it);
        }
    }
}

static void act(int i)
{
    sim_player_t *sp = &players[i];
    kj_client_t *c = &sp->c;
    const kj_view_t *v = &c->view;
    if (sp->bump_at && actions_enabled && (int32_t)(now_ms - sp->bump_at) >= 0) {
        // 约好了碰拳：到点就长按（之前被别人挑战走了、或还有请求没确认，就作罢）
        sp->bump_at = 0;
#ifdef SIM_DEBUG
        fprintf(stderr, "t=%u player %d (g%d) bump status=%u pending=%d\n", (unsigned)now_ms, i, sp->bump_group,
                v->status, c->pending);
#endif
        if (c->link == KJ_LINK_JOINED && v->phase == KJ_PHASE_RUNNING && v->status == KJ_ST_IDLE &&
            kj_client_request(c, KJ_OP_BUMP, 0, now_ms)) {
            bump_sent++;
            groups[sp->bump_group].sent++;
        }
        return;
    }
    if ((int32_t)(now_ms - sp->next_action_ms) < 0) return;
    sp->next_action_ms = now_ms + 200 + rnd() % 1500;
    if (c->link == KJ_LINK_IDLE) {
        kj_room_entry_t rooms[KJ_CLIENT_MAX_ROOMS];
        if (kj_client_rooms(c, now_ms, rooms, KJ_CLIENT_MAX_ROOMS) > 0) kj_client_join(c, rooms[0].room, now_ms);
        return;
    }
    if (!actions_enabled || c->link != KJ_LINK_JOINED || c->pending || sp->bump_at) return;
    if (v->phase != KJ_PHASE_RUNNING) return;
    uint32_t r = rnd() % 100;
    switch (v->status) {
    case KJ_ST_MATCHED:
        if (r < 4) kj_client_request(c, KJ_OP_CANCEL, 0, now_ms);   // 偶尔认错人
        break;
    case KJ_ST_IDLE: {
        kj_opponent_t opp[8];
        int n = kj_client_opponents(c, opp, 8);
        if (n > 0 && r < 60) kj_client_request(c, KJ_OP_CHALLENGE, opp[rnd() % (uint32_t)n].no, now_ms);
        break;
    }
    case KJ_ST_CHALLENGED:
        kj_client_request(c, r < 85 ? KJ_OP_ACCEPT : KJ_OP_DECLINE, 0, now_ms);
        break;
    case KJ_ST_CHALLENGING:
        if (r < 3) kj_client_request(c, KJ_OP_CANCEL, 0, now_ms);
        break;
    case KJ_ST_DUEL:
        if (v->my_lock == KJ_CARD_NONE) {
            if (r < 3) {
                kj_client_request(c, KJ_OP_WITHDRAW, 0, now_ms);
                break;
            }
            uint8_t pick[3];
            int n = 0;
            for (uint8_t k = 0; k < KJ_CARD_TYPES; k++) {
                if (v->cards[k]) pick[n++] = k;
            }
            if (n) kj_client_request(c, KJ_OP_PLAY, pick[rnd() % (uint32_t)n], now_ms);
        }
        break;
    default:
        break;
    }
}

static long results_seen;

static bool free_for_bump(int i)
{
    const sim_player_t *sp = &players[i];
    return sp->bump_at == 0 && sp->c.link == KJ_LINK_JOINED && !sp->c.pending &&
           sp->c.view.phase == KJ_PHASE_RUNNING && sp->c.view.status == KJ_ST_IDLE;
}

// 每 1.5 s 安排一次碰拳：多数是两人一组（按下时刻相差 0~240 ms），也有单人和三人同时。
static void schedule_bumps(void)
{
    if (!actions_enabled || now_ms % 1500 >= STEP_MS) return;
    uint32_t r = rnd() % 100;
    int want = r < 50 ? 2 : r < 60 ? 3 : r < 70 ? 1 : 0;
    if (want == 0 || group_count >= MAX_GROUPS) return;
    bump_group_t *grp = &groups[group_count];
    memset(grp, 0, sizeof(*grp));
    for (int tries = 0; tries < 40 && grp->size < want; tries++) {
        int i = (int)(rnd() % HUMANS);
        if (!free_for_bump(i)) continue;
        players[i].bump_at = now_ms + 1 + rnd() % 240;
        players[i].bump_group = group_count;
        grp->member[grp->size++] = i;
    }
    if (grp->size == 0) return;
    if (grp->size == 2 && want == 2) bump_pairs++;
    group_count++;
}

static int sim_index_of_no(uint8_t no)
{
    const kj_player_t *p = kj_rules_player_by_no(&server.game, no);
    if (!p || p->is_bot) return -1;
    for (int i = 0; i < HUMANS; i++) {
        if (memcmp(players[i].mac, p->mac, 6) == 0) return i;
    }
    return -1;
}

static void note_match(uint8_t a_no, uint8_t b_no)
{
    int a = sim_index_of_no(a_no), b = sim_index_of_no(b_no);
    if (a < 0 || b < 0 || players[a].bump_group != players[b].bump_group) return;
    groups[players[a].bump_group].matched = true;
}

static void step(void)
{
    kj_outbox_t o;
    kj_outbox_clear(&o);
    schedule_bumps();
    kj_server_tick(&server, now_ms, &o);
    route(&o, host_mac, -1);
    for (int i = 0; i < HUMANS; i++) {
        act(i);
        kj_outbox_clear(&o);
        kj_client_tick(&players[i].c, now_ms, &o);
        bool seated = players[i].c.link == KJ_LINK_JOINED;
        kj_names_tick(&players[i].names, seated ? players[i].c.room : 0, seated ? players[i].c.view.no : 0,
                      players[i].mac, players[i].name, now_ms, &o);
        route(&o, players[i].mac, i);
        kj_client_take_view_changed(&players[i].c);
        kj_client_take_req_failed(&players[i].c);
        kj_client_take_kicked(&players[i].c, NULL);
    }
    kj_event_t e;
    while (kj_rules_pop_event(&server.game, &e)) {
        results_seen += e.kind == KJ_EV_RESULT;
        matches_seen += e.kind == KJ_EV_MATCH;
        if (e.kind == KJ_EV_MATCH) note_match(e.a, e.b);
#ifdef SIM_DEBUG
        if (e.kind == KJ_EV_MATCH || e.kind == KJ_EV_BUMP_FAIL || e.kind == KJ_EV_CHALLENGE || e.kind == KJ_EV_MATCH_CANCEL) {
            int ia = sim_index_of_no(e.a), ib = e.b ? sim_index_of_no(e.b) : -1;
            fprintf(stderr, "t=%u ev=%d a=%d(g%d) b=%d(g%d) aux=%u\n", (unsigned)now_ms, e.kind, ia,
                    ia >= 0 ? players[ia].bump_group : -1, ib, ib >= 0 ? players[ib].bump_group : -1, e.aux);
        }
#endif
        bump_fails_seen += e.kind == KJ_EV_BUMP_FAIL;
    }
    now_ms += STEP_MS;
}

static int dealt_players;

static void check_invariants(void)
{
    const kj_game_t *g = &server.game;
    int stars = 0, cards = 0;
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
        const kj_player_t *p = &g->players[i];
        if (!p->used) continue;
        stars += p->stars;
        for (int c = 0; c < KJ_CARD_TYPES; c++) {
            CHECK(p->cards[c] <= KJ_CARDS_PER_TYPE);
            cards += p->cards[c];
        }
        if (p->status == KJ_ST_CHALLENGING || p->status == KJ_ST_CHALLENGED || p->status == KJ_ST_DUEL) {
            CHECK(p->peer < KJ_MAX_PLAYERS);
            if (p->peer < KJ_MAX_PLAYERS) {
                const kj_player_t *q = &g->players[p->peer];
                CHECK(q->used);
                CHECK_EQ(q->peer, i);
                if (p->status == KJ_ST_CHALLENGING) CHECK_EQ(q->status, KJ_ST_CHALLENGED);
                if (p->status == KJ_ST_CHALLENGED) CHECK_EQ(q->status, KJ_ST_CHALLENGING);
                if (p->status == KJ_ST_DUEL) CHECK_EQ(q->status, KJ_ST_DUEL);
            }
        }
        if (p->status == KJ_ST_MATCHED) {
            CHECK(p->peer < KJ_MAX_PLAYERS);
            if (p->peer < KJ_MAX_PLAYERS) {
                CHECK_EQ(g->players[p->peer].peer, i);
                CHECK_EQ(g->players[p->peer].status, KJ_ST_MATCHED);
            }
            CHECK((uint32_t)(now_ms - p->since_ms) <= KJ_MATCH_COUNTDOWN_MS + STEP_MS);
        }
        if (p->status == KJ_ST_BUMPING) {
            CHECK_EQ(p->peer, 0xFF);
            CHECK((uint32_t)(now_ms - p->since_ms) <= KJ_BUMP_MAX_AGE_MS + KJ_BUMP_SETTLE_MS);
        }
        if (p->is_bot) CHECK(p->status != KJ_ST_BUMPING && p->status != KJ_ST_MATCHED);
        if (p->status == KJ_ST_ELIMINATED) CHECK_EQ(p->stars, 0);
        if (p->status == KJ_ST_CLEARED) CHECK(p->stars >= KJ_CLEAR_STARS && kj_rules_total_cards(p) == 0);
    }
    if (g->phase != KJ_PHASE_LOBBY) {
        CHECK_EQ(stars, dealt_players * KJ_START_STARS);
        CHECK_EQ(cards, dealt_players * 12 - 2 * (int)results_seen);
    }
}

static void check_converged(const char *when)
{
    for (int i = 0; i < HUMANS; i++) {
        kj_client_t *c = &players[i].c;
        CHECK_EQ(c->link, KJ_LINK_JOINED);
        int idx = kj_rules_find_mac(&server.game, players[i].mac);
        CHECK(idx >= 0);
        if (idx < 0) continue;
        kj_view_t want;
        kj_rules_view(&server.game, idx, now_ms, &want);
        if (c->view.view_ver != want.view_ver || c->view.status != want.status ||
            memcmp(c->view.cards, want.cards, 3) != 0 || c->view.stars != want.stars) {
            fprintf(stderr, "%s: player %d view ver %u/%u status %u/%u stars %u/%u\n", when, i,
                    c->view.view_ver, want.view_ver, c->view.status, want.status, c->view.stars, want.stars);
            kj_test_failures++;
        }
        CHECK(!c->pending);
    }
}

// 信道干净时，每台选手设备都知道其他在座选手的昵称，庄家（按座位 MAC 校验）也知道每个人的。
static void check_names(const char *when)
{
    for (int j = 0; j < HUMANS; j++) {
        int idx = kj_rules_find_mac(&server.game, players[j].mac);
        if (idx < 0 || players[j].c.link != KJ_LINK_JOINED) continue;
        uint8_t no = kj_no_of(idx);
        const char *name = NULL;
        if (!kj_names_get(&host_names, ROOM_ID, no, players[j].mac, &name) || strcmp(name, players[j].name) != 0) {
            fprintf(stderr, "%s: host lacks name of player %d (no %u)\n", when, j, no);
            kj_test_failures++;
        }
        for (int i = 0; i < HUMANS; i++) {
            if (players[i].c.link != KJ_LINK_JOINED) continue;
            if (!kj_names_get(&players[i].names, ROOM_ID, no, NULL, &name) || strcmp(name, players[j].name) != 0) {
                fprintf(stderr, "%s: player %d lacks name of player %d (no %u)\n", when, i, j, no);
                kj_test_failures++;
            }
        }
    }
}

static void run_ms(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += STEP_MS) {
        step();
        if ((now_ms / STEP_MS) % 50 == 0) check_invariants();
    }
}

static int run_sim(uint32_t seed)
{
    rng = seed;
    now_ms = 1 + seed % 1000;
    loss_pct = 25;
    actions_enabled = true;
    delivered = dropped = results_seen = 0;
    bump_pairs = bump_sent = matches_seen = bump_fails_seen = 0;
    group_count = 0;
    int failures_before = kj_test_failures;
    kj_server_init(&server, ROOM_ID, seed * 7 + 1);
    kj_names_init(&host_names, seed);
    kj_names_set_room(&host_names, ROOM_ID);
    name_rng = seed | 1u;
    for (int i = 0; i < HUMANS; i++) {
        uint8_t mac[6] = { 0x24, 0x6F, 0x28, 0x10, 0x00, (uint8_t)(i + 1) };
        memcpy(players[i].mac, mac, 6);
        kj_client_init(&players[i].c, seed * 131u + 1000u + (uint32_t)i);
        kj_names_init(&players[i].names, seed * 17u + (uint32_t)i);
        // 有中文昵称、英文昵称，也有没登记昵称的（空串）
        if (i == 7) {
            players[i].name[0] = '\0';
        } else if (i % 3 == 0) {
            snprintf(players[i].name, sizeof(players[i].name), "\xE7\x8E\xA9\xE5\xAE\xB6%02d", i);   // 玩家NN
        } else {
            snprintf(players[i].name, sizeof(players[i].name), "P%02d", i);
        }
        players[i].next_action_ms = 0;
        players[i].bump_at = 0;
        players[i].bump_group = 0;
    }
    for (int b = 0; b < BOTS; b++) CHECK_EQ(kj_server_command(&server, KJ_CMD_BOT_ADD, 0, now_ms), KJ_N_NONE);

    // 入座（有丢包）
    run_ms(6000);
    CHECK_EQ(kj_rules_player_count(&server.game), HUMANS + BOTS);
    for (int i = 0; i < HUMANS; i++) CHECK_EQ(players[i].c.link, KJ_LINK_JOINED);
    for (int i = 0; i < HUMANS; i++) CHECK(players[i].c.view.no >= 1);

    dealt_players = HUMANS + BOTS;
    CHECK_EQ(kj_server_command(&server, KJ_CMD_START, 0, now_ms), KJ_N_NONE);
    run_ms(20000);
    CHECK(results_seen > 10);

    // 选手 3 重启：同一 MAC 重新入座，拿回原座位与手牌
    int idx3 = kj_rules_find_mac(&server.game, players[3].mac);
    uint8_t stars3 = server.game.players[idx3].stars;
    kj_client_init(&players[3].c, 777);
    kj_names_init(&players[3].names, 778);   // 重启后内存里的昵称表也没了
    run_ms(5000);
    CHECK_EQ(players[3].c.link, KJ_LINK_JOINED);
    CHECK_EQ(players[3].c.view.no, kj_no_of(idx3));
    (void)stars3;

    // 庄家重启：保存 → 重新初始化 → 恢复
    uint8_t blob[KJ_PERSIST_MAX];
    size_t n = kj_persist_save(&server.game, blob, sizeof(blob));
    CHECK(n > 0);
    printf("sim: before host reboot %ld duels, %u views, %u beacons\n", results_seen,
           (unsigned)server.tx_views, (unsigned)server.tx_beacons);
    kj_server_init(&server, ROOM_ID, 4242);
    CHECK(kj_persist_load(&server.game, blob, n, now_ms));
    kj_names_init(&host_names, 4243);   // 昵称表不进快照：靠选手的周期广播补回来
    kj_names_set_room(&host_names, ROOM_ID);
    // 恢复时进行中的对决作废，结算计数按当前手牌重算
    {
        int cards = 0;
        for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
            if (server.game.players[i].used) cards += kj_rules_total_cards(&server.game.players[i]);
        }
        results_seen = (dealt_players * 12 - cards) / 2;
    }
    long before_reboot = results_seen;
    run_ms(90000);
    check_invariants();
    CHECK(results_seen > before_reboot);   // 庄家恢复后对局继续

    // 停止操作、关掉丢包，等所有视图收敛
    actions_enabled = false;
    loss_pct = 0;
    run_ms(8000);
    check_converged("after quiet period");
    check_names("after quiet period");

    // 宣布结束：仍有手牌的人判负，视图同步到所有选手
    CHECK_EQ(kj_server_command(&server, KJ_CMD_END, 0, now_ms), KJ_N_NONE);
    loss_pct = 30;
    run_ms(6000);
    loss_pct = 0;
    run_ms(3000);
    check_converged("after end");
    for (int i = 0; i < HUMANS; i++) {
        CHECK_EQ(players[i].c.view.phase, KJ_PHASE_ENDED);
        CHECK(kj_rules_is_final(players[i].c.view.status));
    }
    check_invariants();

    int st_count[KJ_ST_COUNT] = { 0 };
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
        if (server.game.players[i].used) st_count[server.game.players[i].status]++;
    }
    printf("sim: final statuses cleared=%d eliminated=%d failed=%d\n", st_count[KJ_ST_CLEARED],
           st_count[KJ_ST_ELIMINATED], st_count[KJ_ST_FAILED]);

    // 移除一名选手：他的设备收到 no=0 视图，回到找赌局（座位与牌一起移除，此后不再检查守恒）。
    uint8_t kicked_no = players[5].c.view.no;
    CHECK_EQ(kj_server_command(&server, KJ_CMD_KICK, kicked_no, now_ms), KJ_N_NONE);
    actions_enabled = false;
    for (int t = 0; t < 300 && players[5].c.link == KJ_LINK_JOINED; t++) step();
    CHECK(players[5].c.link != KJ_LINK_JOINED);

    printf("sim seed %u: %ld frames delivered, %ld dropped, %ld duels resolved, %u views, %u beacons\n",
           (unsigned)seed, delivered, dropped, results_seen, (unsigned)server.tx_views,
           (unsigned)server.tx_beacons);
    int clean = 0, clean_ok = 0;
    for (int k = 0; k < group_count; k++) {
        if (groups[k].size == 2 && groups[k].sent == 2) {
            clean++;
            clean_ok += groups[k].matched;
        }
    }
    printf("sim seed %u: %ld bump pairs scheduled, %ld bumps sent, %ld matched, %ld bump failures, "
           "clean pairs %d/%d matched\n",
           (unsigned)seed, bump_pairs, bump_sent, matches_seen, bump_fails_seen, clean_ok, clean);
    CHECK(clean > 0);
    CHECK(clean_ok * 10 >= clean * 8);   // 两人都按下了的配对，丢包 25% 下至少八成成功
    return kj_test_failures - failures_before;
}

int main(void)
{
    static const uint32_t seeds[] = { 0x12345678u, 0xCAFEBABEu, 0x0BADF00Du, 20261004u };
    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        if (run_sim(seeds[i])) fprintf(stderr, "seed %u failed\n", (unsigned)seeds[i]);
    }
    KJ_TEST_DONE("test_kj_sim");
}
