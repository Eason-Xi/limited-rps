// tests/test_kj_flow.c —— 界面状态机：页面推导、按键→动作、亮牌只播一次、通知 toast、选牌跳过用完的牌、
// 庄家菜单（不可用项跳过、危险操作二次确认、选手名单翻页）、首页 / 设置 / 登记 / 配网的按键、
// 界面模型（倒计时、昵称、联网信息、名单）、碰拳页与配对页。
#include "kj_flow.h"
#include "kj_model.h"
#include "kj_test.h"

#include <string.h>

static kj_client_t c;
static kj_flow_t f;
static kj_opponent_t opps[4];
static kj_room_entry_t rooms[2];

static kj_player_ctx_t ctx(uint32_t now, int nopp)
{
    kj_player_ctx_t x = { .client = &c, .rooms = rooms, .room_count = 2, .opps = opps, .opp_count = nopp,
                          .connected = true, .now_ms = now };
    return x;
}

static kj_page_t update(uint32_t now, int nopp, kj_cue_t *cue)
{
    kj_player_ctx_t x = ctx(now, nopp);
    return kj_flow_player_update(&f, &x, cue);
}

static kj_action_t key(kj_key_t k, uint32_t now, int nopp)
{
    kj_player_ctx_t x = ctx(now, nopp);
    kj_flow_player_update(&f, &x, NULL);
    return kj_flow_player_key(&f, &x, k);
}

static void joined_view(uint8_t phase, uint8_t status)
{
    c.link = KJ_LINK_JOINED;
    c.room = 0x1234;
    c.view.no = 3;
    c.view.phase = phase;
    c.view.status = status;
    c.view.game_id = 1;
    for (int i = 0; i < 3; i++) c.view.cards[i] = 4;
    c.view.stars = 3;
    c.view.my_lock = KJ_CARD_NONE;
    c.view.res_my = c.view.res_opp = KJ_CARD_NONE;
}

