// main/kj_flow.c —— 选手 / 庄家界面状态机（纯 C）。
#include "kj_flow.h"

#include <string.h>

void kj_flow_init(kj_flow_t *f, uint8_t title_sel)
{
    memset(f, 0, sizeof(*f));
    f->conn = KJ_CONN_DIRECT;
    f->title_sel = title_sel < KJ_TITLE_ITEMS ? title_sel : 0;
    f->host_confirm = -1;
    f->last_page = KJ_PAGE_TITLE;
}

void kj_flow_set_conn(kj_flow_t *f, uint8_t conn)
{
    f->conn = conn == KJ_CONN_HUB ? KJ_CONN_HUB : KJ_CONN_DIRECT;
}

static uint8_t wrap(int v, int n)
{
    if (n <= 0) return 0;
    return (uint8_t)(((v % n) + n) % n);
}

void kj_flow_toast(kj_flow_t *f, kj_toast_t t, uint32_t now_ms)
{
    f->toast = (uint8_t)t;
    f->toast_until = now_ms + KJ_TOAST_MS;
}

kj_toast_t kj_flow_active_toast(const kj_flow_t *f, uint32_t now_ms)
{
    if (f->toast == KJ_TOAST_NONE || (int32_t)(now_ms - f->toast_until) >= 0) return KJ_TOAST_NONE;
    return (kj_toast_t)f->toast;
}

kj_toast_t kj_flow_toast_for_notice(uint8_t notice)
{
    switch (notice) {
    case KJ_N_DECLINED: return KJ_TOAST_DECLINED;
    case KJ_N_CANCELLED: return KJ_TOAST_CANCELLED;
    case KJ_N_TIMEOUT: return KJ_TOAST_TIMEOUT;
    case KJ_N_WITHDRAWN: return KJ_TOAST_WITHDRAWN;
    case KJ_N_ABORTED: return KJ_TOAST_ABORTED;
    case KJ_N_BUSY: return KJ_TOAST_BUSY;
    case KJ_N_NOT_RUNNING: return KJ_TOAST_NOT_RUNNING;
    case KJ_N_NO_CARD: return KJ_TOAST_NO_CARD;
    case KJ_N_INVALID: return KJ_TOAST_INVALID;
    case KJ_N_FULL: return KJ_TOAST_FULL;
    case KJ_N_BUMP_ALONE: return KJ_TOAST_BUMP_ALONE;
    case KJ_N_BUMP_CROWD: return KJ_TOAST_BUMP_CROWD;
    case KJ_N_MATCH_CANCELLED: return KJ_TOAST_MATCH_CANCELLED;
    default: return KJ_TOAST_NONE;
    }
}

kj_action_t kj_flow_title_key(kj_flow_t *f, kj_key_t key)
{
    kj_action_t a = { 0 };
    if (key == KJ_KEY_UP || key == KJ_KEY_DOWN) {
        f->title_sel = wrap(f->title_sel + (key == KJ_KEY_UP ? -1 : 1), KJ_TITLE_ITEMS);
    } else if (key == KJ_KEY_OK) {
        if (f->title_sel == 2) {
            a.kind = KJ_ACT_SETTINGS;
            kj_flow_settings_open(f, KJ_SET_NAME);
        } else {
            a.kind = KJ_ACT_ROLE;
            a.arg = f->title_sel;
        }
    }
    return a;
}

int kj_flow_settings_items(const kj_flow_t *f, uint8_t items[KJ_SET_COUNT])
{
    int n = 0;
    items[n++] = KJ_SET_NAME;
    if (f->conn == KJ_CONN_HUB) items[n++] = KJ_SET_WIFI;
    items[n++] = KJ_SET_CONN;
    items[n++] = KJ_SET_BACK;
    return n;
}

void kj_flow_settings_open(kj_flow_t *f, uint8_t item)
{
    uint8_t items[KJ_SET_COUNT];
    int n = kj_flow_settings_items(f, items);
    f->settings_sel = 0;
    f->settings_confirm = false;
    for (int i = 0; i < n; i++) {
        if (items[i] == item) f->settings_sel = (uint8_t)i;
    }
}

