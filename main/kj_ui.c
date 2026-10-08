// main/kj_ui.c —— 限定猜拳 LVGL 界面：控件工具 + 渲染调度 + 局部刷新。各页面见 kj_ui_pages.c。
#include "kj_ui.h"
#include "kj_ui_internal.h"

#include <stdio.h>
#include <string.h>

kj_ui_state_t kj_ui;

// ---------------------------------------------------------------------------
// 控件工具
// ---------------------------------------------------------------------------

lv_obj_t *kj_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

lv_obj_t *kj_frame(lv_obj_t *parent, int x, int y, int w, int h, uint32_t border, int width, int radius)
{
    lv_obj_t *o = kj_box(parent, x, y, w, h, 0, radius);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(o, width, 0);
    return o;
}

lv_obj_t *kj_text(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *s)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, s);
    return l;
}

lv_obj_t *kj_text_at(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *s,
                     lv_align_t align, int x, int y)
{
    lv_obj_t *l = kj_text(parent, font, color, s);
    lv_obj_align(l, align, x, y);
    return l;
}

lv_obj_t *kj_text_box(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *s,
                      int x, int y, int w, lv_text_align_t align)
{
    lv_obj_t *l = kj_text(parent, font, color, s);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(l, align, 0);
    return l;
}

const char *kj_card_name(uint8_t card)
{
    switch (card) {
    case KJ_ROCK: return KJ_STR_CARD_ROCK;
    case KJ_SCISSORS: return KJ_STR_CARD_SCISSORS;
    case KJ_PAPER: return KJ_STR_CARD_PAPER;
    default: return "";
    }
}

const char *kj_card_glyph(uint8_t card)
{
    switch (card) {
    case KJ_ROCK: return KJ_HAND_ROCK;
    case KJ_SCISSORS: return KJ_HAND_SCISSORS;
    case KJ_PAPER: return KJ_HAND_PAPER;
    default: return "";
    }
}

// 牌：象牙白牌面 + 手势；选中时红框上浮；用完变暗；背面是红底菱形纹。
lv_obj_t *kj_card(lv_obj_t *parent, int x, int y, int w, int h, uint8_t card, int count, uint32_t flags)
{
    bool back = flags & KJ_CARD_BACK;
    bool off = flags & KJ_CARD_OFF;
    bool sel = flags & KJ_CARD_SEL;
    bool big = flags & KJ_CARD_BIG;
    if (sel) y -= 8;
    uint32_t face = back ? KJ_C_CARD_BACK : off ? 0x3A332D : KJ_C_CARD;
    lv_obj_t *c = kj_box(parent, x, y, w, h, face, 10);
    lv_obj_set_style_border_width(c, sel ? 3 : 2, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(sel ? KJ_C_RED : off ? 0x4A4038 : 0x0F0C0A), 0);
    if (sel) {
        // 选中牌下方一道红色底光，暗示"抬起"
        lv_obj_t *glow = kj_box(parent, x + 8, y + h + 4, w - 16, 3, KJ_C_RED, 2);
        (void)glow;
    }
    if (back) {
        lv_obj_t *inner = kj_frame(c, 6, 6, w - 12, h - 12, 0xE8C9A0, 1, 6);
        lv_obj_t *dia = kj_frame(inner, 0, 0, (w - 12) / 2, (w - 12) / 2, 0xE8C9A0, 2, 4);
        lv_obj_center(dia);
        lv_obj_t *mark = kj_text(inner, &kj_zh18, 0xF6E7C8, KJ_STR_STAR);
        lv_obj_center(mark);
        return c;
    }
    const lv_font_t *hf = big ? &kj_hand64 : &kj_hand36;
    lv_obj_t *g = kj_text(c, hf, off ? 0x6B6158 : KJ_C_CARD_INK, kj_card_glyph(card));
    lv_obj_align(g, LV_ALIGN_TOP_MID, 0, big ? h / 2 - 50 : 6);
    if (flags & KJ_CARD_COUNT) {
        char buf[12];
        snprintf(buf, sizeof(buf), KJ_STR_COUNT_FMT, (unsigned)count);
        lv_obj_t *n = kj_text(c, &kj_zh18, off ? 0x6B6158 : KJ_C_CARD_INK, buf);
        lv_obj_align(n, LV_ALIGN_BOTTOM_MID, 0, -6);
    }
    if (flags & KJ_CARD_NAME) {
        lv_obj_t *n = kj_text(c, &kj_zh14, off ? 0x6B6158 : 0x6E5F50, kj_card_name(card));
        lv_obj_align(n, LV_ALIGN_BOTTOM_MID, 0, -6);
    }
    return c;
}

