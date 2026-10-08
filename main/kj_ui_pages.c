// main/kj_ui_pages.c —— 限定猜拳各页面的版面（240×320，四角 30 px 圆角遮罩之内）。
// 昵称（任意中文）一律用 kj_font_name：正文字体子集 + 回退到昵称字库。
#include "kj_model.h"
#include "kj_ui_internal.h"

#include <stdio.h>
#include <string.h>

#define CARD_ROW_W 64
#define CARD_ROW_H 92

static void no_text(char *buf, size_t n, unsigned no)
{
    snprintf(buf, n, KJ_STR_NO_FMT, no);
}

// 对手的称呼：有昵称用昵称，否则"07号"；电脑选手"电脑 07"。
static void who_text(char *buf, size_t n, unsigned no, const char *name, bool bot)
{
    if (name && name[0]) {
        snprintf(buf, n, "%s", name);
    } else if (bot) {
        snprintf(buf, n, "%s %02u", KJ_STR_BOT, no);
    } else {
        no_text(buf, n, no);
    }
}

static const char *net_text(uint8_t net)
{
    switch (net) {
    case KJ_NET_CONNECTING: return KJ_STR_NET_CONNECTING;
    case KJ_NET_SEARCHING: return KJ_STR_NET_SEARCHING;
    case KJ_NET_OK: return KJ_STR_NET_OK;
    case KJ_NET_OLD: return KJ_STR_NET_OLD;
    case KJ_NET_DIRECT: return KJ_STR_NET_DIRECT;
    case KJ_NET_NO_RADIO: return KJ_STR_NET_NO_RADIO;
    default: return KJ_STR_NET_NO_WIFI;
    }
}

static uint32_t net_color(uint8_t net)
{
    if (net == KJ_NET_OK || net == KJ_NET_DIRECT) return KJ_C_GREEN;
    return (net == KJ_NET_CONNECTING || net == KJ_NET_SEARCHING) ? KJ_C_GOLD : KJ_C_RED;
}

// 雷达：三圈同心圆 + 中心点（找赌局 / 等电脑服务）
static void radar(lv_obj_t *s, int cy)
{
    for (int i = 0; i < 3; i++) {
        int d = 40 + i * 36;
        kj_frame(s, 120 - d / 2, cy - d / 2, d, d, i == 0 ? KJ_C_RED : 0x3A2F28, 2, LV_RADIUS_CIRCLE);
    }
    kj_box(s, 114, cy - 6, 12, 12, KJ_C_RED, LV_RADIUS_CIRCLE);
}