kj_action_t kj_flow_settings_key(kj_flow_t *f, kj_key_t key)
{
    kj_action_t a = { 0 };
    if (f->settings_confirm) {   // 确认切换联机方式：OK 确定，其他任何键取消
        f->settings_confirm = false;
        if (key == KJ_KEY_OK) {
            a.kind = KJ_ACT_SET_CONN;
            a.arg = f->conn == KJ_CONN_HUB ? KJ_CONN_DIRECT : KJ_CONN_HUB;
        }
        return a;
    }
    uint8_t items[KJ_SET_COUNT];
    int n = kj_flow_settings_items(f, items);
    if (f->settings_sel >= n) f->settings_sel = 0;
    if (key == KJ_KEY_UP || key == KJ_KEY_DOWN) {
        f->settings_sel = wrap(f->settings_sel + (key == KJ_KEY_UP ? -1 : 1), n);
    } else if (key == KJ_KEY_OK) {
        uint8_t item = items[f->settings_sel];
        if (item == KJ_SET_BACK) {
            a.kind = KJ_ACT_BACK;
        } else if (item == KJ_SET_CONN) {
            f->settings_confirm = true;
        } else {
            a.kind = KJ_ACT_SET_ITEM;
            a.arg = item;
        }
    } else if (key == KJ_KEY_OK_LONG) {
        a.kind = KJ_ACT_BACK;
    }
    return a;
}

kj_action_t kj_flow_register_key(kj_flow_t *f, kj_key_t key)
{
    kj_action_t a = { 0 };
    if (key == KJ_KEY_OK) a.kind = f->conn == KJ_CONN_HUB ? KJ_ACT_REG_REFRESH : KJ_ACT_REG_START;
    if (key == KJ_KEY_OK_LONG) a.kind = KJ_ACT_BACK;
    return a;
}

kj_action_t kj_flow_provision_key(kj_flow_t *f, kj_key_t key)
{
    (void)f;
    kj_action_t a = { 0 };
    if (key == KJ_KEY_OK_LONG) a.kind = KJ_ACT_PROV_SKIP;
    return a;
}

uint8_t kj_flow_valid_card(const kj_view_t *v, uint8_t preferred, int direction)
{
    int dir = direction < 0 ? -1 : 1;
    int c = preferred % KJ_CARD_TYPES;
    for (int k = 0; k < KJ_CARD_TYPES; k++) {
        if (v->cards[c] > 0) return (uint8_t)c;
        c = (c + dir + KJ_CARD_TYPES) % KJ_CARD_TYPES;
    }
    return KJ_CARD_NONE;
}

static kj_page_t derive_page(const kj_flow_t *f, const kj_view_t *v)
{
    if (f->reveal_active) return KJ_PAGE_REVEAL;
    if (v->phase == KJ_PHASE_LOBBY || v->status == KJ_ST_WAITING) return KJ_PAGE_SEAT;
    switch (v->status) {
    case KJ_ST_IDLE:
        if (f->bump_pending) return KJ_PAGE_BUMP;
        return f->picking ? KJ_PAGE_OPPONENTS : KJ_PAGE_HAND;
    case KJ_ST_CHALLENGING: return KJ_PAGE_WAIT;
    case KJ_ST_CHALLENGED: return KJ_PAGE_CHALLENGED;
    case KJ_ST_DUEL: return KJ_PAGE_CHOOSE;
    case KJ_ST_BUMPING: return KJ_PAGE_BUMP;
    case KJ_ST_MATCHED: return KJ_PAGE_MATCHED;
    default: return KJ_PAGE_FINAL;
    }
}

static void sync_opp_selection(kj_flow_t *f, const kj_player_ctx_t *ctx)
{
    if (ctx->opp_count <= 0) {
        f->opp_sel = 0;
        f->opp_no = 0;
        return;
    }
    for (int i = 0; i < ctx->opp_count; i++) {
        if (ctx->opps[i].no == f->opp_no) {
            f->opp_sel = (uint8_t)i;
            return;
        }
    }
    if (f->opp_sel >= ctx->opp_count) f->opp_sel = (uint8_t)(ctx->opp_count - 1);
    f->opp_no = ctx->opps[f->opp_sel].no;
}