void kj_stars(lv_obj_t *parent, int y, unsigned stars, const lv_font_t *font)
{
    char buf[48];
    if (stars == 0) {
        snprintf(buf, sizeof(buf), "%s", KJ_STR_STAR_EMPTY);
    } else if (stars <= 6) {
        buf[0] = '\0';
        for (unsigned i = 0; i < stars; i++) strncat(buf, KJ_STR_STAR, sizeof(buf) - strlen(buf) - 1);
    } else {
        snprintf(buf, sizeof(buf), "%s" KJ_STR_COUNT_FMT, KJ_STR_STAR, stars);
    }
    kj_text_at(parent, font, stars ? KJ_C_GOLD : KJ_C_DIM, buf, LV_ALIGN_TOP_MID, 0, y);
}

// 圆形编号牌：红环 + 大号数字
lv_obj_t *kj_badge(lv_obj_t *parent, int cx, int cy, int d, unsigned no, uint32_t ring, bool big)
{
    lv_obj_t *b = kj_box(parent, cx - d / 2, cy - d / 2, d, d, 0x16110E, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(b, big ? 4 : 2, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(ring), 0);
    char buf[8];
    snprintf(buf, sizeof(buf), "%02u", no);
    lv_obj_t *n = kj_text(b, big ? &kj_num56 : &kj_zh18, KJ_C_TEXT, buf);
    lv_obj_center(n);
    return b;
}

lv_obj_t *kj_pill(lv_obj_t *parent, int x, int y, int w, int h, const char *s, bool filled, uint32_t color)
{
    lv_obj_t *p = kj_box(parent, x, y, w, h, color, h / 2);
    if (!filled) {
        lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(p, 1, 0);
        lv_obj_set_style_border_color(p, lv_color_hex(0x5A4D42), 0);
    }
    lv_obj_t *t = kj_text(p, &kj_zh18, filled ? (color == KJ_C_GOLD ? 0x1A1410 : KJ_C_TEXT) : 0xC9BCA8, s);
    lv_obj_center(t);
    return p;
}

void kj_footer(lv_obj_t *scr, const char *s)
{
    kj_text_at(scr, &kj_zh14, KJ_C_MUTED, s, LV_ALIGN_TOP_MID, 0, 294);
}

// 顶栏：左侧标签（可能含昵称）+ 右上角电量（电量在局部刷新里更新）。
void kj_top_bar(lv_obj_t *scr, const char *left, uint32_t left_color)
{
    if (left && left[0]) kj_text_box(scr, &kj_font_name, left_color, left, 24, 11, 150, LV_TEXT_ALIGN_LEFT);
    kj_ui.bat_label = kj_text(scr, &kj_zh14, KJ_C_MUTED, KJ_STR_BATTERY_NA);
    lv_obj_align(kj_ui.bat_label, LV_ALIGN_TOP_RIGHT, -50, 13);
    lv_obj_t *body = kj_frame(scr, 192, 16, 24, 12, 0x9C8F7E, 1, 3);
    kj_box(scr, 216, 20, 2, 4, 0x9C8F7E, 1);
    kj_ui.bat_fill = kj_box(body, 2, 2, 0, 8, KJ_C_GREEN, 1);
    kj_ui.bat_shown = -2;
}

void kj_pulse(lv_obj_t *obj, uint32_t period_ms)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_30);
    lv_anim_set_duration(&a, period_ms / 2);
    lv_anim_set_reverse_duration(&a, period_ms / 2);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)kj_set_text_opa);
    lv_anim_start(&a);
}