// 白底二维码（自带留白）。text 为空时不画。
static void qr_box(lv_obj_t *s, int x, int y, int box, const char *text)
{
    if (!text || !text[0]) return;
    lv_obj_t *bg = kj_box(s, x, y, box, box, 0xFFFFFF, 10);
    lv_obj_t *qr = lv_qrcode_create(bg);
    lv_qrcode_set_size(qr, box - 16);
    lv_qrcode_set_dark_color(qr, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(qr, lv_color_hex(0xFFFFFF));
    lv_qrcode_update(qr, text, (uint32_t)strlen(text));
    lv_obj_center(qr);
}

// ---------------------------------------------------------------------------
// 首页：选手 / 庄家 / 设置
// ---------------------------------------------------------------------------
static void page_title(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    kj_top_bar(s, NULL, 0);
    kj_text_at(s, &kj_zh14, net_color(m->net), net_text(m->net), LV_ALIGN_TOP_LEFT, 24, 13);
    kj_text_at(s, &kj_big48, KJ_C_TEXT, KJ_BIG_TITLE_1, LV_ALIGN_TOP_MID, -50, 30);
    kj_text_at(s, &kj_big48, KJ_C_RED, KJ_BIG_TITLE_2, LV_ALIGN_TOP_MID, 50, 30);
    // 三张扇形摆开的牌（中间那张抬高）
    kj_card(s, 52, 96, 44, 58, KJ_ROCK, 0, 0);
    kj_card(s, 98, 88, 44, 58, KJ_SCISSORS, 0, 0);
    kj_card(s, 144, 96, 44, 58, KJ_PAPER, 0, 0);
    if (m->my_name[0]) {
        char buf[48];
        snprintf(buf, sizeof(buf), KJ_STR_HELLO_FMT, m->my_name);
        kj_text_box(s, &kj_font_name, KJ_C_GOLD, buf, 20, 160, 200, LV_TEXT_ALIGN_CENTER);
    } else {
        kj_text_at(s, &kj_zh14, KJ_C_MUTED, KJ_STR_NO_NAME, LV_ALIGN_TOP_MID, 0, 163);
    }
    static const char *const items[KJ_TITLE_ITEMS] = { KJ_STR_ROLE_PLAYER, KJ_STR_ROLE_HOST, KJ_STR_ROLE_SETTINGS };
    for (int i = 0; i < KJ_TITLE_ITEMS; i++) {
        kj_pill(s, 22, 190 + i * 32, 196, 28, items[i], m->title_sel == i, i == 2 ? KJ_C_GOLD : KJ_C_RED);
    }
    kj_footer(s, KJ_STR_HINT_TITLE);
}

// ---------------------------------------------------------------------------
// 设置：本机信息 + 登记昵称 /（重新配网）/ 改用另一种联机方式 / 返回
// ---------------------------------------------------------------------------
static void info_row(lv_obj_t *panel, int y, const char *label, const char *value, uint32_t color)
{
    kj_text_at(panel, &kj_zh14, KJ_C_MUTED, label, LV_ALIGN_TOP_LEFT, 10, y);
    kj_text_box(panel, &kj_font_name, color, value, 58, y - 2, 136, LV_TEXT_ALIGN_LEFT);
}

static const char *settings_item_text(const kj_ui_model_t *m, uint8_t item)
{
    switch (item) {
    case KJ_SET_NAME: return KJ_STR_SET_NAME;
    case KJ_SET_WIFI: return KJ_STR_SET_WIFI;
    case KJ_SET_CONN: return m->conn == KJ_CONN_HUB ? KJ_STR_SET_TO_DIRECT : KJ_STR_SET_TO_HUB;
    default: return KJ_STR_SET_BACK;
    }
}

static void page_settings(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    kj_top_bar(s, KJ_STR_SET_TITLE, KJ_C_GOLD);
    lv_obj_t *panel = kj_box(s, 18, 38, 204, 112, KJ_C_PANEL, 12);
    info_row(panel, 7, KJ_STR_INFO_NAME, m->my_name[0] ? m->my_name : KJ_STR_INFO_NO_NAME,
             m->my_name[0] ? KJ_C_GOLD : KJ_C_DIM);
    if (m->conn == KJ_CONN_HUB) {
        info_row(panel, 32, KJ_STR_INFO_WIFI, m->ssid[0] ? m->ssid : KJ_STR_NET_NO_WIFI,
                 m->net >= KJ_NET_SEARCHING ? KJ_C_TEXT : KJ_C_DIM);
        char ip[16];
        kj_ip_text(m->hub_ip, ip);
        info_row(panel, 57, KJ_STR_INFO_HUB, m->net == KJ_NET_OK || m->net == KJ_NET_OLD ? ip : net_text(m->net),
                 net_color(m->net));
    } else {
        info_row(panel, 32, KJ_STR_INFO_CONN, m->net == KJ_NET_NO_RADIO ? KJ_STR_NET_NO_RADIO : KJ_STR_INFO_DIRECT,
                 net_color(m->net));
        char ch[8];
        snprintf(ch, sizeof(ch), "%u", (unsigned)m->channel);
        info_row(panel, 57, KJ_STR_INFO_CHANNEL, ch, KJ_C_TEXT);
    }
    char dev[48];
    snprintf(dev, sizeof(dev), KJ_STR_INFO_DEVICE_FMT, (unsigned)m->dev_id, m->fw);
    kj_text_at(panel, &kj_zh14, KJ_C_DIM, dev, LV_ALIGN_TOP_LEFT, 10, 84);
    // 电脑服务模式 4 项、直连 3 项：同样的行距，最后一项离页脚至少 12 px
    for (int i = 0; i < m->settings_count; i++) {
        uint8_t item = m->settings_items[i];
        uint32_t color = item == KJ_SET_BACK ? 0x5A4D42 : item == KJ_SET_CONN ? KJ_C_GOLD : KJ_C_RED;
        kj_pill(s, 22, 158 + i * 32, 196, 28, settings_item_text(m, item), m->settings_sel == i, color);
    }
    kj_footer(s, KJ_STR_HINT_SETTINGS);
    if (m->settings_confirm) {   // 切换联机方式要重启：先确认
        lv_obj_t *dlg = kj_box(s, 18, 100, 204, 124, 0x241C18, 14);
        lv_obj_set_style_border_width(dlg, 2, 0);
        lv_obj_set_style_border_color(dlg, lv_color_hex(KJ_C_GOLD), 0);
        kj_text_at(dlg, &kj_zh18, KJ_C_TEXT, m->conn == KJ_CONN_HUB ? KJ_STR_CONN_Q_DIRECT : KJ_STR_CONN_Q_HUB,
                   LV_ALIGN_TOP_MID, 0, 16);
        kj_text_at(dlg, &kj_zh14, KJ_C_MUTED, KJ_STR_CONN_SAME, LV_ALIGN_TOP_MID, 0, 50);
        kj_text_at(dlg, &kj_zh14, KJ_C_MUTED, KJ_STR_CONN_RESTART, LV_ALIGN_TOP_MID, 0, 70);
        kj_text_at(dlg, &kj_zh14, KJ_C_GOLD, KJ_STR_CONFIRM_HINT, LV_ALIGN_TOP_MID, 0, 96);
    }
}

// ---------------------------------------------------------------------------
// 登记昵称：电脑服务模式扫码打开 hub 的登记页；直连模式先说明，再重启进入热点登记
// ---------------------------------------------------------------------------
// 登记成功的"欢迎"印章 + 昵称
static void welcome(lv_obj_t *s, const char *name, const char *sub)
{
    kj_frame(s, 50, 50, 140, 140, KJ_C_GOLD, 4, LV_RADIUS_CIRCLE);
    kj_frame(s, 60, 60, 120, 120, KJ_C_GOLD, 1, LV_RADIUS_CIRCLE);
    kj_text_at(s, &kj_big48, KJ_C_GOLD, KJ_BIG_WELCOME, LV_ALIGN_TOP_MID, 0, 88);
    kj_text_box(s, &kj_font_name, KJ_C_GOLD, name, 20, 204, 200, LV_TEXT_ALIGN_CENTER);
    kj_text_at(s, &kj_zh14, KJ_C_MUTED, sub, LV_ALIGN_TOP_MID, 0, 232);
}

// 直连模式：没有电脑服务的登记页，先说明"设备会重启并开热点"，OK 之后进入热点登记
static void page_register_direct(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    (void)m;
    radar(s, 112);
    kj_text_at(s, &kj_zh18, KJ_C_GOLD, KJ_STR_REG_AP_LEAD, LV_ALIGN_TOP_MID, 0, 184);
    kj_text_at(s, &kj_zh14, KJ_C_TEXT, KJ_STR_REG_AP_1, LV_ALIGN_TOP_MID, 0, 214);
    kj_text_at(s, &kj_zh14, KJ_C_TEXT, KJ_STR_REG_AP_2, LV_ALIGN_TOP_MID, 0, 234);
    kj_text_at(s, &kj_zh14, KJ_C_DIM, KJ_STR_REG_AP_3, LV_ALIGN_TOP_MID, 0, 262);
    kj_footer(s, KJ_STR_HINT_REG_AP);
}

static void page_register(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    kj_top_bar(s, KJ_STR_REG_TITLE, KJ_C_GOLD);
    if (m->conn == KJ_CONN_DIRECT) {
        page_register_direct(m);
        return;
    }
    if (m->reg_state == KH_REG_INVALID) {   // 还没连上电脑服务：没有网址可扫
        radar(s, 132);
        lv_obj_t *t = kj_text_at(s, &kj_zh18, KJ_C_TEXT, KJ_STR_REG_NO_HUB, LV_ALIGN_TOP_MID, 0, 212);
        kj_pulse(t, 1600);
        kj_text_at(s, &kj_zh14, KJ_C_MUTED, KJ_STR_REG_NO_HUB2, LV_ALIGN_TOP_MID, 0, 240);
        kj_text_at(s, &kj_zh14, net_color(m->net), net_text(m->net), LV_ALIGN_TOP_MID, 0, 262);
    } else if (m->reg_state == KH_REG_DONE) {
        welcome(s, m->my_name, KJ_STR_REG_DONE);
    } else {
        qr_box(s, 42, 36, 156, m->qr);
        if (m->reg_state == KH_REG_OPENED) {
            lv_obj_t *t = kj_text_at(s, &kj_zh18, KJ_C_GOLD, KJ_STR_REG_OPENED, LV_ALIGN_TOP_MID, 0, 202);
            kj_pulse(t, 1200);
        } else {
            kj_text_at(s, &kj_zh14, KJ_C_TEXT, KJ_STR_REG_SCAN, LV_ALIGN_TOP_MID, 0, 202);
        }
        // 扫不了码时手动输入的网址：主机:端口 一行，路径一行
        kj_text_at(s, &kj_zh14, KJ_C_MUTED, m->line1, LV_ALIGN_TOP_MID, 0, 224);
        kj_text_at(s, &kj_zh14, KJ_C_MUTED, m->line2, LV_ALIGN_TOP_MID, 0, 242);
        kj_text_at(s, &kj_zh14, KJ_C_DIM, KJ_STR_REG_SCAN2, LV_ALIGN_TOP_MID, 0, 264);
    }
    kj_footer(s, KJ_STR_HINT_REG);
}

// ---------------------------------------------------------------------------
// 热点页：设备开热点 → 手机网页里选 Wi-Fi（配网），或填写昵称（直连模式登记）
// ---------------------------------------------------------------------------
static void page_provision(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    bool name = m->prov_kind == KJ_PROV_KIND_NAME;
    kj_top_bar(s, name ? KJ_STR_REG_TITLE : KJ_STR_PROV_TITLE, KJ_C_GOLD);
    if (name && m->prov_state == KJ_PROV_OK) {   // 昵称已保存，马上重启回游戏
        welcome(s, m->my_name, KJ_STR_NAP_DONE);
        return;
    }
    qr_box(s, 56, 34, 128, m->qr);
    kj_text_at(s, &kj_zh14, KJ_C_TEXT, KJ_STR_PROV_STEP1, LV_ALIGN_TOP_MID, 0, 168);
    char buf[KJ_UI_TEXT_LEN + 16];
    snprintf(buf, sizeof(buf), KJ_STR_PROV_AP_FMT, m->line1);
    kj_text_at(s, &kj_zh18, KJ_C_GOLD, buf, LV_ALIGN_TOP_LEFT, 22, 188);
    snprintf(buf, sizeof(buf), KJ_STR_PROV_PASS_FMT, m->prov_state == KJ_PROV_TRYING ? "" : m->line2);
    if (m->prov_state != KJ_PROV_TRYING) kj_text_at(s, &kj_zh18, KJ_C_GOLD, buf, LV_ALIGN_TOP_LEFT, 22, 210);
    const char *status = NULL;
    uint32_t color = KJ_C_GOLD;
    switch (m->prov_state) {
    case KJ_PROV_PHONE_IN: status = name ? KJ_STR_NAP_PHONE_IN : KJ_STR_PROV_PHONE_IN; break;
    case KJ_PROV_TRYING:
        snprintf(buf, sizeof(buf), KJ_STR_PROV_TRYING_FMT, m->line2);
        status = buf;
        break;
    case KJ_PROV_OK: status = KJ_STR_PROV_OK; color = KJ_C_GREEN; break;
    case KJ_PROV_FAILED: status = KJ_STR_PROV_FAILED; color = KJ_C_RED; break;
    default: break;
    }
    if (status) {
        lv_obj_t *t = kj_text_box(s, &kj_font_name, color, status, 16, 240, 208, LV_TEXT_ALIGN_CENTER);
        if (m->prov_state == KJ_PROV_TRYING || m->prov_state == KJ_PROV_PHONE_IN) kj_pulse(t, 1200);
    } else {
        kj_text_at(s, &kj_zh14, KJ_C_TEXT, name ? KJ_STR_NAP_STEP2 : KJ_STR_PROV_STEP2, LV_ALIGN_TOP_MID, 0, 236);
        kj_text_at(s, &kj_zh14, KJ_C_MUTED, KJ_STR_PROV_STEP2B, LV_ALIGN_TOP_MID, 0, 256);
    }
    kj_footer(s, name ? KJ_STR_HINT_NAP : KJ_STR_HINT_PROV);
}

// ---------------------------------------------------------------------------
// 找赌局
// ---------------------------------------------------------------------------
static const char *phase_text(uint8_t phase)
{
    return phase == KJ_PHASE_RUNNING ? KJ_STR_PHASE_RUN
         : phase == KJ_PHASE_ENDED ? KJ_STR_PHASE_ENDED : KJ_STR_PHASE_LOBBY;
}

static uint32_t phase_color(uint8_t phase)
{
    return phase == KJ_PHASE_RUNNING ? KJ_C_RED : phase == KJ_PHASE_ENDED ? KJ_C_BLUE : KJ_C_GOLD;
}

static void page_rooms(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    kj_top_bar(s, NULL, 0);
    kj_text_at(s, &kj_zh26, KJ_C_TEXT, KJ_STR_ROOMS_TITLE, LV_ALIGN_TOP_MID, 0, 40);
    kj_text_at(s, &kj_zh14, KJ_C_MUTED, KJ_STR_ROOMS_SUB, LV_ALIGN_TOP_MID, 0, 76);
    bool direct = m->conn == KJ_CONN_DIRECT;
    if (direct && m->net == KJ_NET_NO_RADIO) {
        radar(s, 170);
        kj_text_at(s, &kj_zh18, KJ_C_RED, KJ_STR_NET_NO_RADIO, LV_ALIGN_TOP_MID, 0, 238);
        kj_text_at(s, &kj_zh14, KJ_C_MUTED, KJ_STR_RADIO_RETRY, LV_ALIGN_TOP_MID, 0, 264);
    } else if (!direct && m->net != KJ_NET_OK) {
        radar(s, 170);
        lv_obj_t *t = kj_text_at(s, &kj_zh18, KJ_C_TEXT, KJ_STR_REG_NO_HUB, LV_ALIGN_TOP_MID, 0, 238);
        kj_pulse(t, 1600);
        kj_text_at(s, &kj_zh14, KJ_C_MUTED, m->net == KJ_NET_OLD ? KJ_STR_T_OLD_FW : KJ_STR_ROOMS_NO_HUB2,
                   LV_ALIGN_TOP_MID, 0, 264);
    } else if (m->room_count == 0) {
        radar(s, 170);
        lv_obj_t *t = kj_text_at(s, &kj_zh18, KJ_C_TEXT, KJ_STR_ROOMS_EMPTY, LV_ALIGN_TOP_MID, 0, 238);
        kj_pulse(t, 1600);
        kj_text_at(s, &kj_zh14, KJ_C_MUTED, direct ? KJ_STR_ROOMS_DIRECT2 : KJ_STR_ROOMS_EMPTY2,
                   LV_ALIGN_TOP_MID, 0, 264);
    } else {
        int first = m->room_sel >= 4 ? m->room_sel - 3 : 0;
        for (int i = first; i < m->room_count && i < first + 4; i++) {
            const kj_room_entry_t *r = &m->rooms[i];
            bool sel = i == m->room_sel;
            int y = 102 + (i - first) * 46;
            lv_obj_t *row = kj_box(s, 18, y, 204, 40, sel ? 0x3A1517 : KJ_C_PANEL, 10);
            lv_obj_set_style_border_width(row, sel ? 2 : 1, 0);
            lv_obj_set_style_border_color(row, lv_color_hex(sel ? KJ_C_RED : KJ_C_LINE), 0);
            char room[5], buf[40];
            snprintf(room, sizeof(room), "%04X", (unsigned)r->room);
            snprintf(buf, sizeof(buf), KJ_STR_ROOM_FMT, room);
            kj_text_at(row, &kj_zh18, KJ_C_TEXT, buf, LV_ALIGN_TOP_LEFT, 12, 1);
            snprintf(buf, sizeof(buf), KJ_STR_ROOM_LINE_FMT, phase_text(r->phase), (unsigned)r->seated);
            kj_text_at(row, &kj_zh14, KJ_C_MUTED, buf, LV_ALIGN_TOP_LEFT, 12, 21);
            kj_box(row, 182, 15, 10, 10, phase_color(r->phase), LV_RADIUS_CIRCLE);
        }
        if (m->joining) {
            lv_obj_t *j = kj_box(s, 50, 196, 140, 44, 0x2B231E, 12);
            lv_obj_set_style_border_width(j, 1, 0);
            lv_obj_set_style_border_color(j, lv_color_hex(KJ_C_GOLD), 0);
            lv_obj_t *t = kj_text(j, &kj_zh18, KJ_C_GOLD, KJ_STR_JOINING);
            lv_obj_center(t);
            kj_pulse(t, 1000);
        }
    }
    kj_footer(s, KJ_STR_HINT_ROOMS);
}

// ---------------------------------------------------------------------------
// 入座等待
// ---------------------------------------------------------------------------
static void room_top_bar(const kj_ui_model_t *m)
{
    char room[5], buf[24];
    snprintf(room, sizeof(room), "%04X", (unsigned)m->room);
    snprintf(buf, sizeof(buf), KJ_STR_ROOM_FMT, room);
    kj_top_bar(kj_ui.scr, buf, KJ_C_MUTED);
}

// 顶栏左侧："07号 小明"
static void player_top_bar(const kj_ui_model_t *m)
{
    char buf[48];
    if (m->my_name[0]) {
        snprintf(buf, sizeof(buf), KJ_STR_NO_FMT " %s", (unsigned)m->view.no, m->my_name);
    } else {
        no_text(buf, sizeof(buf), m->view.no);
    }
    kj_top_bar(kj_ui.scr, buf, KJ_C_GOLD);
}

static void page_seat(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    room_top_bar(m);
    kj_text_at(s, &kj_zh14, KJ_C_MUTED, KJ_STR_YOUR_NO, LV_ALIGN_TOP_MID, 0, 46);
    kj_badge(s, 120, 128, 112, m->view.no, KJ_C_RED, true);
    if (m->my_name[0]) kj_text_box(s, &kj_font_name, KJ_C_GOLD, m->my_name, 20, 190, 200, LV_TEXT_ALIGN_CENTER);
    char buf[32];
    snprintf(buf, sizeof(buf), KJ_STR_SEATED_FMT, (unsigned)m->seated);
    kj_text_at(s, &kj_zh18, KJ_C_TEXT, buf, LV_ALIGN_TOP_MID, 0, 220);
    bool ended = m->view.phase == KJ_PHASE_ENDED;
    lv_obj_t *t = kj_text_at(s, &kj_zh18, KJ_C_RED, ended ? KJ_STR_WAIT_NEXT : KJ_STR_WAIT_START,
                             LV_ALIGN_TOP_MID, 0, 250);
    kj_pulse(t, 1600);
    kj_footer(s, KJ_STR_HINT_SEAT);
}

// ---------------------------------------------------------------------------
// 手牌主页
// ---------------------------------------------------------------------------
static void card_row(lv_obj_t *s, const kj_view_t *v, int y)
{
    for (int c = 0; c < KJ_CARD_TYPES; c++) {
        uint32_t flags = KJ_CARD_COUNT | (v->cards[c] == 0 ? KJ_CARD_OFF : 0);
        kj_card(s, 18 + c * 70, y, CARD_ROW_W, CARD_ROW_H, (uint8_t)c, v->cards[c], flags);
    }
}

static unsigned hand_total(const kj_view_t *v)
{
    return (unsigned)v->cards[0] + v->cards[1] + v->cards[2];
}

static void page_hand(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    const kj_view_t *v = &m->view;
    player_top_bar(m);
    kj_text_at(s, &kj_zh14, KJ_C_MUTED, KJ_STR_STARS_LABEL, LV_ALIGN_TOP_MID, 0, 40);
    kj_stars(s, 56, v->stars, &kj_zh26);
    card_row(s, v, 100);
    char buf[64];
    snprintf(buf, sizeof(buf), KJ_STR_HAND_INFO_FMT, hand_total(v), v->wins, v->losses, v->draws);
    kj_text_at(s, &kj_zh14, KJ_C_MUTED, buf, LV_ALIGN_TOP_MID, 0, 204);
    kj_pill(s, 30, 230, 180, 40, KJ_STR_FIND_OPP, true, KJ_C_RED);
    snprintf(buf, sizeof(buf), KJ_STR_AVAIL_FMT, (unsigned)m->opp_count);
    kj_text_at(s, &kj_zh14, m->opp_count ? KJ_C_GOLD : KJ_C_DIM, buf, LV_ALIGN_TOP_MID, 0, 274);
    kj_footer(s, KJ_STR_HINT_HAND);
}

// ---------------------------------------------------------------------------
// 选择对手
// ---------------------------------------------------------------------------
static void page_opponents(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    player_top_bar(m);
    kj_text_at(s, &kj_zh26, KJ_C_TEXT, KJ_STR_OPP_TITLE, LV_ALIGN_TOP_MID, 0, 40);
    kj_text_at(s, &kj_zh14, KJ_C_MUTED, KJ_STR_OPP_SUB, LV_ALIGN_TOP_MID, 0, 76);
    if (m->opp_count == 0) {
        kj_text_at(s, &kj_zh18, KJ_C_TEXT, KJ_STR_OPP_EMPTY, LV_ALIGN_TOP_MID, 0, 160);
        kj_text_at(s, &kj_zh14, KJ_C_MUTED, KJ_STR_OPP_EMPTY2, LV_ALIGN_TOP_MID, 0, 188);
    } else {
        int first = m->opp_first;
        for (int i = first; i < m->opp_count && i < first + KJ_UI_OPP_ROWS; i++) {
            const kj_opponent_t *o = &m->opps[i];
            bool sel = i == m->opp_sel;
            int y = 100 + (i - first) * 46;
            lv_obj_t *row = kj_box(s, 18, y, 204, 40, sel ? 0x3A1517 : KJ_C_PANEL, 10);
            lv_obj_set_style_border_width(row, sel ? 2 : 1, 0);
            lv_obj_set_style_border_color(row, lv_color_hex(sel ? KJ_C_RED : KJ_C_LINE), 0);
            char buf[8];
            snprintf(buf, sizeof(buf), "%02u", (unsigned)o->no);
            kj_text_at(row, &kj_zh26, sel ? KJ_C_GOLD : 0xD9CDBA, buf, LV_ALIGN_LEFT_MID, 12, -1);
            const char *name = m->opp_names[i - first];
            if (o->is_bot) {
                lv_obj_t *tag = kj_box(row, 56, 10, 44, 20, 0x2E3A44, 10);
                lv_obj_center(kj_text(tag, &kj_zh14, KJ_C_BLUE, KJ_STR_BOT));
            } else if (name[0]) {
                lv_obj_t *l = kj_text_box(row, &kj_font_name, sel ? KJ_C_TEXT : 0xD9CDBA, name, 56, 9, 140,
                                          LV_TEXT_ALIGN_LEFT);
                (void)l;
            } else {
                char no[16];
                no_text(no, sizeof(no), o->no);
                kj_text_at(row, &kj_zh14, KJ_C_DIM, no, LV_ALIGN_LEFT_MID, 58, 0);
            }
        }
        char pos[12];
        snprintf(pos, sizeof(pos), "%u/%u", (unsigned)m->opp_sel + 1, (unsigned)m->opp_count);
        kj_text_at(s, &kj_zh14, KJ_C_DIM, pos, LV_ALIGN_TOP_RIGHT, -22, 80);
    }
    kj_footer(s, KJ_STR_HINT_OPP);
}

// ---------------------------------------------------------------------------
// 等待应战 / 收到挑战 / 碰拳配对（共用倒计时条）
// ---------------------------------------------------------------------------
static void countdown(lv_obj_t *s, const char *fmt, int y, uint32_t color, int range_s)
{
    kj_ui.countdown_fmt = fmt;
    kj_ui.countdown_label = kj_text_at(s, &kj_zh18, KJ_C_TEXT, "", LV_ALIGN_TOP_MID, 0, y);
    lv_obj_t *bar = lv_bar_create(s);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 180, 6);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, y + 30);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x3A2F28), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, range_s);
    lv_bar_set_value(bar, range_s, LV_ANIM_OFF);
    kj_ui.countdown_bar = bar;
}