static void test_title(void)
{
    kj_flow_init(&f, 1);
    CHECK_EQ(f.title_sel, 1);
    kj_action_t a = kj_flow_title_key(&f, KJ_KEY_DOWN);
    CHECK_EQ(a.kind, KJ_ACT_NONE);
    CHECK_EQ(f.title_sel, 2);
    a = kj_flow_title_key(&f, KJ_KEY_OK);
    CHECK_EQ(a.kind, KJ_ACT_SETTINGS);
    kj_flow_title_key(&f, KJ_KEY_DOWN);     // 循环回第一项
    CHECK_EQ(f.title_sel, 0);
    a = kj_flow_title_key(&f, KJ_KEY_OK);
    CHECK_EQ(a.kind, KJ_ACT_ROLE);
    CHECK_EQ(a.arg, 0);
    kj_flow_title_key(&f, KJ_KEY_UP);
    CHECK_EQ(f.title_sel, 2);
    kj_flow_init(&f, 9);                    // NVS 里的旧值越界：回到第一项
    CHECK_EQ(f.title_sel, 0);

    // 设置（直连，默认）：登记昵称 / 改用电脑服务 / 返回。▲▼ 选择，OK 选定，"返回"或长按回首页
    kj_flow_init(&f, 0);
    CHECK_EQ(f.conn, KJ_CONN_DIRECT);
    uint8_t items[KJ_SET_COUNT];
    CHECK_EQ(kj_flow_settings_items(&f, items), 3);
    CHECK_EQ(items[0], KJ_SET_NAME);
    CHECK_EQ(items[1], KJ_SET_CONN);
    CHECK_EQ(items[2], KJ_SET_BACK);
    a = kj_flow_settings_key(&f, KJ_KEY_OK);
    CHECK_EQ(a.kind, KJ_ACT_SET_ITEM);
    CHECK_EQ(a.arg, KJ_SET_NAME);
    kj_flow_settings_key(&f, KJ_KEY_DOWN);
    a = kj_flow_settings_key(&f, KJ_KEY_OK);          // 换联机方式要重启：先确认
    CHECK_EQ(a.kind, KJ_ACT_NONE);
    CHECK(f.settings_confirm);
    a = kj_flow_settings_key(&f, KJ_KEY_OK_LONG);     // 长按（或任何别的键）取消
    CHECK_EQ(a.kind, KJ_ACT_NONE);
    CHECK(!f.settings_confirm);
    CHECK_EQ(f.settings_sel, 1);
    kj_flow_settings_key(&f, KJ_KEY_OK);
    kj_flow_settings_key(&f, KJ_KEY_DOWN);
    CHECK(!f.settings_confirm);                       // ▼ 也是取消，并且不移动
    CHECK_EQ(f.settings_sel, 1);
    kj_flow_settings_key(&f, KJ_KEY_OK);
    a = kj_flow_settings_key(&f, KJ_KEY_OK);          // OK 确定
    CHECK_EQ(a.kind, KJ_ACT_SET_CONN);
    CHECK_EQ(a.arg, KJ_CONN_HUB);
    CHECK(!f.settings_confirm);
    kj_flow_settings_key(&f, KJ_KEY_DOWN);
    CHECK_EQ(kj_flow_settings_key(&f, KJ_KEY_OK).kind, KJ_ACT_BACK);
    kj_flow_settings_key(&f, KJ_KEY_DOWN);            // 循环回第一项
    CHECK_EQ(f.settings_sel, 0);
    CHECK_EQ(kj_flow_settings_key(&f, KJ_KEY_OK_LONG).kind, KJ_ACT_BACK);

    // 设置（电脑服务）：登记昵称 / 重新配网 / 改用直连 / 返回
    kj_flow_init(&f, 0);
    kj_flow_set_conn(&f, KJ_CONN_HUB);
    CHECK_EQ(kj_flow_settings_items(&f, items), 4);
    CHECK_EQ(items[1], KJ_SET_WIFI);
    CHECK_EQ(items[2], KJ_SET_CONN);
    kj_flow_settings_key(&f, KJ_KEY_DOWN);
    a = kj_flow_settings_key(&f, KJ_KEY_OK);
    CHECK_EQ(a.kind, KJ_ACT_SET_ITEM);
    CHECK_EQ(a.arg, KJ_SET_WIFI);
    kj_flow_settings_key(&f, KJ_KEY_DOWN);
    kj_flow_settings_key(&f, KJ_KEY_OK);
    a = kj_flow_settings_key(&f, KJ_KEY_OK);
    CHECK_EQ(a.kind, KJ_ACT_SET_CONN);
    CHECK_EQ(a.arg, KJ_CONN_DIRECT);
    kj_flow_settings_key(&f, KJ_KEY_UP);
    kj_flow_settings_key(&f, KJ_KEY_UP);
    kj_flow_settings_key(&f, KJ_KEY_UP);              // 循环到最后一项
    CHECK_EQ(f.settings_sel, 3);
    kj_flow_settings_open(&f, KJ_SET_WIFI);           // 没配网时直接停在"重新配网"
    CHECK_EQ(f.settings_sel, 1);
    kj_flow_set_conn(&f, KJ_CONN_DIRECT);
    kj_flow_settings_open(&f, KJ_SET_WIFI);           // 直连没有这一项：停在第一项
    CHECK_EQ(f.settings_sel, 0);
    f.settings_sel = 3;                               // 越界的选中项（项数变少了）不会选到不存在的项
    a = kj_flow_settings_key(&f, KJ_KEY_OK);
    CHECK_EQ(a.kind, KJ_ACT_SET_ITEM);
    CHECK_EQ(a.arg, KJ_SET_NAME);
    // 首页进入设置：从第一项开始，没有残留的确认框
    f.settings_confirm = true;
    f.title_sel = 2;
    CHECK_EQ(kj_flow_title_key(&f, KJ_KEY_OK).kind, KJ_ACT_SETTINGS);
    CHECK_EQ(f.settings_sel, 0);
    CHECK(!f.settings_confirm);

    // 登记：电脑服务 OK 换二维码；直连 OK 开始热点登记（会重启）；长按都是返回。热点页只有长按（跳过 / 取消）
    kj_flow_set_conn(&f, KJ_CONN_HUB);
    CHECK_EQ(kj_flow_register_key(&f, KJ_KEY_OK).kind, KJ_ACT_REG_REFRESH);
    CHECK_EQ(kj_flow_register_key(&f, KJ_KEY_OK_LONG).kind, KJ_ACT_BACK);
    CHECK_EQ(kj_flow_register_key(&f, KJ_KEY_UP).kind, KJ_ACT_NONE);
    kj_flow_set_conn(&f, KJ_CONN_DIRECT);
    CHECK_EQ(kj_flow_register_key(&f, KJ_KEY_OK).kind, KJ_ACT_REG_START);
    CHECK_EQ(kj_flow_register_key(&f, KJ_KEY_OK_LONG).kind, KJ_ACT_BACK);
    CHECK_EQ(kj_flow_register_key(&f, KJ_KEY_DOWN).kind, KJ_ACT_NONE);
    CHECK_EQ(kj_flow_provision_key(&f, KJ_KEY_OK).kind, KJ_ACT_NONE);
    CHECK_EQ(kj_flow_provision_key(&f, KJ_KEY_OK_LONG).kind, KJ_ACT_PROV_SKIP);
    kj_flow_set_conn(&f, 7);                          // NVS 里的怪值按直连处理
    CHECK_EQ(f.conn, KJ_CONN_DIRECT);
}

