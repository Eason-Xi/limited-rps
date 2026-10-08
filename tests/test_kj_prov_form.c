// tests/test_kj_prov_form.c —— 配网表单：URL 解码、各字段长度边界、可选的电脑地址、错误字段；
// 扫描结果 JSON：去重、排序、隐藏网络、转义（引号 / 反斜杠 / 控制字符 / 尖括号 / 非法 UTF-8）、容量不足；
// 昵称表单（直连模式的热点登记）：空白规整、控制字符、字库外的字、长度上限、编码错误。
#include "kj_prov_form.h"
#include "kj_test.h"

#include <string.h>

static bool parse(const char *body, kj_prov_form_t *f, const char **err)
{
    return kj_prov_form_parse(body, strlen(body), f, err);
}

static void test_form(void)
{
    kj_prov_form_t f;
    const char *err = NULL;
    CHECK(parse("ssid=Home+WiFi&pass=12345678", &f, &err));
    CHECK(strcmp(f.ssid, "Home WiFi") == 0);
    CHECK(strcmp(f.pass, "12345678") == 0);
    CHECK_EQ(f.hub_ip, 0);
    CHECK(err == NULL);
    // %XX（含中文 UTF-8）、字段顺序无关、未知字段忽略
    CHECK(parse("x=1&pass=a%26b%3Dc%25d123&ssid=%E5%AE%B6", &f, &err));
    CHECK(strcmp(f.ssid, "\xE5\xAE\xB6") == 0);
    CHECK(strcmp(f.pass, "a&b=c%d123") == 0);
    // 开放网络：空密码
    CHECK(parse("ssid=Cafe&pass=", &f, &err));
    CHECK_EQ(f.pass[0], '\0');
    CHECK(parse("ssid=Cafe", &f, &err));
    // 可选的电脑地址
    CHECK(parse("ssid=A&pass=12345678&hub=192.168.1.20", &f, &err));
    uint8_t b[4];
    memcpy(b, &f.hub_ip, 4);
    CHECK(b[0] == 192 && b[1] == 168 && b[2] == 1 && b[3] == 20);
    CHECK(parse("ssid=A&hub=", &f, &err));   // 空 = 自动寻找
    CHECK_EQ(f.hub_ip, 0);

    // 错误：缺 ssid / ssid 太长 / 密码太短太长 / 64 位但不是十六进制 / 地址不合法 / 编码不合法 / 正文太长
    CHECK(!parse("pass=12345678", &f, &err));
    CHECK(strcmp(err, "ssid") == 0);
    CHECK(!parse("ssid=", &f, &err));
    CHECK(strcmp(err, "ssid") == 0);
    CHECK(!parse("ssid=123456789012345678901234567890123", &f, &err));   // 33 字节
    CHECK(parse("ssid=12345678901234567890123456789012", &f, &err));     // 32 字节
    CHECK(!parse("ssid=A&pass=1234567", &f, &err));
    CHECK(strcmp(err, "pass") == 0);
    CHECK(parse("ssid=A&pass=123456789012345678901234567890123456789012345678901234567890123", &f, &err));
    char hex64[96] = "ssid=A&pass=";
    for (int i = 0; i < 64; i++) strcat(hex64, "a");
    CHECK(parse(hex64, &f, &err));
    hex64[strlen(hex64) - 1] = 'z';
    CHECK(!parse(hex64, &f, &err));
    CHECK(!parse("ssid=A&hub=300.1.1.1", &f, &err));
    CHECK(strcmp(err, "hub") == 0);
    CHECK(!parse("ssid=A&hub=127.0.0.1", &f, &err));
    CHECK(!parse("ssid=A&hub=1.2.3", &f, &err));
    CHECK(!parse("ssid=A&hub=1.2.3.4x", &f, &err));
    CHECK(!parse("ssid=%E5%AE", &f, &err));     // 截断的 UTF-8
    CHECK(!parse("ssid=%ZZ", &f, &err));
    CHECK(!parse("ssid=A%2", &f, &err));
    char big[KJ_PROV_FORM_MAX + 8];
    memset(big, 'a', sizeof(big) - 1);
    memcpy(big, "ssid=", 5);
    big[sizeof(big) - 1] = '\0';
    CHECK(!parse(big, &f, &err));
    CHECK(strcmp(err, "form") == 0);
    CHECK(!kj_prov_form_parse(NULL, 3, &f, &err));
}