// 对手的编号牌 + 昵称（或"电脑"标签）
static void peer_badge(lv_obj_t *s, const kj_ui_model_t *m, int cy, int d, uint32_t ring, int name_y)
{
    kj_badge(s, 120, cy, d, m->view.peer_no, ring, true);
    if (m->view.peer_is_bot) {
        lv_obj_t *tag = kj_box(s, 100, name_y, 40, 20, 0x2E3A44, 10);
        lv_obj_center(kj_text(tag, &kj_zh14, KJ_C_BLUE, KJ_STR_BOT));
    } else if (m->peer_name[0]) {
        kj_text_box(s, &kj_font_name, KJ_C_TEXT, m->peer_name, 20, name_y, 200, LV_TEXT_ALIGN_CENTER);
    }
}

static void page_wait(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    player_top_bar(m);
    kj_text_at(s, &kj_zh26, KJ_C_TEXT, KJ_STR_WAIT_TITLE, LV_ALIGN_TOP_MID, 0, 40);
    peer_badge(s, m, 134, 100, KJ_C_GOLD, 190);
    countdown(s, KJ_STR_WAIT_FMT, 222, KJ_C_GOLD, KJ_CHALLENGE_TIMEOUT_MS / 1000);
    kj_footer(s, KJ_STR_HINT_WAIT);
}

