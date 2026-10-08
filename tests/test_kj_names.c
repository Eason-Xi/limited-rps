// tests/test_kj_names.c —— 直连模式的昵称表：按赌局与编号记录、庄家的 MAC 校验、座位变化清表、
// 本机广播的时机（立即 / 周期抖动 / 昵称变化 / 队列满）。
#include "kj_names.h"
#include "kj_test.h"

#include <string.h>

static const uint8_t MAC_A[6] = { 0x24, 0x6F, 0x28, 0x00, 0x00, 0x0A };
static const uint8_t MAC_B[6] = { 0x24, 0x6F, 0x28, 0x00, 0x00, 0x0B };
static const uint8_t MAC_ME[6] = { 0x24, 0x6F, 0x28, 0x00, 0x00, 0xEE };
#define ROOM 0xA3F2
#define XIAOMING "\xE5\xB0\x8F\xE6\x98\x8E"   // 小明

static kj_name_t tag(uint8_t no, const char *name)
{
    kj_name_t n = { .no = no };
    strcpy(n.name, name);
    return n;
}

// 解出 outbox 里唯一的一条广播。
static bool only_tag(const kj_outbox_t *o, kj_frame_t *f)
{
    if (o->count != 1 || !o->items[0].broadcast) return false;
    return kj_proto_decode(o->items[0].data, o->items[0].len, f) && f->type == KJ_F_NAME;
}

static void test_table(void)
{
    static kj_names_t t;
    const char *name;
    kj_names_init(&t, 1);
    CHECK(!kj_names_get(&t, ROOM, 3, NULL, &name));
    CHECK(name == NULL);
    kj_name_t n = tag(3, XIAOMING);
    CHECK(!kj_names_store(&t, MAC_A, ROOM, &n));       // 还没指定赌局：不记
    kj_names_set_room(&t, ROOM);
    CHECK(kj_names_store(&t, MAC_A, ROOM, &n));
    CHECK(!kj_names_store(&t, MAC_A, ROOM, &n));       // 没变化
    CHECK(kj_names_get(&t, ROOM, 3, NULL, &name));
    CHECK(strcmp(name, XIAOMING) == 0);
    CHECK(kj_names_get(&t, ROOM, 3, MAC_A, &name));    // 庄家按座位 MAC 校验
    CHECK(!kj_names_get(&t, ROOM, 3, MAC_B, &name));   // 座位换了人：旧昵称作废
    CHECK(!kj_names_get(&t, ROOM + 1, 3, NULL, &name));
    CHECK(!kj_names_get(&t, ROOM, 4, NULL, &name));
    CHECK(!kj_names_get(&t, ROOM, 0, NULL, &name));
    CHECK(!kj_names_get(&t, ROOM, KJ_MAX_PLAYERS + 1, NULL, &name));
    // 别的赌局的广播、越界编号都不记
    kj_name_t other = tag(5, "Bob");
    CHECK(!kj_names_store(&t, MAC_B, ROOM + 1, &other));
    CHECK(!kj_names_get(&t, ROOM + 1, 5, NULL, &name));
    other.no = 0;
    CHECK(!kj_names_store(&t, MAC_B, ROOM, &other));
    other.no = KJ_MAX_PLAYERS + 1;
    CHECK(!kj_names_store(&t, MAC_B, ROOM, &other));
    // 同一编号以最新一次广播为准（重新编号后由新主人纠正）
    kj_name_t newer = tag(3, "Bob");
    CHECK(kj_names_store(&t, MAC_B, ROOM, &newer));
    CHECK(kj_names_get(&t, ROOM, 3, MAC_B, &name));
    CHECK(strcmp(name, "Bob") == 0);
    // 空串 = 对方没有昵称：查得到，但是空的
    kj_name_t empty = tag(9, "");
    CHECK(kj_names_store(&t, MAC_A, ROOM, &empty));
    CHECK(kj_names_get(&t, ROOM, 9, NULL, &name));
    CHECK_EQ(name[0], '\0');
    // 最高编号
    kj_name_t last = tag(KJ_MAX_PLAYERS, "Zed");
    CHECK(kj_names_store(&t, MAC_A, ROOM, &last));
    CHECK(kj_names_get(&t, ROOM, KJ_MAX_PLAYERS, NULL, &name));
    // 同一个赌局不清表；换赌局清表；0 不变
    kj_names_set_room(&t, ROOM);
    CHECK(kj_names_get(&t, ROOM, 3, NULL, &name));
    kj_names_set_room(&t, 0);
    CHECK(kj_names_get(&t, ROOM, 3, NULL, &name));
    kj_names_set_room(&t, ROOM + 1);
    CHECK(!kj_names_get(&t, ROOM, 3, NULL, &name));
    CHECK(!kj_names_get(&t, ROOM + 1, 3, NULL, &name));
}

