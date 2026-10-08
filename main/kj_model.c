// main/kj_model.c —— 界面模型组装（纯 C）。
#include "kj_model.h"

#include <stdio.h>
#include <string.h>

static int8_t clamp_battery(int battery)
{
    if (battery < 0) return -1;
    return (int8_t)(battery > 100 ? 100 : battery);
}

static void copy_text(char *dst, size_t cap, const char *src)
{
    size_t n = 0;
    if (src) {
        while (src[n] && n < cap - 1) n++;
    }
    memcpy(dst, src ? src : "", n);
    dst[n] = '\0';
}

void kj_ip_text(uint32_t ip, char out[16])
{
    uint8_t b[4];
    memcpy(b, &ip, 4);   // 网络字节序：内存里依次是 a.b.c.d
    snprintf(out, 16, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

uint8_t kj_model_opp_first(uint8_t sel, uint8_t count)
{
    int first = sel >= 3 ? sel - 2 : 0;
    int max = count > KJ_UI_OPP_ROWS ? count - KJ_UI_OPP_ROWS : 0;
    return (uint8_t)(first > max ? max : first);
}

static void base(kj_ui_model_t *m, uint8_t page, const kj_flow_t *f, const kj_model_env_t *env, int battery,
                 uint32_t now_ms)
{
    memset(m, 0, sizeof(*m));
    m->page = page;
    m->battery = clamp_battery(battery);
    m->now_ms = now_ms;
    m->toast = (uint8_t)kj_flow_active_toast(f, now_ms);
    m->host_confirm = -1;
    if (env) {
        m->conn = env->conn;
        m->channel = env->channel;
        m->net = env->net;
        m->ip = env->ip;
        m->hub_ip = env->hub_ip;
        m->dev_id = env->dev_id;
        copy_text(m->ssid, sizeof(m->ssid), env->ssid);
        copy_text(m->my_name, sizeof(m->my_name), env->my_name);
        copy_text(m->fw, sizeof(m->fw), env->fw);
    }
}

static void lookup(char *dst, const kj_names_if_t *names, uint16_t room, uint8_t no)
{
    dst[0] = '\0';
    const char *name = NULL;
    if (names && names->lookup && no && names->lookup(names->ctx, room, no, &name) && name) {
        copy_text(dst, KJ_UI_NAME_LEN, name);
    }
}

void kj_model_title(kj_ui_model_t *m, const kj_flow_t *f, const kj_model_env_t *env, int battery,
                    uint32_t now_ms)
{
    base(m, KJ_PAGE_TITLE, f, env, battery, now_ms);
    m->title_sel = f->title_sel;
}

void kj_model_settings(kj_ui_model_t *m, const kj_flow_t *f, const kj_model_env_t *env, int battery,
                       uint32_t now_ms)
{
    base(m, KJ_PAGE_SETTINGS, f, env, battery, now_ms);
    m->settings_count = (uint8_t)kj_flow_settings_items(f, m->settings_items);
    m->settings_sel = f->settings_sel < m->settings_count ? f->settings_sel : 0;
    m->settings_confirm = f->settings_confirm;
}

void kj_model_register(kj_ui_model_t *m, const kj_flow_t *f, const kj_model_env_t *env, uint8_t reg_state,
                       const char *url, int battery, uint32_t now_ms)
{
    base(m, KJ_PAGE_REGISTER, f, env, battery, now_ms);
    m->reg_state = reg_state;
    if (reg_state != KH_REG_INVALID && url) {
        copy_text(m->qr, sizeof(m->qr), url);
        // 屏幕上的网址去掉 "http://"，分成"主机:端口"和"路径"两行（一行放不下）
        const char *shown = strncmp(url, "http://", 7) == 0 ? url + 7 : url;
        const char *path = strchr(shown, '/');
        size_t host_len = path ? (size_t)(path - shown) : strlen(shown);
        if (host_len >= sizeof(m->line1)) host_len = sizeof(m->line1) - 1;
        memcpy(m->line1, shown, host_len);
        m->line1[host_len] = '\0';
        copy_text(m->line2, sizeof(m->line2), path ? path : "");
    }
}

void kj_model_provision(kj_ui_model_t *m, const kj_flow_t *f, const kj_model_env_t *env, uint8_t kind,
                        uint8_t prov_state, const char *qr, const char *line1, const char *line2, int battery,
                        uint32_t now_ms)
{
    base(m, KJ_PAGE_PROVISION, f, env, battery, now_ms);
    m->prov_kind = kind;
    m->prov_state = prov_state;
    copy_text(m->qr, sizeof(m->qr), qr);
    copy_text(m->line1, sizeof(m->line1), line1);
    copy_text(m->line2, sizeof(m->line2), line2);
}

void kj_model_player(kj_ui_model_t *m, const kj_flow_t *f, const kj_player_ctx_t *ctx, kj_page_t page,
                     const kj_model_env_t *env, const kj_names_if_t *names, int battery)
{
    const kj_client_t *c = ctx->client;
    base(m, (uint8_t)page, f, env, battery, ctx->now_ms);
    int rooms = ctx->room_count < KJ_CLIENT_MAX_ROOMS ? ctx->room_count : KJ_CLIENT_MAX_ROOMS;
    for (int i = 0; i < rooms; i++) {
        m->rooms[i] = ctx->rooms[i];
        m->rooms[i].seen_ms = 0;
    }
    m->room_count = (uint8_t)(rooms > 0 ? rooms : 0);
    m->room_sel = f->room_sel;
    m->joining = c->link == KJ_LINK_JOINING;
    if (c->link != KJ_LINK_JOINED) return;
    m->room = c->room;
    m->view = c->view;
    m->view.epoch = 0;   // 庄家重启不应让界面重建
    m->seated = c->have_room_info ? c->room_info.seated : 0;
    m->disconnected = !ctx->connected;
    int n = ctx->opp_count < KJ_UI_OPP_MAX ? ctx->opp_count : KJ_UI_OPP_MAX;
    for (int i = 0; i < n; i++) m->opps[i] = ctx->opps[i];
    m->opp_count = (uint8_t)(n > 0 ? n : 0);
    m->opp_sel = f->opp_sel < m->opp_count ? f->opp_sel : 0;
    m->card_sel = f->card_sel;
    if (page == KJ_PAGE_OPPONENTS) {
        m->opp_first = kj_model_opp_first(m->opp_sel, m->opp_count);
        for (int r = 0; r < KJ_UI_OPP_ROWS && m->opp_first + r < m->opp_count; r++) {
            lookup(m->opp_names[r], names, c->room, m->opps[m->opp_first + r].no);
        }
    }
    uint8_t peer = page == KJ_PAGE_REVEAL ? c->view.res_opp_no : c->view.peer_no;
    lookup(m->peer_name, names, c->room, peer);
    // 倒计时按收到视图后的本地流逝时间实时递减。
    uint32_t el = (ctx->now_ms - c->view_rx_ms) / 1000u;
    m->deadline_s = (uint8_t)(c->view.deadline_s > el ? c->view.deadline_s - el : 0);
}

void kj_model_host(kj_ui_model_t *m, const kj_flow_t *f, const kj_server_t *s, uint32_t now_ms, int battery,
                   uint8_t board, const kj_model_env_t *env, const kj_names_if_t *names)
{
    base(m, f->host_roster ? KJ_PAGE_HOST_ROSTER : KJ_PAGE_HOST, f, env, battery, now_ms);
    m->host_room = s->room;
    m->host_phase = s->game.phase;
    m->host_phase_s = (now_ms - s->game.phase_since_ms) / 1000u;
    kj_rules_summary(&s->game, now_ms, &m->host_sum);
    m->host_sel = f->host_sel;
    m->host_confirm = f->host_confirm;
    for (int i = 0; i < KJ_HM_COUNT; i++) {
        if (kj_flow_host_item_enabled((kj_host_item_t)i, &s->game)) m->host_enabled |= (uint8_t)(1u << i);
    }
    m->board = board;
    m->roster_first = f->roster_first;
    m->roster_total = (uint8_t)kj_rules_player_count(&s->game);
    if (!f->host_roster) return;
    int seen = 0;
    for (int i = 0; i < KJ_MAX_PLAYERS && m->roster_count < KJ_ROSTER_ROWS; i++) {
        const kj_player_t *p = &s->game.players[i];
        if (!p->used || seen++ < f->roster_first) continue;
        kj_roster_row_t *r = &m->roster[m->roster_count++];
        r->no = kj_no_of(i);
        r->is_bot = p->is_bot;
        r->online = kj_rules_online(&s->game, i, now_ms) ? 1 : 0;
        r->status = p->status;
        r->stars = p->stars;
        r->cards = kj_rules_total_cards(p);
        if (!p->is_bot) lookup(r->name, names, s->room, r->no);
    }
}