static void page_challenged(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    player_top_bar(m);
    lv_obj_t *title = kj_text_at(s, &kj_big48, KJ_C_TEXT, KJ_BIG_CHALLENGE, LV_ALIGN_TOP_MID, 0, 32);
    kj_pulse(title, 900);
    kj_badge(s, 120, 128, 84, m->view.peer_no, KJ_C_RED, true);
    char buf[64];
    if (m->view.peer_is_bot) {
        snprintf(buf, sizeof(buf), "%s", KJ_STR_CHAL_BOT);
    } else {
        char who[40];
        who_text(who, sizeof(who), m->view.peer_no, m->peer_name, false);
        snprintf(buf, sizeof(buf), "%s %s", who, KJ_STR_CHAL_FROM);
    }
    kj_text_box(s, &kj_font_name, KJ_C_TEXT, buf, 16, 178, 208, LV_TEXT_ALIGN_CENTER);
    countdown(s, KJ_STR_CHAL_LEFT_FMT, 206, KJ_C_RED, KJ_CHALLENGE_TIMEOUT_MS / 1000);
    kj_pill(s, 22, 256, 100, 30, KJ_STR_ACCEPT, true, KJ_C_GOLD);
    kj_pill(s, 128, 256, 90, 30, KJ_STR_DECLINE, false, 0);
}