static void test_tags(void)
{
    static kj_names_t t;
    kj_outbox_t o;
    kj_frame_t f;
    const char *name;
    kj_names_init(&t, 0xC0FFEE);

    // 没入座：不广播
    kj_outbox_clear(&o);
    kj_names_tick(&t, 0, 0, MAC_ME, "Amy", 1000, &o);
    CHECK_EQ(o.count, 0);
    kj_names_tick(&t, ROOM, 0, MAC_ME, "Amy", 1000, &o);
    CHECK_EQ(o.count, 0);

    // 入座：立即广播一次，自己的昵称也记进表里
    kj_names_tick(&t, ROOM, 5, MAC_ME, "Amy", 1000, &o);
    CHECK(only_tag(&o, &f));
    CHECK_EQ(f.room, ROOM);
    CHECK_EQ(f.u.name.no, 5);
    CHECK(strcmp(f.u.name.name, "Amy") == 0);
    CHECK(kj_names_get(&t, ROOM, 5, MAC_ME, &name));
    CHECK(strcmp(name, "Amy") == 0);
    // 收到别人的广播
    kj_name_t bob = tag(9, "Bob");
    CHECK(kj_names_store(&t, MAC_B, ROOM, &bob));

    // 周期：间隔在 KJ_NAMES_TAG_MS ±20% 之内，且不总是同一个值（有抖动）
    uint32_t last = 1000, min_gap = 0xFFFFFFFFu, max_gap = 0;
    int sent = 0;
    for (uint32_t now = 1010; now < 1000 + 60000; now += 10) {
        kj_outbox_clear(&o);
        kj_names_tick(&t, ROOM, 5, MAC_ME, "Amy", now, &o);
        if (o.count) {
            CHECK(only_tag(&o, &f));
            uint32_t gap = now - last;
            if (gap < min_gap) min_gap = gap;
            if (gap > max_gap) max_gap = gap;
            last = now;
            sent++;
        }
    }
    CHECK(sent >= 15 && sent <= 25);
    CHECK(min_gap >= KJ_NAMES_TAG_MS * 4 / 5);
    CHECK(max_gap <= KJ_NAMES_TAG_MS * 6 / 5 + 10);
    CHECK(max_gap > min_gap);
    CHECK(kj_names_get(&t, ROOM, 9, NULL, &name));   // 同一个座位：别人的昵称还在

    // 昵称变了：立即广播
    uint32_t now = last + 100;
    kj_outbox_clear(&o);
    kj_names_tick(&t, ROOM, 5, MAC_ME, XIAOMING, now, &o);
    CHECK(only_tag(&o, &f));
    CHECK(strcmp(f.u.name.name, XIAOMING) == 0);
    kj_outbox_clear(&o);
    kj_names_tick(&t, ROOM, 5, MAC_ME, XIAOMING, now + 10, &o);
    CHECK_EQ(o.count, 0);

    // 离座再回到同一个座位：表留着，但马上再广播一次
    kj_names_tick(&t, 0, 0, MAC_ME, XIAOMING, now + 20, &o);
    CHECK_EQ(o.count, 0);
    kj_names_tick(&t, ROOM, 5, MAC_ME, XIAOMING, now + 30, &o);
    CHECK(only_tag(&o, &f));
    CHECK(kj_names_get(&t, ROOM, 9, NULL, &name));

    // 重新编号（庄家清空后重新入座）：整表作废，立即广播新编号
    kj_outbox_clear(&o);
    kj_names_tick(&t, 0, 0, MAC_ME, XIAOMING, now + 40, &o);
    kj_names_tick(&t, ROOM, 2, MAC_ME, XIAOMING, now + 50, &o);
    CHECK(only_tag(&o, &f));
    CHECK_EQ(f.u.name.no, 2);
    CHECK(!kj_names_get(&t, ROOM, 9, NULL, &name));
    CHECK(!kj_names_get(&t, ROOM, 5, NULL, &name));
    CHECK(kj_names_get(&t, ROOM, 2, NULL, &name));
    // 编号不变但换了赌局（同一个编号，不同庄家）：同样清表
    CHECK(kj_names_store(&t, MAC_B, ROOM, &bob));
    kj_outbox_clear(&o);
    kj_names_tick(&t, ROOM + 1, 2, MAC_ME, XIAOMING, now + 60, &o);
    CHECK(only_tag(&o, &f));
    CHECK_EQ(f.room, ROOM + 1);
    CHECK(!kj_names_get(&t, ROOM, 9, NULL, &name));
    CHECK(!kj_names_get(&t, ROOM + 1, 9, NULL, &name));

    // 发送队列满：先不发，有空位的下一轮补发
    kj_outbox_clear(&o);
    kj_frame_t hello = { .type = KJ_F_HELLO, .room = ROOM + 1 };
    while (o.count < KJ_OUTBOX_MAX) kj_outbox_push(&o, MAC_A, &hello);
    kj_names_tick(&t, ROOM + 1, 2, MAC_ME, "Amy", now + 70, &o);   // 昵称变了，本该立即发
    CHECK_EQ(o.count, KJ_OUTBOX_MAX);
    kj_outbox_clear(&o);
    kj_names_tick(&t, ROOM + 1, 2, MAC_ME, "Amy", now + 80, &o);
    CHECK(only_tag(&o, &f));
    CHECK(strcmp(f.u.name.name, "Amy") == 0);

    // 没有昵称也广播（空串），让别人清掉这个编号上的旧昵称
    kj_outbox_clear(&o);
    kj_names_tick(&t, ROOM + 1, 2, MAC_ME, NULL, now + 90, &o);
    CHECK(only_tag(&o, &f));
    CHECK_EQ(f.u.name.name[0], '\0');
    // 本机昵称里有控制字符或被截断的半个汉字（不该出现，但 NVS 里的数据不可信）：清理后照常广播
    kj_outbox_clear(&o);
    kj_names_tick(&t, ROOM + 1, 2, MAC_ME, "A\x01" "B\xE5\xB0", now + 100, &o);
    CHECK(only_tag(&o, &f));
    CHECK(strcmp(f.u.name.name, "AB") == 0);
}

