// main/kj_proto.c —— 游戏帧编解码（固定小端布局，不依赖结构体内存排布）。
#include "kj_proto.h"
#include "kj_utf8.h"

#include <string.h>

#define VIEW_PAYLOAD 34
#define ROOM_PAYLOAD (1 + 2 + 2 + 1 + 1 + KJ_BITMAP_BYTES * 2)
#define HELLO_PAYLOAD 4
#define REQ_PAYLOAD 8
#define NAME_PAYLOAD_MIN 3   // no、flags、len，后面跟 len 字节的昵称

typedef struct {
    uint8_t *p;
    size_t n, cap;
    bool ok;
} writer_t;

static void put8(writer_t *w, uint8_t v)
{
    if (w->n + 1 > w->cap) {
        w->ok = false;
        return;
    }
    w->p[w->n++] = v;
}

static void put16(writer_t *w, uint16_t v)
{
    put8(w, (uint8_t)(v & 0xFF));
    put8(w, (uint8_t)(v >> 8));
}

static void putn(writer_t *w, const uint8_t *src, size_t len)
{
    for (size_t i = 0; i < len; i++) put8(w, src[i]);
}

typedef struct {
    const uint8_t *p;
    size_t n, len;
} reader_t;

static uint8_t get8(reader_t *r) { return r->n < r->len ? r->p[r->n++] : 0; }

static uint16_t get16(reader_t *r)
{
    uint16_t lo = get8(r);
    uint16_t hi = get8(r);
    return (uint16_t)(lo | (hi << 8));
}

static void getn(reader_t *r, uint8_t *dst, size_t len)
{
    for (size_t i = 0; i < len; i++) dst[i] = get8(r);
}

static size_t name_len(const char *s)
{
    size_t n = 0;
    while (n <= KJ_NAME_MAX && s[n]) n++;
    return n;
}

// 昵称必须是合法 UTF-8，且不含控制字符（屏幕和看板行都会原样用到它）。
static bool name_ok(const char *s, size_t n)
{
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        int k = kj_utf8_decode(s + i, n - i, &cp);
        if (k == 0 || cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0)) return false;
        i += (size_t)k;
    }
    return true;
}

size_t kj_proto_encode(const kj_frame_t *f, uint8_t *buf, size_t cap)
{
    writer_t w = { .p = buf, .n = 0, .cap = cap, .ok = true };
    put8(&w, 'K');
    put8(&w, 'J');
    put8(&w, KJ_PROTO_VERSION);
    put8(&w, f->type);
    put16(&w, f->room);
    switch (f->type) {
    case KJ_F_ROOM: {
        const kj_room_t *r = &f->u.room_info;
        put8(&w, r->phase);
        put16(&w, r->game_id);
        put16(&w, r->phase_ver);
        put8(&w, r->seated);
        put8(&w, r->online);
        putn(&w, r->avail, KJ_BITMAP_BYTES);
        putn(&w, r->bots, KJ_BITMAP_BYTES);
        break;
    }
    case KJ_F_HELLO:
        put8(&w, f->u.hello.no);
        put8(&w, f->u.hello.flags);
        put16(&w, f->u.hello.view_ver);
        break;
    case KJ_F_REQ:
        put16(&w, f->u.req.seq);
        put8(&w, f->u.req.op);
        put8(&w, f->u.req.arg);
        put16(&w, f->u.req.age_ms);
        put16(&w, f->u.req.boot);
        break;
    case KJ_F_VIEW: {
        const kj_view_t *v = &f->u.view;
        put16(&w, v->view_ver);
        put16(&w, v->ack_seq);
        put16(&w, v->game_id);
        put16(&w, v->duel_id);
        put16(&w, v->res_duel_id);
        put8(&w, v->no);
        put8(&w, v->phase);
        put8(&w, v->status);
        putn(&w, v->cards, KJ_CARD_TYPES);
        put8(&w, v->stars);
        put8(&w, v->peer_no);
        put8(&w, v->peer_is_bot);
        put8(&w, v->my_lock);
        put8(&w, v->peer_locked);
        put8(&w, v->notice);
        put8(&w, v->notice_seq);
        put8(&w, v->res_my);
        put8(&w, v->res_opp);
        put8(&w, v->res_outcome);
        put8(&w, v->res_opp_no);
        put8(&w, v->final_reason);
        put8(&w, v->wins);
        put8(&w, v->losses);
        put8(&w, v->draws);
        put8(&w, v->deadline_s);
        put16(&w, v->epoch);
        break;
    }
    case KJ_F_NAME: {
        const kj_name_t *nm = &f->u.name;
        size_t n = name_len(nm->name);
        if (nm->no == 0 || nm->no > KJ_MAX_PLAYERS || n > KJ_NAME_MAX || !name_ok(nm->name, n)) return 0;
        put8(&w, nm->no);
        put8(&w, nm->flags);
        put8(&w, (uint8_t)n);
        putn(&w, (const uint8_t *)nm->name, n);
        break;
    }
    default:
        return 0;
    }
    return w.ok ? w.n : 0;
}

static size_t payload_len(uint8_t type)
{
    switch (type) {
    case KJ_F_ROOM: return ROOM_PAYLOAD;
    case KJ_F_HELLO: return HELLO_PAYLOAD;
    case KJ_F_REQ: return REQ_PAYLOAD;
    case KJ_F_VIEW: return VIEW_PAYLOAD;
    case KJ_F_NAME: return NAME_PAYLOAD_MIN;
    default: return 0;
    }
}