static void test_player_pages(void)
{
    memset(&c, 0, sizeof(c));
    kj_flow_init(&f, 0);
    rooms[0].room = 0xAAAA;
    rooms[1].room = 0xBBBB;
    kj_cue_t cue;
    CHECK_EQ(update(0, 0, &cue), KJ_PAGE_ROOMS);
    kj_action_t a = key(KJ_KEY_DOWN, 0, 0);
    CHECK_EQ(f.room_sel, 1);
    a = key(KJ_KEY_DOWN, 0, 0);
    CHECK_EQ(f.room_sel, 0);   // 循环
    a = key(KJ_KEY_UP, 0, 0);
    a = key(KJ_KEY_OK, 0, 0);
    CHECK_EQ(a.kind, KJ_ACT_JOIN);
    CHECK_EQ(a.room, 0xBBBB);
    a = key(KJ_KEY_OK_LONG, 0, 0);
    CHECK_EQ(a.kind, KJ_ACT_TO_TITLE);

    joined_view(KJ_PHASE_LOBBY, KJ_ST_WAITING);
    CHECK_EQ(update(10, 0, &cue), KJ_PAGE_SEAT);
    CHECK_EQ(cue, KJ_CUE_NONE);
    a = key(KJ_KEY_OK_LONG, 10, 0);
    CHECK_EQ(a.kind, KJ_ACT_LEAVE);

    c.view.phase = KJ_PHASE_RUNNING;
    c.view.status = KJ_ST_IDLE;
    CHECK_EQ(update(20, 2, &cue), KJ_PAGE_HAND);
    opps[0].no = 9;
    opps[1].no = 5;
    a = key(KJ_KEY_OK, 20, 2);
    CHECK_EQ(a.kind, KJ_ACT_NONE);
    CHECK_EQ(update(21, 2, NULL), KJ_PAGE_OPPONENTS);
    key(KJ_KEY_DOWN, 22, 2);
    CHECK_EQ(f.opp_no, 5);
    // 列表重新排序：选中项跟着编号走
    opps[0].no = 5;
    opps[1].no = 9;
    update(23, 2, NULL);
    CHECK_EQ(f.opp_sel, 0);
    a = key(KJ_KEY_OK, 24, 2);
    CHECK_EQ(a.kind, KJ_ACT_REQUEST);
    CHECK_EQ(a.op, KJ_OP_CHALLENGE);
    CHECK_EQ(a.arg, 5);
    // 选中的人离开空闲名单：选中项收敛到剩下的人
    opps[0].no = 9;
    CHECK_EQ(update(25, 1, NULL), KJ_PAGE_OPPONENTS);
    CHECK_EQ(f.opp_no, 9);
    a = key(KJ_KEY_OK_LONG, 26, 1);
    CHECK_EQ(update(27, 1, NULL), KJ_PAGE_HAND);

    c.view.status = KJ_ST_CHALLENGING;
    c.view.peer_no = 5;
    CHECK_EQ(update(30, 0, &cue), KJ_PAGE_WAIT);
    a = key(KJ_KEY_OK_LONG, 31, 0);
    CHECK_EQ(a.op, KJ_OP_CANCEL);

    c.view.status = KJ_ST_CHALLENGED;
    CHECK_EQ(update(40, 0, &cue), KJ_PAGE_CHALLENGED);
    CHECK_EQ(cue, KJ_CUE_ALERT);
    CHECK_EQ(key(KJ_KEY_OK, 41, 0).op, KJ_OP_ACCEPT);
    CHECK_EQ(key(KJ_KEY_OK_LONG, 41, 0).op, KJ_OP_DECLINE);

    // 出牌：默认停在剪刀；用完的牌被跳过
    c.view.status = KJ_ST_DUEL;
    c.view.cards[KJ_SCISSORS] = 0;
    CHECK_EQ(update(50, 0, &cue), KJ_PAGE_CHOOSE);
    CHECK_EQ(cue, KJ_CUE_DUEL);
    CHECK_EQ(f.card_sel, KJ_PAPER);
    key(KJ_KEY_UP, 51, 0);
    CHECK_EQ(f.card_sel, KJ_ROCK);   // 跳过剪刀
    key(KJ_KEY_UP, 52, 0);
    CHECK_EQ(f.card_sel, KJ_PAPER);
    a = key(KJ_KEY_OK, 53, 0);
    CHECK_EQ(a.op, KJ_OP_PLAY);
    CHECK_EQ(a.arg, KJ_PAPER);
    CHECK_EQ(key(KJ_KEY_OK_LONG, 53, 0).op, KJ_OP_WITHDRAW);
    c.view.my_lock = KJ_PAPER;
    CHECK_EQ(key(KJ_KEY_OK, 54, 0).kind, KJ_ACT_NONE);   // 已扣牌，按键无效
    CHECK_EQ(key(KJ_KEY_OK_LONG, 54, 0).kind, KJ_ACT_NONE);

    // 亮牌：只播一次，OK 收起；新结果再次触发
    c.view.status = KJ_ST_IDLE;
    c.view.my_lock = KJ_CARD_NONE;
    c.view.res_duel_id = 7;
    c.view.res_outcome = KJ_OUT_WIN;
    c.view.res_my = KJ_PAPER;
    c.view.res_opp = KJ_ROCK;
    CHECK_EQ(update(60, 0, &cue), KJ_PAGE_REVEAL);
    CHECK_EQ(cue, KJ_CUE_WIN);
    CHECK_EQ(update(61, 0, &cue), KJ_PAGE_REVEAL);
    CHECK_EQ(cue, KJ_CUE_NONE);
    key(KJ_KEY_OK, 62, 0);
    CHECK_EQ(update(63, 0, &cue), KJ_PAGE_HAND);
    c.view.res_duel_id = 8;
    c.view.res_outcome = KJ_OUT_LOSE;
    CHECK_EQ(update(70, 0, &cue), KJ_PAGE_REVEAL);
    CHECK_EQ(cue, KJ_CUE_LOSE);
    CHECK_EQ(update(70 + KJ_REVEAL_AUTO_MS, 0, &cue), KJ_PAGE_HAND);   // 自动收起

    // 通知 → toast + 提示音；同一序号不重复
    c.view.notice = KJ_N_DECLINED;
    c.view.notice_seq++;
    update(20000, 0, &cue);
    CHECK_EQ(cue, KJ_CUE_NOTICE);
    CHECK_EQ(kj_flow_active_toast(&f, 20001), KJ_TOAST_DECLINED);
    CHECK_EQ(kj_flow_active_toast(&f, 20000 + KJ_TOAST_MS), KJ_TOAST_NONE);
    update(20002, 0, &cue);
    CHECK_EQ(cue, KJ_CUE_NONE);

    // 终局
    c.view.status = KJ_ST_CLEARED;
    CHECK_EQ(update(30000, 0, &cue), KJ_PAGE_FINAL);
    CHECK_EQ(cue, KJ_CUE_CLEARED);
    // 新一局：回到入座页，旧结算不重播
    c.view.game_id = 2;
    c.view.phase = KJ_PHASE_LOBBY;
    c.view.status = KJ_ST_WAITING;
    CHECK_EQ(update(31000, 0, &cue), KJ_PAGE_SEAT);

    // 重新入座时，视图里已有的历史结算 / 通知不会重播
    memset(&c, 0, sizeof(c));
    kj_flow_init(&f, 0);
    update(0, 0, NULL);
    joined_view(KJ_PHASE_RUNNING, KJ_ST_IDLE);
    c.view.res_duel_id = 99;
    c.view.res_outcome = KJ_OUT_WIN;
    c.view.notice = KJ_N_BUSY;
    c.view.notice_seq = 4;
    CHECK_EQ(update(10, 0, &cue), KJ_PAGE_HAND);
    CHECK_EQ(cue, KJ_CUE_NONE);
    CHECK_EQ(kj_flow_active_toast(&f, 11), KJ_TOAST_NONE);
}

