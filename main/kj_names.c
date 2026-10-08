// main/kj_names.c —— 直连模式的昵称表与本机广播（纯 C）。
#include "kj_names.h"
#include "kj_utf8.h"

#include <string.h>

static uint32_t rnd(kj_names_t *t)
{
    uint32_t x = t->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    t->rng = x;
    return x;
}

static void copy_name(char *dst, const char *src)
{
    size_t n = 0;
    if (src) {
        while (src[n] && n < KJ_NAME_MAX) n++;
    }
    memcpy(dst, src ? src : "", n);
    dst[n] = '\0';
}

void kj_names_init(kj_names_t *t, uint32_t seed)
{
    memset(t, 0, sizeof(*t));
    t->rng = seed ? seed : 0x9E3779B9u;
}

void kj_names_set_room(kj_names_t *t, uint16_t room)
{
    if (room == 0 || room == t->room) return;
    t->room = room;
    memset(t->seat, 0, sizeof(t->seat));
}

bool kj_names_store(kj_names_t *t, const uint8_t mac[6], uint16_t room, const kj_name_t *n)
{
    if (room == 0 || room != t->room || n->no == 0 || n->no > KJ_MAX_PLAYERS) return false;
    kj_name_entry_t *e = &t->seat[kj_idx_of(n->no)];
    if (e->known && memcmp(e->mac, mac, 6) == 0 && strcmp(e->name, n->name) == 0) return false;
    e->known = 1;
    memcpy(e->mac, mac, 6);
    copy_name(e->name, n->name);
    return true;
}

bool kj_names_get(const kj_names_t *t, uint16_t room, uint8_t no, const uint8_t *mac, const char **name)
{
    if (name) *name = NULL;
    if (room == 0 || room != t->room || no == 0 || no > KJ_MAX_PLAYERS) return false;
    const kj_name_entry_t *e = &t->seat[kj_idx_of(no)];
    if (!e->known || (mac && memcmp(e->mac, mac, 6) != 0)) return false;
    if (name) *name = e->name;
    return true;
}

static uint32_t tag_interval(kj_names_t *t)
{
    uint32_t span = KJ_NAMES_TAG_MS * 2 / 5;   // ±20%
    return KJ_NAMES_TAG_MS - span / 2 + rnd(t) % (span + 1);
}

void kj_names_tick(kj_names_t *t, uint16_t room, uint8_t no, const uint8_t my_mac[6], const char *my_name,
                   uint32_t now_ms, kj_outbox_t *out)
{
    // 没入座时不广播；表也留着（离座后回到同一个座位还能接着用）
    if (room == 0 || no == 0 || no > KJ_MAX_PLAYERS) {
        t->seated = false;
        return;
    }
    if (room != t->seat_room || no != t->seat_no) {
        // 新入座、换了赌局或重新编号：别人的编号可能也变了，整表作废，等各人的广播重新填满
        t->room = room;
        memset(t->seat, 0, sizeof(t->seat));
        t->seat_room = room;
        t->seat_no = no;
        t->tag_due = true;
    }
    if (!t->seated) {   // 刚（重新）入座：马上让大家知道
        t->seated = true;
        t->tag_due = true;
    }
    char want[KJ_NAME_MAX + 1];
    kj_utf8_sanitize(my_name ? my_name : "", my_name ? strlen(my_name) : 0, want, sizeof(want));
    if (strcmp(want, t->tag_name) != 0) {
        memcpy(t->tag_name, want, sizeof(want));
        t->tag_due = true;
    }
    kj_name_t self = { .no = no };
    memcpy(self.name, t->tag_name, sizeof(self.name));
    kj_names_store(t, my_mac, room, &self);   // 自己收不到自己的广播：直接记进表里
    if (!t->tag_due && (int32_t)(now_ms - t->next_tag_ms) < 0) return;
    if (out->count >= KJ_OUTBOX_MAX) return;   // 发送队列满了：下一轮再发
    kj_frame_t f = { .type = KJ_F_NAME, .room = room };
    f.u.name = self;
    kj_outbox_push(out, NULL, &f);
    t->tag_due = false;
    t->next_tag_ms = now_ms + tag_interval(t);
}