void kj_set_text_opa(lv_obj_t *obj, int32_t v)
{
    lv_obj_set_style_text_opa(obj, (lv_opa_t)v, 0);
}

const char *kj_ui_toast_text(uint8_t toast)
{
    switch (toast) {
    case KJ_TOAST_DECLINED: return KJ_STR_T_DECLINED;
    case KJ_TOAST_CANCELLED: return KJ_STR_T_CANCELLED;
    case KJ_TOAST_TIMEOUT: return KJ_STR_T_TIMEOUT;
    case KJ_TOAST_WITHDRAWN: return KJ_STR_T_WITHDRAWN;
    case KJ_TOAST_ABORTED: return KJ_STR_T_ABORTED;
    case KJ_TOAST_BUSY: return KJ_STR_T_BUSY;
    case KJ_TOAST_NOT_RUNNING: return KJ_STR_T_NOT_RUN;
    case KJ_TOAST_NO_CARD: return KJ_STR_T_NO_CARD;
    case KJ_TOAST_INVALID: return KJ_STR_T_INVALID;
    case KJ_TOAST_FULL: return KJ_STR_T_FULL;
    case KJ_TOAST_KICKED: return KJ_STR_T_KICKED;
    case KJ_TOAST_NO_REPLY: return KJ_STR_T_NO_REPLY;
    case KJ_TOAST_RESTORED: return KJ_STR_T_RESTORED;
    case KJ_TOAST_NEED_TWO: return KJ_STR_T_NEED_TWO;
    case KJ_TOAST_DONE: return KJ_STR_T_DONE;
    case KJ_TOAST_RADIO_FAIL: return KJ_STR_T_RADIO_FAIL;
    case KJ_TOAST_BUMP_ALONE: return KJ_STR_T_BUMP_ALONE;
    case KJ_TOAST_BUMP_CROWD: return KJ_STR_T_BUMP_CROWD;
    case KJ_TOAST_MATCH_CANCELLED: return KJ_STR_T_MATCH_CANCEL;
    case KJ_TOAST_NAME_UPDATED: return KJ_STR_T_NAME_UPDATED;
    case KJ_TOAST_NEED_HUB: return KJ_STR_T_NEED_HUB;
    case KJ_TOAST_OLD_FW: return KJ_STR_T_OLD_FW;
    case KJ_TOAST_NO_WIFI: return KJ_STR_T_NO_WIFI;
    case KJ_TOAST_RESTARTING: return KJ_STR_T_RESTARTING;
    default: return NULL;
    }
}

static void build_overlays(const kj_ui_model_t *m)
{
    lv_obj_t *scr = kj_ui.scr;
    if (m->disconnected) {
        lv_obj_t *bar = kj_box(scr, 20, 38, 200, 24, KJ_C_RED_DARK, 12);
        lv_obj_set_style_border_width(bar, 1, 0);
        lv_obj_set_style_border_color(bar, lv_color_hex(KJ_C_RED), 0);
        lv_obj_t *t = kj_text(bar, &kj_zh14, KJ_C_TEXT, KJ_STR_T_LOST);
        lv_obj_center(t);
        kj_pulse(t, 1400);
    }
    const char *toast = kj_ui_toast_text(m->toast);
    if (toast) {
        lv_obj_t *box = kj_box(scr, 18, 246, 204, 38, 0x2B231E, 12);
        lv_obj_set_style_border_width(box, 1, 0);
        lv_obj_set_style_border_color(box, lv_color_hex(KJ_C_GOLD), 0);
        lv_obj_t *t = kj_text(box, &kj_zh14, KJ_C_TEXT, toast);
        lv_obj_set_width(t, 192);
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(t);
    }
}

// ---------------------------------------------------------------------------
// 签名：只对会改变版面的字段取哈希（倒计时、电量、时间不参与）
// ---------------------------------------------------------------------------