static void test_bump_pages(void)
{
    memset(&c, 0, sizeof(c));
    kj_flow_init(&f, 0);
    joined_view(KJ_PHASE_RUNNING, KJ_ST_IDLE);
    kj_cue_t cue;
    CHECK_EQ(update(100, 1, &cue), KJ_PAGE_HAND);
    // 手牌页长按 = 碰拳：立刻显示碰拳页
    kj_action_t a = key(KJ_KEY_OK_LONG, 110, 1);
    CHECK_EQ(a.kind, KJ_ACT_REQUEST);
    CHECK_EQ(a.op, KJ_OP_BUMP);
    CHECK_EQ(update(120, 1, &cue), KJ_PAGE_BUMP);
    // 庄家接手（BUMPING），随后配对成功
    c.view.status = KJ_ST_BUMPING;
    CHECK_EQ(update(300, 1, &cue), KJ_PAGE_BUMP);
    CHECK(!f.bump_pending);
    c.view.status = KJ_ST_MATCHED;
    c.view.peer_no = 7;
    CHECK_EQ(update(900, 1, &cue), KJ_PAGE_MATCHED);
    CHECK_EQ(cue, KJ_CUE_MATCH);
    CHECK_EQ(key(KJ_KEY_OK, 910, 1).kind, KJ_ACT_NONE);   // 单击不做事，避免误触
    a = key(KJ_KEY_OK_LONG, 920, 1);
    CHECK_EQ(a.kind, KJ_ACT_REQUEST);
    CHECK_EQ(a.op, KJ_OP_CANCEL);
    // 倒计时结束进入出牌
    c.view.status = KJ_ST_DUEL;
    CHECK_EQ(update(4000, 1, &cue), KJ_PAGE_CHOOSE);
    CHECK_EQ(cue, KJ_CUE_DUEL);

    // 没碰到：通知到达时结束碰拳页并弹出提示
    c.view.status = KJ_ST_IDLE;
    update(5000, 1, NULL);
    key(KJ_KEY_OK_LONG, 5010, 1);
    CHECK_EQ(update(5020, 1, NULL), KJ_PAGE_BUMP);
    c.view.notice = KJ_N_BUMP_ALONE;
    c.view.notice_seq++;
    CHECK_EQ(update(6000, 1, &cue), KJ_PAGE_HAND);
    CHECK_EQ(cue, KJ_CUE_NOTICE);
    CHECK_EQ(kj_flow_active_toast(&f, 6001), KJ_TOAST_BUMP_ALONE);
    CHECK_EQ(kj_flow_toast_for_notice(KJ_N_BUMP_CROWD), KJ_TOAST_BUMP_CROWD);
    CHECK_EQ(kj_flow_toast_for_notice(KJ_N_MATCH_CANCELLED), KJ_TOAST_MATCH_CANCELLED);

    // 迟迟没有结果：本地碰拳页自动收起
    key(KJ_KEY_OK_LONG, 7000, 1);
    CHECK_EQ(update(7000 + KJ_BUMP_LOCAL_MS - 1, 1, NULL), KJ_PAGE_BUMP);
    CHECK_EQ(update(7000 + KJ_BUMP_LOCAL_MS, 1, NULL), KJ_PAGE_HAND);

    // 请求没能发出：立即撤销
    key(KJ_KEY_OK_LONG, 9000, 1);
    kj_flow_bump_rejected(&f);
    CHECK_EQ(update(9001, 1, NULL), KJ_PAGE_HAND);

    // 配对页的倒计时进入界面模型
    c.view.status = KJ_ST_MATCHED;
    c.view.deadline_s = 3;
    c.view_rx_ms = 10000;
    kj_player_ctx_t x = ctx(11200, 1);
    kj_page_t page = kj_flow_player_update(&f, &x, NULL);
    CHECK_EQ(page, KJ_PAGE_MATCHED);
    kj_ui_model_t m;
    kj_model_player(&m, &f, &x, page, NULL, NULL, 80);
    CHECK_EQ(m.deadline_s, 2);
}