// ---------------------------------------------------------------------------
// 碰拳：两只拳头对撞，等庄家配对；配对成功后显示对手并倒计时
// ---------------------------------------------------------------------------
static void page_bump(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    player_top_bar(m);
    lv_obj_t *big = kj_text_at(s, &kj_big48, KJ_C_RED, KJ_BIG_BUMP, LV_ALIGN_TOP_MID, 0, 34);
    kj_pulse(big, 700);
    kj_card(s, 22, 100, 84, 112, KJ_ROCK, 0, KJ_CARD_BIG);
    kj_card(s, 134, 100, 84, 112, KJ_ROCK, 0, KJ_CARD_BIG);
    lv_obj_t *t = kj_text_at(s, &kj_zh18, KJ_C_TEXT, KJ_STR_BUMP_WAIT, LV_ALIGN_TOP_MID, 0, 228);
    kj_pulse(t, 1200);
    kj_text_at(s, &kj_zh14, KJ_C_MUTED, KJ_STR_BUMP_SUB, LV_ALIGN_TOP_MID, 0, 256);
    kj_footer(s, KJ_STR_HINT_BUMP);
}

static void page_matched(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    player_top_bar(m);
    kj_text_at(s, &kj_zh26, KJ_C_GOLD, KJ_STR_MATCH_TITLE, LV_ALIGN_TOP_MID, 0, 38);
    kj_text_at(s, &kj_zh14, KJ_C_MUTED, KJ_STR_MATCH_PEER, LV_ALIGN_TOP_MID, 0, 72);
    peer_badge(s, m, 136, 92, KJ_C_GOLD, 188);
    countdown(s, KJ_STR_MATCH_FMT, 218, KJ_C_RED, KJ_MATCH_COUNTDOWN_MS / 1000);
    kj_footer(s, KJ_STR_HINT_MATCH);
}

