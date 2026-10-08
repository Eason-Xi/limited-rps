// main/kj_fonts.c —— 昵称字体的回退链，以及字体覆盖自检：开机时把生成的码点表逐一对照实际字体（LVGL API）。
#include "kj_fonts.h"
#include "kj_font_glyphs.h"

#include <string.h>

lv_font_t kj_font_name;
static lv_font_t s_name_a;   // kj_name18a 的可写副本：回退到 kj_name18b

void kj_fonts_init(void)
{
    // 正文子集（界面文字的字形与正文一致）→ 昵称字库前半 → 后半。
    memcpy(&s_name_a, &kj_name18a, sizeof(s_name_a));
    s_name_a.fallback = &kj_name18b;
    memcpy(&kj_font_name, &kj_zh18, sizeof(kj_font_name));
    kj_font_name.fallback = &s_name_a;
}

static bool has_glyph(const lv_font_t *font, uint32_t cp)
{
    lv_font_glyph_dsc_t dsc = { 0 };
    return lv_font_get_glyph_dsc(font, &dsc, cp, 0) && !dsc.is_placeholder;
}

bool kj_fonts_name_has(uint32_t codepoint)
{
    return has_glyph(&s_name_a, codepoint);
}

static int check_set(const char *name, const lv_font_t *font, const uint32_t *cps, int n,
                     kj_font_missing_cb_t cb)
{
    int missing = 0;
    for (int i = 0; i < n; i++) {
        if (!has_glyph(font, cps[i])) {
            missing++;
            if (cb) cb(name, cps[i]);
        }
    }
    return missing;
}

int kj_fonts_selfcheck(kj_font_missing_cb_t cb)
{
    int missing = 0;
    missing += check_set("kj_zh14", &kj_zh14, KJ_GLYPHS_TEXT, KJ_GLYPHS_TEXT_COUNT, cb);
    missing += check_set("kj_zh18", &kj_zh18, KJ_GLYPHS_TEXT, KJ_GLYPHS_TEXT_COUNT, cb);
    missing += check_set("kj_zh26", &kj_zh26, KJ_GLYPHS_TEXT, KJ_GLYPHS_TEXT_COUNT, cb);
    missing += check_set("kj_big48", &kj_big48, KJ_GLYPHS_BIG, KJ_GLYPHS_BIG_COUNT, cb);
    missing += check_set("kj_num56", &kj_num56, KJ_GLYPHS_NUM, KJ_GLYPHS_NUM_COUNT, cb);
    missing += check_set("kj_hand36", &kj_hand36, KJ_GLYPHS_HAND, KJ_GLYPHS_HAND_COUNT, cb);
    missing += check_set("kj_hand64", &kj_hand64, KJ_GLYPHS_HAND, KJ_GLYPHS_HAND_COUNT, cb);
    missing += check_set("kj_font_name", &kj_font_name, KJ_GLYPHS_NAME_SAMPLE, KJ_GLYPHS_NAME_SAMPLE_COUNT, cb);
    // 反例：一个肯定不在子集里的字（U+9F98 龘）必须查不到，否则自检本身失效。
    if (has_glyph(&kj_zh18, 0x9F98) || has_glyph(&kj_font_name, 0x9F98)) {
        missing++;
        if (cb) cb("selfcheck-negative", 0x9F98);
    }
    return missing;
}