static void test_valid_card(void)
{
    kj_view_t v = { 0 };
    CHECK_EQ(kj_flow_valid_card(&v, 0, 1), KJ_CARD_NONE);
    v.cards[KJ_PAPER] = 1;
    CHECK_EQ(kj_flow_valid_card(&v, KJ_ROCK, 1), KJ_PAPER);
    CHECK_EQ(kj_flow_valid_card(&v, KJ_ROCK, -1), KJ_PAPER);
    v.cards[KJ_SCISSORS] = 2;
    CHECK_EQ(kj_flow_valid_card(&v, KJ_ROCK, 1), KJ_SCISSORS);
    CHECK_EQ(kj_flow_valid_card(&v, KJ_ROCK, -1), KJ_PAPER);
}

static void test_host_menu(void)
{
    static kj_game_t g;
    kj_rules_init(&g);
    kj_flow_init(&f, 1);
    CHECK(!kj_flow_host_item_enabled(KJ_HM_START, &g));
    CHECK(kj_flow_host_item_enabled(KJ_HM_BOT_ADD, &g));
    CHECK(!kj_flow_host_item_enabled(KJ_HM_BOT_DEL, &g));
    // 人不够时开局被拒，提示需要 2 人
    kj_action_t a = kj_flow_host_key(&f, &g, KJ_KEY_OK, 100);
    CHECK_EQ(a.kind, KJ_ACT_NONE);
    CHECK_EQ(a.arg, 1);
    CHECK_EQ(kj_flow_active_toast(&f, 101), KJ_TOAST_NEED_TWO);
    kj_flow_host_sync(&f, &g);
    CHECK_EQ(f.host_sel, KJ_HM_BOT_ADD);   // 顺延到第一个可用项
    a = kj_flow_host_key(&f, &g, KJ_KEY_OK, 200);
    CHECK_EQ(a.kind, KJ_ACT_HOST_CMD);
    CHECK_EQ(a.cmd, KJ_CMD_BOT_ADD);
    kj_rules_add_bot(&g, 0);
    kj_rules_add_bot(&g, 0);
    // ▲ 从"添加电脑"往上跳过"新一局 / 结束"（不可用），落在"开始"
    a = kj_flow_host_key(&f, &g, KJ_KEY_UP, 300);
    CHECK_EQ(f.host_sel, KJ_HM_START);
    a = kj_flow_host_key(&f, &g, KJ_KEY_OK, 300);
    CHECK_EQ(a.cmd, KJ_CMD_START);
    kj_rules_start(&g, 300);
    kj_flow_host_sync(&f, &g);
    CHECK_EQ(f.host_sel, KJ_HM_END);
    // 结束需要二次确认；确认时其他键取消
    a = kj_flow_host_key(&f, &g, KJ_KEY_OK, 400);
    CHECK_EQ(a.kind, KJ_ACT_NONE);
    CHECK_EQ(f.host_confirm, KJ_HM_END);
    a = kj_flow_host_key(&f, &g, KJ_KEY_DOWN, 401);
    CHECK_EQ(a.kind, KJ_ACT_NONE);
    CHECK_EQ(f.host_confirm, -1);
    CHECK_EQ(f.host_sel, KJ_HM_END);      // 取消确认的那一下不移动选中项
    kj_flow_host_key(&f, &g, KJ_KEY_OK, 403);
    a = kj_flow_host_key(&f, &g, KJ_KEY_OK, 404);
    CHECK_EQ(a.kind, KJ_ACT_HOST_CMD);
    CHECK_EQ(a.cmd, KJ_CMD_END);
    // 确认中的项失效时自动取消
    f.host_confirm = KJ_HM_START;
    kj_flow_host_sync(&f, &g);
    CHECK_EQ(f.host_confirm, -1);
}