// ---------------------------------------------------------------------------
// 出牌
// ---------------------------------------------------------------------------
static void page_choose(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    const kj_view_t *v = &m->view;
    char buf[64];
    if (m->peer_name[0] && !v->peer_is_bot) {
        snprintf(buf, sizeof(buf), KJ_STR_DUEL_VS_NAME_FMT, m->peer_name);
    } else {
        snprintf(buf, sizeof(buf), KJ_STR_DUEL_VS_FMT, v->peer_no);
    }
    kj_top_bar(s, buf, KJ_C_TEXT);
    // 对手状态
    lv_obj_t *chip = kj_box(s, 50, 40, 140, 26, v->peer_locked ? 0x3B2E12 : KJ_C_PANEL, 13);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(v->peer_locked ? KJ_C_GOLD : KJ_C_LINE), 0);
    lv_obj_t *ct = kj_text(chip, &kj_zh14, v->peer_locked ? KJ_C_GOLD : KJ_C_MUTED,
                           v->peer_locked ? KJ_STR_OPP_LOCKED : KJ_STR_OPP_THINKING);
    lv_obj_center(ct);
    if (!v->peer_locked) kj_pulse(ct, 1200);

    bool locked = v->my_lock != KJ_CARD_NONE;
    kj_text_at(s, &kj_zh18, locked ? KJ_C_GOLD : KJ_C_TEXT, locked ? KJ_STR_LOCKED_WAIT : KJ_STR_PICK_PROMPT,
               LV_ALIGN_TOP_MID, 0, 76);
    for (int c = 0; c < KJ_CARD_TYPES; c++) {
        uint32_t flags = KJ_CARD_COUNT;
        if (locked) {
            flags = c == v->my_lock ? (KJ_CARD_BACK | KJ_CARD_SEL) : (KJ_CARD_OFF | KJ_CARD_COUNT);
        } else {
            if (v->cards[c] == 0) flags |= KJ_CARD_OFF;
            if (c == m->card_sel) flags |= KJ_CARD_SEL;
        }
        kj_card(s, 14 + c * 74, 116, 66, 100, (uint8_t)c, v->cards[c], flags);
    }
    uint8_t shown = locked ? v->my_lock : m->card_sel;
    if (shown < KJ_CARD_TYPES) {
        snprintf(buf, sizeof(buf), KJ_STR_CARD_LEFT_FMT, kj_card_name(shown), (unsigned)v->cards[shown]);
        kj_text_at(s, &kj_zh18, locked ? KJ_C_MUTED : KJ_C_TEXT, buf, LV_ALIGN_TOP_MID, 0, 232);
    }
    kj_footer(s, locked ? KJ_STR_HINT_LOCKED : KJ_STR_HINT_CHOOSE);
}

