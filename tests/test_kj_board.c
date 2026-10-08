// tests/test_kj_board.c —— 看板行协议：各类行的格式、JSON 结构、命令解析的边界。
#include "kj_board.h"
#include "kj_test.h"

#include <string.h>

// 粗检 JSON：以 "@KJ {" 开头、以 "}\n" 结尾、引号成对（字符串里的 \" 不算）、花括号 / 方括号配平。
static int json_ok(const char *line)
{
    if (strncmp(line, "@KJ {", 5) != 0) return 0;
    size_t n = strlen(line);
    if (n < 7 || line[n - 1] != '\n' || line[n - 2] != '}') return 0;
    int depth = 0, quotes = 0;
    for (size_t i = 4; i < n - 1; i++) {
        char c = line[i];
        if (quotes && c == '\\') {
            i++;   // 跳过被转义的字符
            continue;
        }
        if (c == '"') quotes ^= 1;
        if (quotes) continue;
        if (c == '{' || c == '[') depth++;
        if (c == '}' || c == ']') depth--;
        if (depth < 0) return 0;
    }
    return depth == 0 && quotes == 0;
}

static void test_lines(void)
{
    static kj_game_t g;
    char buf[KJ_BOARD_LINE_MAX];
    kj_rules_init(&g);
    uint8_t m1[6] = { 0x24, 0x6F, 0x28, 0xAB, 0xCD, 0xEF };
    uint8_t m2[6] = { 0x24, 0x6F, 0x28, 0x01, 0x02, 0x03 };
    int a = kj_rules_join(&g, m1, 0);
    int b = kj_rules_join(&g, m2, 0);
    int bot = kj_rules_add_bot(&g, 0);
    kj_rules_start(&g, 100);
    kj_rules_seen(&g, a, -42, 100);
    kj_rules_seen(&g, b, -60, 100);
    kj_rules_challenge(&g, a, kj_no_of(b), 200);
    kj_rules_respond(&g, b, true, 300);
    kj_rules_play(&g, a, KJ_SCISSORS, 400);

    uint8_t host[6] = { 0x24, 0x6F, 0x28, 0x11, 0xA3, 0xF2 };
    size_t n = kj_board_hello_line(0xA3F2, "1.0.0", host, "wifi", buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(json_ok(buf));
    CHECK(strstr(buf, "\"room\":\"A3F2\"") != NULL);
    CHECK(strstr(buf, "\"max\":128") != NULL);
    CHECK(strstr(buf, "\"proto\":2") != NULL);
    CHECK(strstr(buf, "\"mac\":\"246f2811a3f2\"") != NULL);
    CHECK(strstr(buf, "\"link\":\"wifi\"") != NULL);
    CHECK(kj_board_hello_line(0xA3F2, "1.2.0", host, "espnow", buf, sizeof(buf)) > 0);   // 直连模式
    CHECK(strstr(buf, "\"link\":\"espnow\"") != NULL);
    CHECK(kj_board_hello_line(1, NULL, NULL, NULL, buf, sizeof(buf)) > 0);
    CHECK(json_ok(buf));

    n = kj_board_player_line(&g, a, 400, NULL, buf, sizeof(buf));
    CHECK(n > 0 && n == strlen(buf));
    CHECK(json_ok(buf));
    CHECK(strstr(buf, "\"no\":1,") != NULL);
    CHECK(strstr(buf, "\"st\":\"duel\"") != NULL);
    CHECK(strstr(buf, "\"lock\":\"s\"") != NULL);     // 看板能看到暗牌
    CHECK(strstr(buf, "\"peer\":2") != NULL);
    CHECK(strstr(buf, "\"rssi\":-42") != NULL);
    CHECK(strstr(buf, "\"id\":\"abcdef\"") != NULL);
    CHECK(strstr(buf, "\"mac\":\"246f28abcdef\"") != NULL);   // hub 据此把座位对上登记的昵称
    CHECK(n < KJ_BOARD_LINE_MAX - 40);                           // 留有余量
    CHECK(strstr(buf, "\"r\":4,\"s\":4,\"p\":4") != NULL);   // 未亮牌前不扣牌
    CHECK(strstr(buf, "\"name\"") == NULL);                    // 电脑服务模式：昵称由 hub 提供

    // 直连模式：看板行带上选手设备广播的昵称（JSON 转义）
    n = kj_board_player_line(&g, a, 400, "\xE5\xB0\x8F\xE6\x98\x8E", buf, sizeof(buf));   // 小明
    CHECK(json_ok(buf));
    CHECK(strstr(buf, "\"name\":\"\xE5\xB0\x8F\xE6\x98\x8E\"}\n") != NULL);
    n = kj_board_player_line(&g, a, 400, "", buf, sizeof(buf));
    CHECK(json_ok(buf));
    CHECK(strstr(buf, "\"name\":\"\"}") != NULL);
    n = kj_board_player_line(&g, a, 400, "a\"b\\c", buf, sizeof(buf));
    CHECK(json_ok(buf));
    CHECK(strstr(buf, "\"name\":\"a\\\"b\\\\c\"}") != NULL);
    // 最长的昵称（24 个要转义的字节）也放得下
    char worst[KJ_NAME_MAX + 1];
    memset(worst, '"', KJ_NAME_MAX);
    worst[KJ_NAME_MAX] = '\0';
    n = kj_board_player_line(&g, a, 400, worst, buf, sizeof(buf));
    CHECK(n > 0 && n < KJ_BOARD_LINE_MAX);
    CHECK(json_ok(buf));
    // 缓冲区放得下前半段、放不下昵称时整行作废
    CHECK_EQ(kj_board_player_line(&g, a, 400, worst, buf, n), 0);

    n = kj_board_player_line(&g, bot, 400, NULL, buf, sizeof(buf));
    CHECK(json_ok(buf));
    CHECK(strstr(buf, "\"bot\":1") != NULL);
    CHECK(strstr(buf, "\"on\":1") != NULL);

    n = kj_board_player_line(&g, 50, 400, NULL, buf, sizeof(buf));
    CHECK(json_ok(buf));
    CHECK(strstr(buf, "\"gone\":1") != NULL);
    CHECK_EQ(kj_board_player_line(&g, -1, 0, NULL, buf, sizeof(buf)), 0);
    CHECK_EQ(kj_board_player_line(&g, a, 0, NULL, buf, 20), 0);   // 缓冲区不足

    kj_rules_play(&g, b, KJ_PAPER, 500);
    n = kj_board_game_line(&g, 0xA3F2, 600, buf, sizeof(buf));
    CHECK(json_ok(buf));
    CHECK(strstr(buf, "\"phase\":\"run\"") != NULL);
    CHECK(strstr(buf, "\"seated\":3") != NULL);
    CHECK(strstr(buf, "\"cards\":[12,11,11]") != NULL);
    CHECK(strstr(buf, "\"stars\":9") != NULL);

    kj_event_t e;
    int saw_result = 0;
    while (kj_rules_pop_event(&g, &e)) {
        n = kj_board_event_line(&e, buf, sizeof(buf));
        CHECK(n > 0);
        CHECK(json_ok(buf));
        if (e.kind == KJ_EV_RESULT) {
            saw_result = 1;
            CHECK(strstr(buf, "\"k\":\"result\"") != NULL);
            CHECK(strstr(buf, "\"a\":1,\"b\":2,\"ca\":\"s\",\"cb\":\"p\",\"w\":1") != NULL);
        }
    }
    CHECK(saw_result);

    n = kj_board_ack_line(KJ_CMD_KICK, 7, KJ_N_INVALID, buf, sizeof(buf));
    CHECK(json_ok(buf));
    CHECK(strstr(buf, "\"cmd\":\"kick\",\"arg\":7,\"ok\":0,\"err\":\"invalid\"") != NULL);

    for (int st = 0; st < KJ_ST_COUNT; st++) CHECK(strcmp(kj_board_status_name((uint8_t)st), "?") != 0);
    for (int k = KJ_EV_JOIN; k <= KJ_EV_BUMP_FAIL; k++) CHECK(strcmp(kj_board_event_name((uint8_t)k), "?") != 0);
    for (int k = 0; k < KJ_N_COUNT; k++) CHECK(strcmp(kj_board_notice_name((uint8_t)k), "?") != 0);
    CHECK(strcmp(kj_board_status_name(KJ_ST_MATCHED), "matched") == 0);
    // 事件的附加值（碰拳配对的时间差）
    kj_event_t me = { .kind = KJ_EV_MATCH, .a = 3, .b = 9, .ca = KJ_CARD_NONE, .cb = KJ_CARD_NONE, .aux = 42 };
    n = kj_board_event_line(&me, buf, sizeof(buf));
    CHECK(json_ok(buf));
    CHECK(strstr(buf, "\"k\":\"match\",\"a\":3,\"b\":9") != NULL);
    CHECK(strstr(buf, "\"x\":42") != NULL);
    char rt[5];
    kj_board_room_text(0x00AF, rt);
    CHECK(strcmp(rt, "00AF") == 0);
}

static void test_commands(void)
{
    kj_cmd_t cmd;
    int arg;
    CHECK(kj_board_parse_command("@KJ start", &cmd, &arg) && cmd == KJ_CMD_START);
    CHECK(kj_board_parse_command("@KJ end\r\n", &cmd, &arg) && cmd == KJ_CMD_END);
    CHECK(kj_board_parse_command("  @KJ new  ", &cmd, &arg) && cmd == KJ_CMD_NEW_GAME);
    CHECK(kj_board_parse_command("@KJ reset", &cmd, &arg) && cmd == KJ_CMD_RESET);
    CHECK(kj_board_parse_command("@KJ bot+", &cmd, &arg) && cmd == KJ_CMD_BOT_ADD);
    CHECK(kj_board_parse_command("@KJ bot-", &cmd, &arg) && cmd == KJ_CMD_BOT_REMOVE);
    CHECK(kj_board_parse_command("@KJ sync", &cmd, &arg) && cmd == KJ_CMD_SYNC);
    CHECK(kj_board_parse_command("@KJ kick 12", &cmd, &arg) && cmd == KJ_CMD_KICK && arg == 12);
    CHECK(kj_board_parse_command("@KJ kick 128\n", &cmd, &arg) && arg == 128);
    CHECK(!kj_board_parse_command("@KJ kick", &cmd, &arg));
    CHECK(!kj_board_parse_command("@KJ kick 0", &cmd, &arg));
    CHECK(!kj_board_parse_command("@KJ kick 129", &cmd, &arg));
    CHECK(!kj_board_parse_command("@KJ kick 3x", &cmd, &arg));
    CHECK(!kj_board_parse_command("@KJ start now", &cmd, &arg));
    CHECK(!kj_board_parse_command("@KJ starts", &cmd, &arg));
    CHECK(!kj_board_parse_command("@KJ START", &cmd, &arg));
    CHECK(!kj_board_parse_command("start", &cmd, &arg));
    CHECK(!kj_board_parse_command("I (123) main: @KJ start", &cmd, &arg));
    CHECK(!kj_board_parse_command("@KJ averyveryverylongword", &cmd, &arg));
    CHECK(!kj_board_parse_command("", &cmd, &arg));
    CHECK(!kj_board_parse_command(NULL, &cmd, &arg));
}

int main(void)
{
    test_lines();
    test_commands();
    KJ_TEST_DONE("test_kj_board");
}