static void test_model(void)
{
    memset(&c, 0, sizeof(c));
    kj_flow_init(&f, 0);
    joined_view(KJ_PHASE_RUNNING, KJ_ST_CHALLENGED);
    c.view.deadline_s = 18;
    c.view_rx_ms = 1000;
    c.have_room_info = true;
    c.room_info.seated = 7;
    kj_player_ctx_t x = ctx(4500, 0);
    kj_page_t page = kj_flow_player_update(&f, &x, NULL);
    kj_ui_model_t m;
    kj_model_player(&m, &f, &x, page, NULL, NULL, 150);
    CHECK_EQ(m.page, KJ_PAGE_CHALLENGED);
    CHECK_EQ(m.deadline_s, 15);   // 收到视图后过了 3.5 s
    CHECK_EQ(m.battery, 100);
    CHECK_EQ(m.seated, 7);
    CHECK(!m.disconnected);
    x.now_ms = 60000;
    kj_model_player(&m, &f, &x, page, NULL, NULL, -5);
    CHECK_EQ(m.deadline_s, 0);
    CHECK_EQ(m.battery, -1);

    static kj_server_t s;
    kj_server_init(&s, 0xBEEF, 1);
    kj_server_command(&s, KJ_CMD_BOT_ADD, 0, 0);
    kj_model_host(&m, &f, &s, 65000, 50, KJ_BOARD_USB, NULL, NULL);
    CHECK_EQ(m.page, KJ_PAGE_HOST);
    CHECK_EQ(m.host_room, 0xBEEF);
    CHECK_EQ(m.host_sum.seated, 1);
    CHECK_EQ(m.host_phase_s, 65);
    CHECK_EQ(m.board, KJ_BOARD_USB);
    CHECK(m.host_enabled & (1u << KJ_HM_BOT_DEL));
    CHECK(m.host_enabled & (1u << KJ_HM_ROSTER));
    CHECK(!(m.host_enabled & (1u << KJ_HM_START)));
}

// 测试用的昵称表：编号 n 叫 "P<n>"，编号 13 没登记，其余（> 50）还不知道
static int lookups;
static bool fake_names(void *ctx, uint16_t room, uint8_t no, const char **name)
{
    (void)ctx;
    lookups++;
    static char buf[8];
    if (room != 0x1234 || no > 50) return false;
    if (no == 13) {
        *name = "";
        return true;
    }
    snprintf(buf, sizeof(buf), "P%u", no);
    *name = buf;
    return true;
}