// ---------------------------------------------------------------------------
// 亮牌
// ---------------------------------------------------------------------------
static void page_reveal(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    const kj_view_t *v = &m->view;
    player_top_bar(m);
    char buf[40];
    kj_text_box(s, &kj_zh14, KJ_C_MUTED, KJ_STR_ME, 18, 42, 84, LV_TEXT_ALIGN_CENTER);
    who_text(buf, sizeof(buf), v->res_opp_no, m->peer_name, false);
    kj_text_box(s, &kj_font_name, KJ_C_MUTED, buf, 128, 38, 104, LV_TEXT_ALIGN_CENTER);
    uint8_t out = v->res_outcome;
    kj_card(s, 18, 64, 84, 112, v->res_my, 0, KJ_CARD_BIG | KJ_CARD_NAME |
            (out == KJ_OUT_LOSE ? KJ_CARD_OFF : 0));
    kj_card(s, 138, 64, 84, 112, v->res_opp, 0, KJ_CARD_BIG | KJ_CARD_NAME |
            (out == KJ_OUT_WIN ? KJ_CARD_OFF : 0));
    kj_text_at(s, &kj_zh26, KJ_C_DIM, KJ_BIG_VS, LV_ALIGN_TOP_MID, 0, 104);

    uint32_t color = out == KJ_OUT_WIN ? KJ_C_GOLD : out == KJ_OUT_LOSE ? KJ_C_RED : KJ_C_BLUE;
    const char *big = out == KJ_OUT_WIN ? KJ_BIG_WIN : out == KJ_OUT_LOSE ? KJ_BIG_LOSE : KJ_BIG_DRAW;
    const char *delta = out == KJ_OUT_WIN ? KJ_STR_STAR_PLUS : out == KJ_OUT_LOSE ? KJ_STR_STAR_MINUS
                                                                                 : KJ_STR_STAR_SAME;
    lv_obj_t *band = kj_box(s, 0, 186, 240, 62, out == KJ_OUT_LOSE ? 0x2A0C0E : 0x221C12, 0);
    lv_obj_set_style_border_side(band, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(band, 2, 0);
    lv_obj_set_style_border_color(band, lv_color_hex(color), 0);
    kj_text_at(band, &kj_big48, color, big, LV_ALIGN_LEFT_MID, 52, -2);
    kj_text_at(band, &kj_zh26, color, delta, LV_ALIGN_LEFT_MID, 118, 0);
    kj_stars(s, 254, v->stars, &kj_zh18);
    kj_footer(s, KJ_STR_HINT_REVEAL);
}

// ---------------------------------------------------------------------------
// 终局
// ---------------------------------------------------------------------------
static void page_final(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    const kj_view_t *v = &m->view;
    player_top_bar(m);
    uint32_t color;
    const char *big;
    char sub[64];
    if (v->status == KJ_ST_CLEARED) {
        color = KJ_C_GOLD;
        big = KJ_BIG_CLEARED;
        snprintf(sub, sizeof(sub), KJ_STR_CLEARED_FMT, (unsigned)v->stars);
    } else if (v->status == KJ_ST_ELIMINATED) {
        color = KJ_C_RED;
        big = KJ_BIG_OUT;
        snprintf(sub, sizeof(sub), "%s", KJ_STR_OUT_SUB);
    } else {
        color = 0xB0453F;
        big = KJ_BIG_FAILED;
        snprintf(sub, sizeof(sub), "%s",
                 v->final_reason == KJ_FINAL_TIME_UP ? KJ_STR_FAIL_TIMEUP : KJ_STR_FAIL_NOCARD);
    }
    // 印章：双圈 + 大字
    kj_frame(s, 50, 50, 140, 140, color, 4, LV_RADIUS_CIRCLE);
    kj_frame(s, 60, 60, 120, 120, color, 1, LV_RADIUS_CIRCLE);
    kj_text_at(s, &kj_big48, color, big, LV_ALIGN_TOP_MID, 0, 88);
    kj_text_at(s, &kj_zh18, KJ_C_TEXT, sub, LV_ALIGN_TOP_MID, 0, 204);
    kj_stars(s, 230, v->stars, &kj_zh26);
    char rec[48];
    snprintf(rec, sizeof(rec), KJ_STR_RECORD_FMT, v->wins, v->losses, v->draws);
    kj_text_at(s, &kj_zh14, KJ_C_MUTED, rec, LV_ALIGN_TOP_MID, 0, 266);
    kj_footer(s, KJ_STR_FINAL_WAIT);
}

// ---------------------------------------------------------------------------
// 庄家面板（只显示汇总，不显示任何人的手牌）
// ---------------------------------------------------------------------------
static const char *host_item_text(int item)
{
    static const char *const names[KJ_HM_COUNT] = {
        KJ_STR_M_START, KJ_STR_M_END, KJ_STR_M_NEW, KJ_STR_M_BOT_ADD, KJ_STR_M_BOT_DEL, KJ_STR_M_RESET,
        KJ_STR_M_ROSTER,
    };
    return item >= 0 && item < KJ_HM_COUNT ? names[item] : "";
}

static void stat_cell(lv_obj_t *s, int x, int value, const char *label)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", value);
    lv_obj_t *cell = kj_box(s, x, 144, 46, 52, KJ_C_PANEL, 8);
    kj_text_at(cell, &kj_zh26, KJ_C_TEXT, buf, LV_ALIGN_TOP_MID, 0, 2);
    kj_text_at(cell, &kj_zh14, KJ_C_MUTED, label, LV_ALIGN_BOTTOM_MID, 0, -3);
}

static void board_footer(lv_obj_t *s, const kj_ui_model_t *m)
{
    char buf[48];
    const char *text;
    if (m->board == KJ_BOARD_WIFI) {
        char ip[16];
        kj_ip_text(m->hub_ip, ip);
        snprintf(buf, sizeof(buf), KJ_STR_BOARD_WIFI_FMT, ip);
        text = buf;
    } else if (m->board == KJ_BOARD_USB) {
        text = KJ_STR_BOARD_USB;
    } else if (m->conn == KJ_CONN_DIRECT) {
        text = KJ_STR_BOARD_DIRECT;
    } else {
        text = m->net == KJ_NET_NO_WIFI ? KJ_STR_BOARD_NO_WIFI : KJ_STR_BOARD_SEARCH;
    }
    kj_text_at(s, &kj_zh14, m->board ? KJ_C_GREEN : KJ_C_MUTED, text, LV_ALIGN_TOP_MID, 0, 294);
}

static void page_host(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    kj_top_bar(s, KJ_STR_HOST_TAG, KJ_C_GOLD);
    kj_text_at(s, &kj_zh14, KJ_C_MUTED, KJ_STR_HOST_ROOM, LV_ALIGN_TOP_MID, 0, 30);
    char room[5];
    snprintf(room, sizeof(room), "%04X", (unsigned)m->host_room);
    kj_text_at(s, &kj_num56, KJ_C_TEXT, room, LV_ALIGN_TOP_MID, 0, 50);
    uint32_t pc = phase_color(m->host_phase);
    lv_obj_t *chip = kj_box(s, 44, 112, 152, 24, 0x1E1714, 12);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(pc), 0);
    kj_text_at(chip, &kj_zh14, pc, phase_text(m->host_phase), LV_ALIGN_LEFT_MID, 14, 0);
    kj_ui.clock_label = kj_text_at(chip, &kj_zh14, KJ_C_MUTED, "", LV_ALIGN_RIGHT_MID, -14, 0);
    const kj_summary_t *sum = &m->host_sum;
    stat_cell(s, 20, sum->seated, KJ_STR_STAT_SEATED);
    stat_cell(s, 72, sum->online, KJ_STR_STAT_ONLINE);
    stat_cell(s, 124, sum->in_duel, KJ_STR_STAT_DUELS);
    stat_cell(s, 176, sum->cleared + sum->eliminated + sum->failed, KJ_STR_STAT_DONE);

    // 菜单：显示选中项及其前后各一项
    for (int k = -1; k <= 1; k++) {
        int item = (m->host_sel + k + KJ_HM_COUNT) % KJ_HM_COUNT;
        bool sel = k == 0;
        bool on = (m->host_enabled >> item) & 1u;
        int y = 210 + (k + 1) * 26;
        if (sel) {
            lv_obj_t *row = kj_box(s, 20, y - 2, 200, 26, on ? KJ_C_RED : 0x3A2F28, 8);
            (void)row;
        }
        kj_text_at(s, sel ? &kj_zh18 : &kj_zh14, sel ? (on ? KJ_C_TEXT : KJ_C_MUTED) : (on ? 0xBDB09C : KJ_C_DIM),
                   host_item_text(item), LV_ALIGN_TOP_MID, 0, y + (sel ? 0 : 3));
    }
    board_footer(s, m);

    if (m->host_confirm >= 0) {
        lv_obj_t *dlg = kj_box(s, 22, 112, 196, 104, 0x241C18, 14);
        lv_obj_set_style_border_width(dlg, 2, 0);
        lv_obj_set_style_border_color(dlg, lv_color_hex(KJ_C_RED), 0);
        char buf[48];
        snprintf(buf, sizeof(buf), KJ_STR_CONFIRM_FMT, host_item_text(m->host_confirm));
        kj_text_at(dlg, &kj_zh18, KJ_C_TEXT, buf, LV_ALIGN_TOP_MID, 0, 22);
        kj_text_at(dlg, &kj_zh14, KJ_C_MUTED, KJ_STR_CONFIRM_HINT, LV_ALIGN_TOP_MID, 0, 62);
    }
}

