// main/kj_fonts.h —— 限定猜拳的字体（tools/gen_kj_fonts.py 生成的子集）与配色。
#pragma once

#include "lvgl.h"

#include <stdbool.h>

LV_FONT_DECLARE(kj_zh14)    // 思源黑体 Regular 14：提示、小字
LV_FONT_DECLARE(kj_zh18)    // 思源黑体 Bold 18：正文、列表、按钮
LV_FONT_DECLARE(kj_zh26)    // 思源黑体 Heavy 26：页面标题、星星
LV_FONT_DECLARE(kj_big48)   // 思源黑体 Heavy 48：只含 KJ_BIG_* 的大字
LV_FONT_DECLARE(kj_num56)   // 思源黑体 Heavy 56：0-9 A-F -
LV_FONT_DECLARE(kj_hand36)  // Noto Emoji Bold 36：✊✌✋
LV_FONT_DECLARE(kj_hand64)  // Noto Emoji Bold 64：✊✌✋
LV_FONT_DECLARE(kj_name18a) // 思源黑体 Bold 18：昵称字库前半（ASCII + GB2312 汉字 + 人名补充字，见 tools/kj_charset.py）
LV_FONT_DECLARE(kj_name18b) // 思源黑体 Bold 18：昵称字库后半（单个字体的位图不能超过 1 MB，所以分成两个）

// 显示昵称用的字体：kj_zh18 的可写副本，缺字时依次回退到 kj_name18a、kj_name18b。kj_fonts_init() 之后可用。
extern lv_font_t kj_font_name;
void kj_fonts_init(void);
// 昵称字库（kj_name18a → kj_name18b，字符集即 tools/kj_charset.py 的昵称字符集）里有没有这个字。
// 查字形会写字体里的查找缓存：在 LVGL 任务之外调用时必须持有 bsp_lvgl_lock()。kj_fonts_init() 之后可用。
bool kj_fonts_name_has(uint32_t codepoint);

// 配色：赌场暗底 + 原作红 + 星星金。
#define KJ_C_BG        0x0D0B0A
#define KJ_C_BG2       0x1C1512
#define KJ_C_PANEL     0x211B17
#define KJ_C_LINE      0x3D332B
#define KJ_C_TEXT      0xF3EBDD
#define KJ_C_MUTED     0x9C8F7E
#define KJ_C_DIM       0x5E544A
#define KJ_C_RED       0xD0232C
#define KJ_C_RED_DARK  0x5A1014
#define KJ_C_GOLD      0xF2C230
#define KJ_C_GREEN     0x3FB56B
#define KJ_C_BLUE      0x7F9CB3
#define KJ_C_CARD      0xF2E7CF
#define KJ_C_CARD_INK  0x1E1915
#define KJ_C_CARD_BACK 0xA9161E

// 检查 kj_font_glyphs.h 中每个码点在对应字体里都有真实字形（非占位框）。
// 返回缺字数量；missing_cb 非空时逐个回调（字体名、码点）。
typedef void (*kj_font_missing_cb_t)(const char *font, uint32_t codepoint);
int kj_fonts_selfcheck(kj_font_missing_cb_t missing_cb);