static void test_scan_json(void)
{
    kj_ap_rec_t aps[] = {
        { "Weak", -80, 1 },
        { "", -30, 1 },                     // 隐藏网络
        { "Home", -50, 1 },
        { "Home", -40, 1 },                 // 同名：取更强的
        { "Open \"Cafe\"", -60, 0 },
        { "a\\b<c>&\x01", -70, 1 },
        { "bad\xFF", -90, 0 },
    };
    char out[512];
    size_t n = kj_prov_scan_json(aps, (int)(sizeof(aps) / sizeof(aps[0])), out, sizeof(out));
    CHECK_EQ(n, strlen(out));
    CHECK(strcmp(out,
                 "[{\"s\":\"Home\",\"r\":-40,\"l\":1},"
                 "{\"s\":\"Open \\u0022Cafe\\u0022\",\"r\":-60,\"l\":0},"
                 "{\"s\":\"a\\u005cb\\u003cc\\u003e\\u0026\\u0001\",\"r\":-70,\"l\":1},"
                 "{\"s\":\"Weak\",\"r\":-80,\"l\":1},"
                 "{\"s\":\"bad\\ufffd\",\"r\":-90,\"l\":0}]") == 0);
    // 容量不足：只保留放得下的完整条目
    char small[40];
    n = kj_prov_scan_json(aps, 7, small, sizeof(small));
    CHECK(strcmp(small, "[{\"s\":\"Home\",\"r\":-40,\"l\":1}]") == 0);
    CHECK_EQ(n, strlen(small));
    char tiny[8];
    CHECK_EQ(kj_prov_scan_json(aps, 7, tiny, sizeof(tiny)), 2);
    CHECK(strcmp(tiny, "[]") == 0);
    CHECK_EQ(kj_prov_scan_json(aps, 0, out, sizeof(out)), 2);
}

// 假的昵称字库：可打印 ASCII、间隔号和几个汉字（小 明 林 雨）
static bool fake_glyph(void *ctx, uint32_t cp)
{
    int *calls = ctx;
    if (calls) (*calls)++;
    return (cp >= 0x20 && cp < 0x7F) || cp == 0xB7 || cp == 0x5C0F || cp == 0x660E || cp == 0x6797 || cp == 0x96E8;
}

static bool name_parse(const char *body, char out[KJ_NAME_MAX + 1], const char **err, char *bad, size_t cap)
{
    return kj_name_form_parse(body, strlen(body), fake_glyph, NULL, out, err, bad, cap);
}

#define XM "%E5%B0%8F%E6%98%8E"   // 小明（URL 编码）