kj_page_t kj_flow_player_update(kj_flow_t *f, const kj_player_ctx_t *ctx, kj_cue_t *cue)
{
    kj_cue_t out = KJ_CUE_NONE;
    const kj_client_t *c = ctx->client;
    kj_page_t page;
    if (c->link != KJ_LINK_JOINED) {
        f->picking = false;
        f->bump_pending = false;
        f->reveal_active = false;
        f->primed = false;
        if (ctx->room_count <= 0) {
            f->room_sel = 0;
        } else if (f->room_sel >= ctx->room_count) {
            f->room_sel = (uint8_t)(ctx->room_count - 1);
        }
        page = KJ_PAGE_ROOMS;
    } else {
        const kj_view_t *v = &c->view;
        if (!f->primed || f->room != c->room) {
            // 第一次拿到视图（入座 / 重连）：历史通知与已经结算过的亮牌不再重播。
            f->primed = true;
            f->room = c->room;
            f->game_id = v->game_id;
            f->notice_seq = v->notice_seq;
            f->shown_res_duel = v->res_duel_id;
            f->last_status = v->status;
            f->picking = false;
            f->reveal_active = false;
        }
        if (v->game_id != f->game_id) {
            f->game_id = v->game_id;
            f->picking = false;
            f->reveal_active = false;
            f->shown_res_duel = v->res_duel_id;
        }
        if (v->notice_seq != f->notice_seq) {
            f->notice_seq = v->notice_seq;
            f->bump_pending = false;   // 碰拳的结果（没碰到 / 人太多 / 被拒）以通知的形式到达
            kj_toast_t t = kj_flow_toast_for_notice(v->notice);
            if (t != KJ_TOAST_NONE) {
                kj_flow_toast(f, t, ctx->now_ms);
                out = KJ_CUE_NOTICE;
            }
        }
        // 庄家已接手（状态离开空闲），或迟迟没有结果：结束本地的"碰拳中"。
        if (f->bump_pending &&
            (v->status != KJ_ST_IDLE || (uint32_t)(ctx->now_ms - f->bump_since) >= KJ_BUMP_LOCAL_MS)) {
            f->bump_pending = false;
        }
        if (!f->reveal_active && v->res_duel_id != 0 && v->res_duel_id != f->shown_res_duel &&
            v->res_outcome != KJ_OUT_NONE) {
            f->reveal_active = true;
            f->reveal_since = ctx->now_ms;
            out = v->res_outcome == KJ_OUT_WIN ? KJ_CUE_WIN
                : v->res_outcome == KJ_OUT_LOSE ? KJ_CUE_LOSE : KJ_CUE_DRAW;
        }
        if (f->reveal_active && (uint32_t)(ctx->now_ms - f->reveal_since) >= KJ_REVEAL_AUTO_MS) {
            f->reveal_active = false;
            f->shown_res_duel = v->res_duel_id;
        }
        if (v->status != KJ_ST_IDLE) f->picking = false;
        if (v->status == KJ_ST_DUEL && v->status != f->last_status) {
            uint8_t card = kj_flow_valid_card(v, KJ_SCISSORS, 1);   // 进入对决默认停在中间那张
            f->card_sel = card == KJ_CARD_NONE ? 0 : card;
        }
        f->last_status = v->status;
        page = derive_page(f, v);
        if (page == KJ_PAGE_OPPONENTS) sync_opp_selection(f, ctx);
        if (page == KJ_PAGE_CHOOSE && v->my_lock == KJ_CARD_NONE && v->cards[f->card_sel % 3] == 0) {
            uint8_t card = kj_flow_valid_card(v, f->card_sel, 1);
            f->card_sel = card == KJ_CARD_NONE ? 0 : card;
        }
        if (page != f->last_page && out == KJ_CUE_NONE) {
            if (page == KJ_PAGE_CHALLENGED) out = KJ_CUE_ALERT;
            else if (page == KJ_PAGE_MATCHED) out = KJ_CUE_MATCH;
            else if (page == KJ_PAGE_CHOOSE) out = KJ_CUE_DUEL;
            else if (page == KJ_PAGE_FINAL) out = v->status == KJ_ST_CLEARED ? KJ_CUE_CLEARED : KJ_CUE_OUT;
        }
    }
    f->last_page = (uint8_t)page;
    if (cue) *cue = out;
    return page;
}