static void test_names_and_env(void)
{
    const kj_names_if_t names = { .lookup = fake_names };
    uint8_t ipb[4] = { 192, 168, 1, 10 };
    uint32_t ip;
    memcpy(&ip, ipb, 4);
    char text[16];
    kj_ip_text(ip, text);
    CHECK(strcmp(text, "192.168.1.10") == 0);
    const kj_model_env_t env = { .net = KJ_NET_OK, .hub_ip = ip, .ssid = "Cafe", .my_name = "Amy", .fw = "1.1.0",
                                 .dev_id = 0xA3F2 };
    kj_ui_model_t m;
    kj_flow_init(&f, 0);
    kj_model_title(&m, &f, &env, 50, 0);
    CHECK_EQ(m.net, KJ_NET_OK);
    CHECK(strcmp(m.my_name, "Amy") == 0);
    CHECK(strcmp(m.ssid, "Cafe") == 0);
    CHECK_EQ(m.dev_id, 0xA3F2);
    kj_model_settings(&m, &f, &env, 50, 0);
    CHECK_EQ(m.page, KJ_PAGE_SETTINGS);
    CHECK_EQ(m.settings_count, 3);                   // 直连：登记昵称 / 改用电脑服务 / 返回
    CHECK_EQ(m.settings_items[1], KJ_SET_CONN);
    CHECK(!m.settings_confirm);
    f.settings_sel = 1;
    f.settings_confirm = true;
    kj_flow_set_conn(&f, KJ_CONN_HUB);
    kj_model_settings(&m, &f, &env, 50, 0);
    CHECK_EQ(m.settings_count, 4);                   // 电脑服务多一项"重新配网"
    CHECK_EQ(m.settings_items[1], KJ_SET_WIFI);
    CHECK_EQ(m.settings_sel, 1);
    CHECK(m.settings_confirm);
    kj_flow_init(&f, 0);
    const kj_model_env_t denv = { .conn = KJ_CONN_DIRECT, .channel = 6, .net = KJ_NET_DIRECT, .my_name = "Amy" };
    kj_model_title(&m, &f, &denv, 50, 0);
    CHECK_EQ(m.conn, KJ_CONN_DIRECT);
    CHECK_EQ(m.channel, 6);
    CHECK_EQ(m.net, KJ_NET_DIRECT);
    kj_model_register(&m, &f, &denv, KH_REG_INVALID, NULL, 50, 0);   // 直连：登记页只做说明，没有网址
    CHECK_EQ(m.page, KJ_PAGE_REGISTER);
    CHECK_EQ(m.qr[0], '\0');
    // 登记页：没连上电脑服务时不给二维码；连上后屏幕上的网址去掉 http://
    kj_model_register(&m, &f, &env, KH_REG_INVALID, "http://192.168.1.10:47180/j/ABCDEFG2", 50, 0);
    CHECK_EQ(m.qr[0], '\0');
    kj_model_register(&m, &f, &env, KH_REG_WAITING, "http://192.168.1.10:47180/j/ABCDEFG2", 50, 0);
    CHECK(strcmp(m.qr, "http://192.168.1.10:47180/j/ABCDEFG2") == 0);
    CHECK(strcmp(m.line1, "192.168.1.10:47180") == 0);
    CHECK(strcmp(m.line2, "/j/ABCDEFG2") == 0);
    kj_model_provision(&m, &f, &env, KJ_PROV_KIND_WIFI, KJ_PROV_TRYING, "WIFI:T:WPA;S:KJ-A3F2;P:1;;", "KJ-A3F2", "Home",
                       50, 0);
    CHECK_EQ(m.page, KJ_PAGE_PROVISION);
    CHECK_EQ(m.prov_kind, KJ_PROV_KIND_WIFI);
    CHECK_EQ(m.prov_state, KJ_PROV_TRYING);
    CHECK(strcmp(m.line2, "Home") == 0);
    kj_model_provision(&m, &f, &denv, KJ_PROV_KIND_NAME, KJ_PROV_OK, "WIFI:T:WPA;S:KJ-A3F2;P:1;;", "KJ-A3F2", "123",
                       50, 0);
    CHECK_EQ(m.prov_kind, KJ_PROV_KIND_NAME);
    CHECK_EQ(m.prov_state, KJ_PROV_OK);
    CHECK(strcmp(m.my_name, "Amy") == 0);            // 登记成功页显示新昵称

    // 选对手页：只给屏幕上看得见的几行查昵称
    memset(&c, 0, sizeof(c));
    kj_flow_init(&f, 0);
    joined_view(KJ_PHASE_RUNNING, KJ_ST_IDLE);
    for (int i = 0; i < 4; i++) opps[i].no = (uint8_t)(10 + i);   // 10、11、12、13
    opps[3].no = 13;
    key(KJ_KEY_OK, 100, 4);                  // 进入选对手
    kj_player_ctx_t x = ctx(110, 4);
    kj_page_t page = kj_flow_player_update(&f, &x, NULL);
    CHECK_EQ(page, KJ_PAGE_OPPONENTS);
    lookups = 0;
    kj_model_player(&m, &f, &x, page, &env, &names, 50);
    CHECK_EQ(m.opp_first, 0);
    CHECK(strcmp(m.opp_names[0], "P10") == 0);
    CHECK_EQ(m.opp_names[3][0], '\0');      // 13 号没登记
    CHECK(lookups <= KJ_UI_OPP_ROWS + 1);
    CHECK_EQ(kj_model_opp_first(5, 8), 3);
    CHECK_EQ(kj_model_opp_first(7, 8), 4);   // 到底了不再往下滚
    CHECK_EQ(kj_model_opp_first(1, 2), 0);
    // 对决中：对手昵称；亮牌页用结算里的对手
    c.view.status = KJ_ST_DUEL;
    c.view.peer_no = 7;
    x = ctx(200, 0);
    page = kj_flow_player_update(&f, &x, NULL);
    kj_model_player(&m, &f, &x, page, &env, &names, 50);
    CHECK(strcmp(m.peer_name, "P7") == 0);
    c.view.peer_no = 99;                     // 不知道的昵称：留空（界面显示编号）
    kj_model_player(&m, &f, &x, page, &env, &names, 50);
    CHECK_EQ(m.peer_name[0], '\0');

    // 庄家名单：从第 roster_first 位开始最多 5 行，带昵称；电脑选手不查昵称
    static kj_server_t s;
    kj_server_init(&s, 0x1234, 1);
    uint8_t mac[6] = { 2, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 6; i++) {
        mac[5] = (uint8_t)(i + 1);
        kj_rules_join(&s.game, mac, 0);
    }
    kj_server_command(&s, KJ_CMD_BOT_ADD, 0, 0);
    kj_flow_init(&f, 1);
    f.host_sel = KJ_HM_ROSTER;
    kj_flow_host_sync(&f, &s.game);
    CHECK_EQ(f.host_sel, KJ_HM_ROSTER);
    kj_flow_host_key(&f, &s.game, KJ_KEY_OK, 0);
    CHECK(f.host_roster);
    kj_model_host(&m, &f, &s, 0, 50, KJ_BOARD_WIFI, &env, &names);
    CHECK_EQ(m.page, KJ_PAGE_HOST_ROSTER);
    CHECK_EQ(m.roster_total, 7);
    CHECK_EQ(m.roster_count, KJ_ROSTER_ROWS);
    CHECK_EQ(m.roster[0].no, 1);
    CHECK(strcmp(m.roster[0].name, "P1") == 0);
    kj_flow_host_key(&f, &s.game, KJ_KEY_DOWN, 0);
    kj_flow_host_key(&f, &s.game, KJ_KEY_DOWN, 0);
    kj_flow_host_key(&f, &s.game, KJ_KEY_DOWN, 0);   // 最多滚到 7 - 5 = 2
    CHECK_EQ(f.roster_first, 2);
    kj_model_host(&m, &f, &s, 0, 50, KJ_BOARD_WIFI, &env, &names);
    CHECK_EQ(m.roster[0].no, 3);
    CHECK_EQ(m.roster[4].no, 7);
    CHECK(m.roster[4].is_bot);
    CHECK_EQ(m.roster[4].name[0], '\0');
    CHECK_EQ(m.roster[0].cards, 0);   // 还没开局
    kj_flow_host_key(&f, &s.game, KJ_KEY_UP, 0);
    CHECK_EQ(f.roster_first, 1);
    kj_flow_host_key(&f, &s.game, KJ_KEY_OK_LONG, 0);
    CHECK(!f.host_roster);
    kj_model_host(&m, &f, &s, 0, 50, KJ_BOARD_WIFI, &env, &names);
    CHECK_EQ(m.page, KJ_PAGE_HOST);
    // 人都移走了：名单自动关上，滚动位置收敛
    f.host_roster = true;
    f.roster_first = 9;
    kj_server_command(&s, KJ_CMD_RESET, 0, 0);
    kj_flow_host_sync(&f, &s.game);
    CHECK(!f.host_roster);
    CHECK_EQ(f.roster_first, 0);
}

int main(void)
{
    test_title();
    test_player_pages();
    test_bump_pages();
    test_valid_card();
    test_host_menu();
    test_model();
    test_names_and_env();
    KJ_TEST_DONE("test_kj_flow");
}