static uint32_t fnv(uint32_t h, const void *data, size_t n)
{
    const uint8_t *p = data;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t signature(const kj_ui_model_t *m)
{
    static kj_ui_model_t s;   // 约 1 KB，避免占用 LVGL / 应用任务栈
    s = *m;
    s.now_ms = 0;
    s.battery = 0;
    s.deadline_s = 0;
    s.host_phase_s = 0;
    for (int i = 0; i < KJ_CLIENT_MAX_ROOMS; i++) s.rooms[i].seen_ms = 0;
    // 视图里只有倒计时会频繁变化
    s.view.deadline_s = 0;
    return fnv(2166136261u, &s, sizeof(s));
}

// ---------------------------------------------------------------------------
// 局部刷新
// ---------------------------------------------------------------------------

static void update_battery(int battery)
{
    if (!kj_ui.bat_label || battery == kj_ui.bat_shown) return;
    kj_ui.bat_shown = battery;
    if (battery < 0) {
        lv_label_set_text(kj_ui.bat_label, KJ_STR_BATTERY_NA);
        lv_obj_set_width(kj_ui.bat_fill, 0);
        return;
    }
    lv_label_set_text_fmt(kj_ui.bat_label, "%d", battery);
    int w = battery * 20 / 100;
    lv_obj_set_width(kj_ui.bat_fill, w < 1 ? 1 : w);
    uint32_t color = battery < 20 ? KJ_C_RED : battery < 50 ? KJ_C_GOLD : KJ_C_GREEN;
    lv_obj_set_style_bg_color(kj_ui.bat_fill, lv_color_hex(color), 0);
}

static void update_dynamic(const kj_ui_model_t *m)
{
    update_battery(m->battery);
    if (kj_ui.countdown_label && m->deadline_s != kj_ui.countdown_shown) {
        kj_ui.countdown_shown = m->deadline_s;
        lv_label_set_text_fmt(kj_ui.countdown_label, kj_ui.countdown_fmt, (unsigned)m->deadline_s);
        if (kj_ui.countdown_bar) {
            lv_bar_set_value(kj_ui.countdown_bar, m->deadline_s, LV_ANIM_ON);
        }
    }
    if (kj_ui.clock_label && m->host_phase_s != kj_ui.clock_shown) {
        kj_ui.clock_shown = m->host_phase_s;
        unsigned s = (unsigned)m->host_phase_s;
        if (s >= 3600) {
            lv_label_set_text_fmt(kj_ui.clock_label, "%u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60);
        } else {
            lv_label_set_text_fmt(kj_ui.clock_label, "%02u:%02u", s / 60, s % 60);
        }
    }
}

// ---------------------------------------------------------------------------
// 调度
// ---------------------------------------------------------------------------

void kj_ui_init(void)
{
    memset(&kj_ui, 0, sizeof(kj_ui));
    kj_ui.scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(kj_ui.scr);
    lv_obj_remove_flag(kj_ui.scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(kj_ui.scr, LV_OPA_COVER, 0);
    lv_screen_load(kj_ui.scr);
}

void kj_ui_render(const kj_ui_model_t *m)
{
    uint32_t sig = signature(m);
    if (!kj_ui.built || sig != kj_ui.sig) {
        lv_obj_clean(kj_ui.scr);
        kj_ui.bat_label = kj_ui.bat_fill = NULL;
        kj_ui.countdown_label = kj_ui.countdown_bar = kj_ui.clock_label = NULL;
        kj_ui.countdown_shown = 0xFF;
        kj_ui.clock_shown = 0xFFFFFFFFu;
        // 纯色底：RGB565 下的暗色渐变会出现明显色带。
        bool alert = m->page == KJ_PAGE_CHALLENGED;
        lv_obj_set_style_bg_color(kj_ui.scr, lv_color_hex(alert ? 0x2E0A0D : KJ_C_BG), 0);
        kj_ui_build_page(m);
        build_overlays(m);
        kj_ui.sig = sig;
        kj_ui.built = true;
        kj_ui.page = m->page;
    }
    update_dynamic(m);
}