static void test_name_form(void)
{
    char out[KJ_NAME_MAX + 1], bad[64];
    const char *err = NULL;
    CHECK(name_parse("name=" XM, out, &err, bad, sizeof(bad)));
    CHECK(strcmp(out, "\xE5\xB0\x8F\xE6\x98\x8E") == 0);
    CHECK(err == NULL);
    CHECK(name_parse("x=1&name=Amy&y=2", out, &err, bad, sizeof(bad)));   // 手机多带的字段不影响
    CHECK(strcmp(out, "Amy") == 0);
    // 空白：连续空白合成一个空格、去掉首尾（含全角空格、制表符、换行）
    CHECK(name_parse("name=+++Amy+++Lee++", out, &err, bad, sizeof(bad)));
    CHECK(strcmp(out, "Amy Lee") == 0);
    CHECK(name_parse("name=%E3%80%80" XM "%09%0A", out, &err, bad, sizeof(bad)));
    CHECK(strcmp(out, "\xE5\xB0\x8F\xE6\x98\x8E") == 0);
    CHECK(name_parse("name=A%C2%B7B", out, &err, bad, sizeof(bad)));     // 外文名的间隔号
    CHECK(strcmp(out, "A\xC2\xB7" "B") == 0);
    // 空
    CHECK(!name_parse("name=", out, &err, bad, sizeof(bad)));
    CHECK(strcmp(err, "empty") == 0);
    CHECK(!name_parse("name=+%E3%80%80+", out, &err, bad, sizeof(bad)));
    CHECK(strcmp(err, "empty") == 0);
    CHECK_EQ(out[0], '\0');
    // 缺字段 / 编码错误 / 太长的正文
    CHECK(!name_parse("nick=Amy", out, &err, bad, sizeof(bad)));
    CHECK(strcmp(err, "form") == 0);
    CHECK(!name_parse("name=%ZZ", out, &err, bad, sizeof(bad)));
    CHECK(strcmp(err, "form") == 0);
    CHECK(!name_parse("name=%E5%B0", out, &err, bad, sizeof(bad)));     // 半个汉字
    CHECK(strcmp(err, "form") == 0);
    char big[KJ_NAME_FORM_MAX + 8];
    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    memcpy(big, "name=", 5);
    CHECK(!name_parse(big, out, &err, bad, sizeof(bad)));
    CHECK(strcmp(err, "form") == 0);
    CHECK(!kj_name_form_parse(NULL, 4, fake_glyph, NULL, out, &err, bad, sizeof(bad)));
    // 控制字符与看不见的格式字符
    CHECK(!name_parse("name=a%01b", out, &err, bad, sizeof(bad)));
    CHECK(strcmp(err, "ctrl") == 0);
    CHECK(!name_parse("name=a%E2%80%8Bb", out, &err, bad, sizeof(bad)));   // 零宽空格
    CHECK(strcmp(err, "ctrl") == 0);
    CHECK(!name_parse("name=%EF%BB%BFAmy", out, &err, bad, sizeof(bad)));  // BOM
    CHECK(strcmp(err, "ctrl") == 0);
    // 字库外的字：去重、按出现顺序列出
    CHECK(!name_parse("name=%E5%B0%8F%E9%BE%98%F0%9F%98%80%E9%BE%98", out, &err, bad, sizeof(bad)));   // 小龘😀龘
    CHECK(strcmp(err, "chars") == 0);
    CHECK(strcmp(bad, "\xE9\xBE\x98 \xF0\x9F\x98\x80") == 0);
    // 最多列 KJ_NAME_BAD_MAX 个
    CHECK(!name_parse("name=%E4%B8%80%E4%B8%81%E4%B8%82%E4%B8%83%E4%B8%84%E4%B8%85%E4%B8%86%E4%B8%87%E4%B8%88",
                      out, &err, bad, sizeof(bad)));
    CHECK(strcmp(err, "chars") == 0);
    int spaces = 0;
    for (const char *c = bad; *c; c++) spaces += *c == ' ';
    CHECK_EQ(spaces, KJ_NAME_BAD_MAX - 1);
    // bad 缓冲区很小时不越界
    char tiny[4];
    CHECK(!name_parse("name=%E9%BE%98%E9%BE%99", out, &err, tiny, sizeof(tiny)));
    CHECK(strcmp(tiny, "\xE9\xBE\x98") == 0);
    CHECK(!name_parse("name=%E9%BE%98", out, &err, NULL, 0));   // 不要 bad 也行
    // 长度：8 个汉字 / 16 个字母可以，再多一个就太长
    CHECK(name_parse("name=" XM XM XM XM, out, &err, bad, sizeof(bad)));
    CHECK_EQ(strlen(out), 24);
    CHECK(!name_parse("name=" XM XM XM XM "%E5%B0%8F", out, &err, bad, sizeof(bad)));
    CHECK(strcmp(err, "long") == 0);
    CHECK(name_parse("name=abcdefghijklmnop", out, &err, bad, sizeof(bad)));
    CHECK(!name_parse("name=abcdefghijklmnopq", out, &err, bad, sizeof(bad)));
    CHECK(strcmp(err, "long") == 0);
    CHECK(!name_parse("name=" XM "abcdefghijklm", out, &err, bad, sizeof(bad)));   // 2 + 2 + 13 = 17
    CHECK(strcmp(err, "long") == 0);
    // 没有字库判断时一律当作显示不了（不会把未校验的昵称放进来）
    CHECK(!kj_name_form_parse("name=Amy", 8, NULL, NULL, out, &err, bad, sizeof(bad)));
    CHECK(strcmp(err, "chars") == 0);
    int calls = 0;
    CHECK(kj_name_form_parse("name=Amy", 8, fake_glyph, &calls, out, &err, bad, sizeof(bad)));
    CHECK(calls >= 3);   // ctx 原样传给字库判断

    // JSON 文本转义（网页里显示设备上的昵称、显示不了的字）
    char json[32];
    CHECK(kj_prov_json_text("a\"b\\c<", json, sizeof(json)));
    CHECK(strcmp(json, "a\\u0022b\\u005cc\\u003c") == 0);
    CHECK(kj_prov_json_text("\xE5\xB0\x8F", json, sizeof(json)));
    CHECK(strcmp(json, "\xE5\xB0\x8F") == 0);
    CHECK(!kj_prov_json_text("abcdefgh", json, 4));
    CHECK_EQ(json[0], '\0');
    CHECK(kj_prov_json_text(NULL, json, sizeof(json)));
    CHECK_EQ(json[0], '\0');
}

int main(void)
{
    test_form();
    test_scan_json();
    test_name_form();
    KJ_TEST_DONE("test_kj_prov_form");
}
