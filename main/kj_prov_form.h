// main/kj_prov_form.h —— 热点网页的输入处理：配网表单与（直连模式的）昵称表单的解析与校验、
// 扫描结果转 JSON（防 XSS 转义）。纯 C，主机测试见 tests/test_kj_prov_form.c。
#pragma once

#include "kj_proto.h"   // KJ_NAME_MAX、KJ_NAME_MAX_UNITS

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KJ_PROV_FORM_MAX 256   // 表单正文上限（ssid + pass + hub，URL 编码后）

typedef struct {
    char ssid[33];
    char pass[65];
    uint32_t hub_ip;   // 可选的电脑地址（IPv4，网络字节序）；0 = 自动寻找
} kj_prov_form_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t secure;    // 需要密码
} kj_ap_rec_t;

// 解析 application/x-www-form-urlencoded 正文（字段 ssid、pass、hub）。
// 失败返回 false，*err 为出错字段："ssid" / "pass" / "hub" / "form"。
bool kj_prov_form_parse(const char *body, size_t n, kj_prov_form_t *out, const char **err);
// "a.b.c.d" → IPv4（网络字节序）；不合法返回 false。
bool kj_prov_parse_ip(const char *s, uint32_t *ip);
// 扫描结果 → JSON 数组 [{"s":"…","r":-60,"l":1},…]：去掉隐藏网络、同名只留信号最强的、按信号排序；
// SSID 里的引号、反斜杠、控制字符、尖括号和 & 都转义成 \uXXXX，非法 UTF-8 换成 �。
// 放不下的条目整条丢掉。返回长度（不含结尾 0）。
size_t kj_prov_scan_json(const kj_ap_rec_t *aps, int n, char *out, size_t cap);
// 把任意字符串写成 JSON 字符串的内容（不含两边的引号），转义规则同上。放不下返回 false（out 置空）。
bool kj_prov_json_text(const char *s, char *out, size_t cap);

// ---- 昵称表单（直连模式：手机连上设备热点后在网页里填写）----
#define KJ_NAME_FORM_MAX 256   // URL 编码后的正文上限
#define KJ_NAME_BAD_MAX  8     // 最多列出几个显示不了的字

// 昵称字库里有没有这个字（直连模式由字体回退链判断，与 tools/kj_charset.py 的字符集一致）。
typedef bool (*kj_glyph_fn)(void *ctx, uint32_t cp);
// 解析并校验昵称表单（字段 name），规则与电脑服务的登记页一致：
//   连续空白（含全角空格）合成一个半角空格并去掉首尾；不能有控制字符或看不见的格式字符；
//   每个字都要在昵称字库里（has_glyph）；最多 KJ_NAME_MAX 字节、显示宽度最多 KJ_NAME_MAX_UNITS。
// 成功返回 true 并写入 out。失败返回 false，*err 为
//   "empty"（没填）/ "ctrl"（控制字符）/ "chars"（有显示不了的字：bad 里是这些字，去重、空格分隔、
//   最多 KJ_NAME_BAD_MAX 个）/ "long"（太长）/ "form"（缺字段或编码不对）。
bool kj_name_form_parse(const char *body, size_t n, kj_glyph_fn has_glyph, void *ctx, char out[KJ_NAME_MAX + 1],
                        const char **err, char *bad, size_t bad_cap);