kj_action_t kj_flow_player_key(kj_flow_t *f, const kj_player_ctx_t *ctx, kj_key_t key)
{
    kj_action_t a = { 0 };
    const kj_view_t *v = &ctx->client->view;
    switch ((kj_page_t)f->last_page) {
    case KJ_PAGE_ROOMS:
        if (key == KJ_KEY_UP) f->room_sel = wrap(f->room_sel - 1, ctx->room_count);
        if (key == KJ_KEY_DOWN) f->room_sel = wrap(f->room_sel + 1, ctx->room_count);
        if (key == KJ_KEY_OK && ctx->room_count > 0 && ctx->client->link == KJ_LINK_IDLE) {
            a.kind = KJ_ACT_JOIN;
            a.room = ctx->rooms[f->room_sel % ctx->room_count].room;
        }
        if (key == KJ_KEY_OK_LONG) a.kind = KJ_ACT_TO_TITLE;
        break;
    case KJ_PAGE_SEAT:
        if (key == KJ_KEY_OK_LONG) a.kind = KJ_ACT_LEAVE;
        break;
    case KJ_PAGE_HAND:
        if (key == KJ_KEY_OK) {
            f->picking = true;
            f->opp_sel = 0;
            f->opp_no = ctx->opp_count > 0 ? ctx->opps[0].no : 0;
            f->last_page = KJ_PAGE_OPPONENTS;
        } else if (key == KJ_KEY_OK_LONG) {
            // 碰拳：立刻切到碰拳页，结果由庄家的视图 / 通知决定。
            a.kind = KJ_ACT_REQUEST;
            a.op = KJ_OP_BUMP;
            f->bump_pending = true;
            f->bump_since = ctx->now_ms;
            f->last_page = KJ_PAGE_BUMP;
        }
        break;
    case KJ_PAGE_MATCHED:
        if (key == KJ_KEY_OK_LONG) {   // 认错人了：取消配对
            a.kind = KJ_ACT_REQUEST;
            a.op = KJ_OP_CANCEL;
        }
        break;
    case KJ_PAGE_OPPONENTS:
        if (key == KJ_KEY_UP || key == KJ_KEY_DOWN) {
            f->opp_sel = wrap(f->opp_sel + (key == KJ_KEY_UP ? -1 : 1), ctx->opp_count);
            f->opp_no = ctx->opp_count > 0 ? ctx->opps[f->opp_sel].no : 0;
        } else if (key == KJ_KEY_OK && ctx->opp_count > 0) {
            a.kind = KJ_ACT_REQUEST;
            a.op = KJ_OP_CHALLENGE;
            a.arg = ctx->opps[f->opp_sel % ctx->opp_count].no;
        } else if (key == KJ_KEY_OK_LONG) {
            f->picking = false;
            f->last_page = KJ_PAGE_HAND;
        }
        break;
    case KJ_PAGE_WAIT:
        if (key == KJ_KEY_OK_LONG) {
            a.kind = KJ_ACT_REQUEST;
            a.op = KJ_OP_CANCEL;
        }
        break;
    case KJ_PAGE_CHALLENGED:
        if (key == KJ_KEY_OK || key == KJ_KEY_OK_LONG) {
            a.kind = KJ_ACT_REQUEST;
            a.op = key == KJ_KEY_OK ? KJ_OP_ACCEPT : KJ_OP_DECLINE;
        }
        break;
    case KJ_PAGE_CHOOSE:
        if (v->my_lock != KJ_CARD_NONE) break;   // 暗牌已扣下
        if (key == KJ_KEY_UP || key == KJ_KEY_DOWN) {
            int dir = key == KJ_KEY_UP ? -1 : 1;
            uint8_t card = kj_flow_valid_card(v, wrap(f->card_sel + dir, KJ_CARD_TYPES), dir);
            if (card != KJ_CARD_NONE) f->card_sel = card;
        } else if (key == KJ_KEY_OK) {
            if (f->card_sel < KJ_CARD_TYPES && v->cards[f->card_sel] > 0) {
                a.kind = KJ_ACT_REQUEST;
                a.op = KJ_OP_PLAY;
                a.arg = f->card_sel;
            }
        } else if (key == KJ_KEY_OK_LONG) {
            a.kind = KJ_ACT_REQUEST;
            a.op = KJ_OP_WITHDRAW;
        }
        break;
    case KJ_PAGE_REVEAL:
        if (key == KJ_KEY_OK) {
            f->reveal_active = false;
            f->shown_res_duel = v->res_duel_id;
        }
        break;
    default:
        break;
    }
    return a;
}

void kj_flow_bump_rejected(kj_flow_t *f)
{
    f->bump_pending = false;
    if (f->last_page == KJ_PAGE_BUMP) f->last_page = KJ_PAGE_HAND;
}

bool kj_flow_host_item_enabled(kj_host_item_t item, const kj_game_t *g)
{
    int count = kj_rules_player_count(g);
    switch (item) {
    case KJ_HM_START: return g->phase == KJ_PHASE_LOBBY && count >= 2;
    case KJ_HM_END: return g->phase == KJ_PHASE_RUNNING;
    case KJ_HM_NEW: return g->phase != KJ_PHASE_LOBBY;
    case KJ_HM_BOT_ADD: return count < KJ_MAX_PLAYERS;
    case KJ_HM_BOT_DEL:
        for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
            if (g->players[i].used && g->players[i].is_bot) return true;
        }
        return false;
    case KJ_HM_RESET: return count > 0;
    case KJ_HM_ROSTER: return count > 0;
    default: return false;
    }
}