// 两台选手设备互相广播，经过有丢包的信道也能在几秒内知道对方的昵称。
static void test_exchange(void)
{
    static kj_names_t a, b;
    kj_names_init(&a, 11);
    kj_names_init(&b, 22);
    uint32_t rng = 7;
    int lost = 0;
    bool a_knows = false, b_knows = false;
    uint32_t t_known = 0;
    for (uint32_t now = 1; now < 20000 && !(a_knows && b_knows); now += 10) {
        kj_outbox_t oa, ob;
        kj_outbox_clear(&oa);
        kj_outbox_clear(&ob);
        kj_names_tick(&a, ROOM, 1, MAC_A, "Amy", now, &oa);
        kj_names_tick(&b, ROOM, 2, MAC_B, XIAOMING, now, &ob);
        for (int k = 0; k < oa.count; k++) {
            rng = rng * 1103515245u + 12345u;
            kj_frame_t f;
            if ((rng >> 16) % 2 == 0) {   // 一半的广播丢掉
                lost++;
                continue;
            }
            if (kj_proto_decode(oa.items[k].data, oa.items[k].len, &f)) kj_names_store(&b, MAC_A, f.room, &f.u.name);
        }
        for (int k = 0; k < ob.count; k++) {
            rng = rng * 1103515245u + 12345u;
            kj_frame_t f;
            if ((rng >> 16) % 2 == 0) {
                lost++;
                continue;
            }
            if (kj_proto_decode(ob.items[k].data, ob.items[k].len, &f)) kj_names_store(&a, MAC_B, f.room, &f.u.name);
        }
        const char *name;
        a_knows = kj_names_get(&a, ROOM, 2, NULL, &name) && strcmp(name, XIAOMING) == 0;
        b_knows = kj_names_get(&b, ROOM, 1, MAC_A, &name) && strcmp(name, "Amy") == 0;
        t_known = now;
    }
    CHECK(a_knows && b_knows);
    CHECK(t_known < 20000);
    printf("names exchanged after %u ms (%d broadcasts lost)\n", (unsigned)t_known, lost);
}

int main(void)
{
    test_table();
    test_tags();
    test_exchange();
    KJ_TEST_DONE("test_kj_names");
}