// ---------------------------------------------------------------------------
// 庄家：选手名单（昵称、状态、星星、剩余手牌；不显示分类牌数，牌数在原作里也是秘密）
// ---------------------------------------------------------------------------
static const char *status_word(uint8_t st)
{
    static const char *const words[KJ_ST_COUNT] = {
        KJ_STR_ST_WAITING, KJ_STR_ST_IDLE, KJ_STR_ST_CHALLENGING, KJ_STR_ST_CHALLENGED, KJ_STR_ST_DUEL,
        KJ_STR_ST_CLEARED, KJ_STR_ST_OUT, KJ_STR_ST_FAILED, KJ_STR_ST_BUMPING, KJ_STR_ST_MATCHED,
    };
    return st < KJ_ST_COUNT ? words[st] : "";
}

static uint32_t status_color(uint8_t st)
{
    switch (st) {
    case KJ_ST_IDLE: return KJ_C_GREEN;
    case KJ_ST_DUEL: case KJ_ST_MATCHED: case KJ_ST_CHALLENGING: case KJ_ST_CHALLENGED: case KJ_ST_BUMPING:
        return KJ_C_GOLD;
    case KJ_ST_CLEARED: return KJ_C_GOLD;
    case KJ_ST_ELIMINATED: case KJ_ST_FAILED: return KJ_C_RED;
    default: return KJ_C_MUTED;
    }
}

static void page_host_roster(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    kj_top_bar(s, KJ_STR_HOST_TAG, KJ_C_GOLD);
    kj_text_at(s, &kj_zh26, KJ_C_TEXT, KJ_STR_ROSTER_TITLE, LV_ALIGN_TOP_LEFT, 20, 36);
    char buf[48];
    snprintf(buf, sizeof(buf), KJ_STR_ROSTER_FMT, (unsigned)m->roster_total);
    kj_text_at(s, &kj_zh14, KJ_C_MUTED, buf, LV_ALIGN_TOP_RIGHT, -22, 46);
    if (m->roster_count == 0) {
        kj_text_at(s, &kj_zh18, KJ_C_MUTED, KJ_STR_ROSTER_EMPTY, LV_ALIGN_TOP_MID, 0, 160);
    }
    for (int i = 0; i < m->roster_count; i++) {
        const kj_roster_row_t *r = &m->roster[i];
        int y = 74 + i * 42;
        lv_obj_t *row = kj_box(s, 14, y, 212, 38, KJ_C_PANEL, 8);
        if (!r->online && !r->is_bot) lv_obj_set_style_bg_opa(row, LV_OPA_50, 0);
        snprintf(buf, sizeof(buf), "%02u", (unsigned)r->no);
        kj_text_at(row, &kj_zh18, KJ_C_GOLD, buf, LV_ALIGN_TOP_LEFT, 8, 0);
        char who[40];
        who_text(who, sizeof(who), r->no, r->name, r->is_bot);
        kj_text_box(row, &kj_font_name, r->online ? KJ_C_TEXT : KJ_C_DIM, who, 38, 0, 108, LV_TEXT_ALIGN_LEFT);
        kj_text_at(row, &kj_zh14, r->online ? status_color(r->status) : KJ_C_DIM,
                   r->online ? status_word(r->status) : KJ_STR_OFFLINE, LV_ALIGN_TOP_RIGHT, -8, 2);
        snprintf(buf, sizeof(buf), "%s%u  " KJ_STR_ROSTER_CARDS_FMT, KJ_STR_STAR, (unsigned)r->stars,
                 (unsigned)r->cards);
        kj_text_at(row, &kj_zh14, KJ_C_MUTED, buf, LV_ALIGN_TOP_LEFT, 38, 20);
    }
    if (m->roster_total > KJ_ROSTER_ROWS) {
        unsigned last = m->roster_first + m->roster_count;
        snprintf(buf, sizeof(buf), "%u-%u/%u", (unsigned)m->roster_first + 1, last, (unsigned)m->roster_total);
        kj_text_at(s, &kj_zh14, KJ_C_DIM, buf, LV_ALIGN_TOP_RIGHT, -22, 282);
    }
    kj_footer(s, KJ_STR_HINT_ROSTER);
}

void kj_ui_build_page(const kj_ui_model_t *m)
{
    switch (m->page) {
    case KJ_PAGE_TITLE: page_title(m); break;
    case KJ_PAGE_ROOMS: page_rooms(m); break;
    case KJ_PAGE_SEAT: page_seat(m); break;
    case KJ_PAGE_HAND: page_hand(m); break;
    case KJ_PAGE_OPPONENTS: page_opponents(m); break;
    case KJ_PAGE_WAIT: page_wait(m); break;
    case KJ_PAGE_CHALLENGED: page_challenged(m); break;
    case KJ_PAGE_CHOOSE: page_choose(m); break;
    case KJ_PAGE_REVEAL: page_reveal(m); break;
    case KJ_PAGE_FINAL: page_final(m); break;
    case KJ_PAGE_HOST: page_host(m); break;
    case KJ_PAGE_BUMP: page_bump(m); break;
    case KJ_PAGE_MATCHED: page_matched(m); break;
    case KJ_PAGE_SETTINGS: page_settings(m); break;
    case KJ_PAGE_REGISTER: page_register(m); break;
    case KJ_PAGE_PROVISION: page_provision(m); break;
    case KJ_PAGE_HOST_ROSTER: page_host_roster(m); break;
    default: page_title(m); break;
    }
}