uint8_t kj_proto_peek_type(const uint8_t *buf, size_t len)
{
    if (!buf || len < KJ_FRAME_HEADER || buf[0] != 'K' || buf[1] != 'J' || buf[2] != KJ_PROTO_VERSION) return 0;
    return buf[3];
}

bool kj_proto_decode(const uint8_t *buf, size_t len, kj_frame_t *out)
{
    if (!buf || !out || len < KJ_FRAME_HEADER) return false;
    if (buf[0] != 'K' || buf[1] != 'J' || buf[2] != KJ_PROTO_VERSION) return false;
    size_t need = payload_len(buf[3]);
    if (need == 0 || len < KJ_FRAME_HEADER + need) return false;   // 允许尾部扩展字段
    memset(out, 0, sizeof(*out));
    reader_t r = { .p = buf, .n = 3, .len = len };
    out->type = get8(&r);
    out->room = get16(&r);
    switch (out->type) {
    case KJ_F_ROOM: {
        kj_room_t *ri = &out->u.room_info;
        ri->phase = get8(&r);
        ri->game_id = get16(&r);
        ri->phase_ver = get16(&r);
        ri->seated = get8(&r);
        ri->online = get8(&r);
        getn(&r, ri->avail, KJ_BITMAP_BYTES);
        getn(&r, ri->bots, KJ_BITMAP_BYTES);
        if (ri->phase > KJ_PHASE_ENDED) return false;
        break;
    }
    case KJ_F_HELLO:
        out->u.hello.no = get8(&r);
        out->u.hello.flags = get8(&r);
        out->u.hello.view_ver = get16(&r);
        if (out->u.hello.no > KJ_MAX_PLAYERS) return false;
        break;
    case KJ_F_REQ:
        out->u.req.seq = get16(&r);
        out->u.req.op = get8(&r);
        out->u.req.arg = get8(&r);
        out->u.req.age_ms = get16(&r);
        out->u.req.boot = get16(&r);
        if (out->u.req.op == 0 || out->u.req.op >= KJ_OP_COUNT) return false;
        break;
    case KJ_F_VIEW: {
        kj_view_t *v = &out->u.view;
        v->view_ver = get16(&r);
        v->ack_seq = get16(&r);
        v->game_id = get16(&r);
        v->duel_id = get16(&r);
        v->res_duel_id = get16(&r);
        v->no = get8(&r);
        v->phase = get8(&r);
        v->status = get8(&r);
        getn(&r, v->cards, KJ_CARD_TYPES);
        v->stars = get8(&r);
        v->peer_no = get8(&r);
        v->peer_is_bot = get8(&r);
        v->my_lock = get8(&r);
        v->peer_locked = get8(&r);
        v->notice = get8(&r);
        v->notice_seq = get8(&r);
        v->res_my = get8(&r);
        v->res_opp = get8(&r);
        v->res_outcome = get8(&r);
        v->res_opp_no = get8(&r);
        v->final_reason = get8(&r);
        v->wins = get8(&r);
        v->losses = get8(&r);
        v->draws = get8(&r);
        v->deadline_s = get8(&r);
        v->epoch = get16(&r);
        // 范围校验：越界值来自损坏或不兼容的帧，整帧丢弃。
        if (v->no > KJ_MAX_PLAYERS || v->peer_no > KJ_MAX_PLAYERS || v->res_opp_no > KJ_MAX_PLAYERS) return false;
        if (v->phase > KJ_PHASE_ENDED || v->status >= KJ_ST_COUNT || v->notice >= KJ_N_COUNT) return false;
        if (v->my_lock != KJ_CARD_NONE && v->my_lock >= KJ_CARD_TYPES) return false;
        if (v->res_my != KJ_CARD_NONE && v->res_my >= KJ_CARD_TYPES) return false;
        if (v->res_opp != KJ_CARD_NONE && v->res_opp >= KJ_CARD_TYPES) return false;
        if (v->res_outcome > KJ_OUT_DRAW || v->final_reason > KJ_FINAL_TIME_UP) return false;
        for (int c = 0; c < KJ_CARD_TYPES; c++) {
            if (v->cards[c] > KJ_CARDS_PER_TYPE) return false;
        }
        break;
    }
    case KJ_F_NAME: {
        kj_name_t *nm = &out->u.name;
        nm->no = get8(&r);
        nm->flags = get8(&r);
        size_t n = get8(&r);
        if (nm->no == 0 || nm->no > KJ_MAX_PLAYERS || n > KJ_NAME_MAX) return false;
        if (len < KJ_FRAME_HEADER + NAME_PAYLOAD_MIN + n) return false;
        getn(&r, (uint8_t *)nm->name, n);
        nm->name[n] = '\0';
        if (!name_ok(nm->name, n)) return false;
        break;
    }
    default:
        return false;
    }
    return true;
}

bool kj_outbox_push(kj_outbox_t *o, const uint8_t *mac, const kj_frame_t *f)
{
    if (o->count >= KJ_OUTBOX_MAX) {
        o->dropped++;
        return false;
    }
    kj_out_t *it = &o->items[o->count];
    size_t len = kj_proto_encode(f, it->data, sizeof(it->data));
    if (len == 0) return false;
    it->len = (uint8_t)len;
    it->broadcast = mac == NULL;
    if (mac) {
        memcpy(it->mac, mac, 6);
    } else {
        memset(it->mac, 0xFF, 6);
    }
    o->count++;
    return true;
}