static kj_cmd_t host_cmd(kj_host_item_t item)
{
    switch (item) {
    case KJ_HM_START: return KJ_CMD_START;
    case KJ_HM_END: return KJ_CMD_END;
    case KJ_HM_NEW: return KJ_CMD_NEW_GAME;
    case KJ_HM_BOT_ADD: return KJ_CMD_BOT_ADD;
    case KJ_HM_BOT_DEL: return KJ_CMD_BOT_REMOVE;
    case KJ_HM_RESET: return KJ_CMD_RESET;
    default: return KJ_CMD_NONE;
    }
}

static int roster_max_first(const kj_game_t *g)
{
    int n = kj_rules_player_count(g);
    return n > KJ_ROSTER_ROWS ? n - KJ_ROSTER_ROWS : 0;
}

void kj_flow_host_sync(kj_flow_t *f, const kj_game_t *g)
{
    if (f->host_confirm >= 0 && !kj_flow_host_item_enabled((kj_host_item_t)f->host_confirm, g)) {
        f->host_confirm = -1;
    }
    if (f->roster_first > roster_max_first(g)) f->roster_first = (uint8_t)roster_max_first(g);
    if (f->host_roster && kj_rules_player_count(g) == 0) f->host_roster = false;
    if (kj_flow_host_item_enabled((kj_host_item_t)f->host_sel, g)) return;
    // 当前项不可用（例如刚开局后的"开始赌局"）：顺延到下一个可用项。
    for (int k = 1; k < KJ_HM_COUNT; k++) {
        int item = (f->host_sel + k) % KJ_HM_COUNT;
        if (kj_flow_host_item_enabled((kj_host_item_t)item, g)) {
            f->host_sel = (uint8_t)item;
            return;
        }
    }
}

kj_action_t kj_flow_host_key(kj_flow_t *f, const kj_game_t *g, kj_key_t key, uint32_t now_ms)
{
    kj_action_t a = { 0 };
    if (f->host_roster) {   // 名单：▲▼ 滚动，OK / 长按回到面板
        int first = f->roster_first;
        if (key == KJ_KEY_UP) first--;
        if (key == KJ_KEY_DOWN) first++;
        int max = roster_max_first(g);
        f->roster_first = (uint8_t)(first < 0 ? 0 : first > max ? max : first);
        if (key == KJ_KEY_OK || key == KJ_KEY_OK_LONG) f->host_roster = false;
        return a;
    }
    if (f->host_confirm >= 0) {
        if (key == KJ_KEY_OK && kj_flow_host_item_enabled((kj_host_item_t)f->host_confirm, g)) {
            a.kind = KJ_ACT_HOST_CMD;
            a.cmd = host_cmd((kj_host_item_t)f->host_confirm);
        }
        f->host_confirm = -1;   // 其他任何键都取消
        return a;
    }
    if (key == KJ_KEY_UP || key == KJ_KEY_DOWN) {
        // 跳过当前不可用的菜单项；全部不可用时按顺序移动。
        int dir = key == KJ_KEY_UP ? -1 : 1;
        int sel = f->host_sel;
        for (int k = 1; k <= KJ_HM_COUNT; k++) {
            int item = wrap(f->host_sel + dir * k, KJ_HM_COUNT);
            if (kj_flow_host_item_enabled((kj_host_item_t)item, g)) {
                sel = item;
                break;
            }
            if (k == KJ_HM_COUNT) sel = wrap(f->host_sel + dir, KJ_HM_COUNT);
        }
        f->host_sel = (uint8_t)sel;
    }
    if (key == KJ_KEY_OK) {
        kj_host_item_t item = (kj_host_item_t)f->host_sel;
        if (!kj_flow_host_item_enabled(item, g)) {
            bool need_two = item == KJ_HM_START && g->phase == KJ_PHASE_LOBBY;
            kj_flow_toast(f, need_two ? KJ_TOAST_NEED_TWO : KJ_TOAST_INVALID, now_ms);
            a.kind = KJ_ACT_NONE;
            a.arg = 1;            // 1 = 被拒绝（调用方据此播放错误音）
        } else if (item == KJ_HM_ROSTER) {
            f->host_roster = true;
            f->roster_first = 0;
        } else if (item == KJ_HM_END || item == KJ_HM_NEW || item == KJ_HM_RESET) {
            f->host_confirm = (int8_t)item;
        } else {
            a.kind = KJ_ACT_HOST_CMD;
            a.cmd = host_cmd(item);
        }
    }
    return a;
}
