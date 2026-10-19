/* SPDX-License-Identifier: GPL-2.0-or-later
 * ModNet: a small multiplexed layer over bbport's session transport.
 *
 * Frame (little endian):
 *   0  char   magic[4] = "BBMD"
 *   4  u8     version  = 1
 *   5  u8     flags    (BBM_NET_* ; bit 7 = control frame)
 *   6  u16    channel  (wire id low 16 for data; control opcode for control)
 *   8  u32    channel_hi (wire id high 32 bits, data frames)
 *   12 u32    seq
 *   16 u16    len
 *   18 ...    payload (<= 1200)
 *
 * Reliable/ordered are delivered by bbport's transport (shadNet room messages
 * are reliable and ordered); unreliable uses the direct UDP path. This layer
 * only frames, budgets and demultiplexes. */
#include "internal.h"

#define MODNET_VER 1
#define MODNET_HDR 18
#define MODNET_MAX_PAYLOAD 1200
#define CTRL_FLAG 0x80
#define CTRL_HELLO 1    /* payload: mod-set hash (u64) + count (u16) + ids */
#define CTRL_ACCEPT 2
#define CTRL_REJECT 3   /* payload: UTF-8 diff text */

uint32_t mh_hash32(const char *a, const char *b) {
    uint32_t h = 2166136261u; /* FNV-1a */
    for (const char *p = a; p && *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    if (b) { h = (h ^ (uint8_t)':') * 16777619u; for (const char *p = b; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u; }
    return h;
}

uint64_t mh_modset_hash(modhost *h) {
    /* Order-independent: XOR of per-mod hashes of id + protocol, for session
     * and host-authoritative code mods only. */
    uint64_t acc = 0;
    for (size_t i = 0; i < h->nmods; i++) {
        mh_entry *e = &h->mods[i];
        if (e->state != MODHOST_ACTIVE || !e->has_code) continue;
        if (e->m.scope == MODHOST_SCOPE_CLIENT) continue;
        char proto[16];
        snprintf(proto, sizeof proto, "%u", e->m.protocol);
        uint64_t x = ((uint64_t)mh_hash32(e->m.id, proto) << 32) | mh_hash32(proto, e->m.id);
        acc ^= x;
    }
    return acc;
}

uint64_t modhost_modset_hash(modhost *h) { return mh_modset_hash(h); }

mh_peer *mh_peer_get(modhost *h, uint64_t id, int create) {
    for (size_t i = 0; i < h->npeers; i++)
        if (h->peers[i].id == id) return &h->peers[i];
    if (!create || h->npeers >= sizeof h->peers / sizeof h->peers[0]) return NULL;
    mh_peer *p = &h->peers[h->npeers++];
    memset(p, 0, sizeof *p);
    p->id = id;
    p->modset_ok = -1;
    p->tokens = h->cfg.net_budget_bytes_per_sec ? h->cfg.net_budget_bytes_per_sec : 16384;
    p->last_refill = plat_now_s();
    return p;
}

static int budget_take(modhost *h, mh_peer *p, size_t bytes) {
    double rate = h->cfg.net_budget_bytes_per_sec ? h->cfg.net_budget_bytes_per_sec : 16384;
    double now = plat_now_s();
    p->tokens += (now - p->last_refill) * rate;
    if (p->tokens > rate) p->tokens = rate; /* one second of burst */
    p->last_refill = now;
    if (p->tokens < (double)bytes) return 0;
    p->tokens -= (double)bytes;
    return 1;
}

static int frame_out(modhost *h, uint64_t peer, uint8_t flags, uint32_t chan, uint32_t seq,
                     const void *data, size_t len) {
    if (len > MODNET_MAX_PAYLOAD) return BBM_ERR_ARG;
    uint8_t buf[MODNET_HDR + MODNET_MAX_PAYLOAD];
    memcpy(buf, "BBMD", 4);
    buf[4] = MODNET_VER;
    buf[5] = flags;
    uint16_t clo = (uint16_t)(chan & 0xFFFF);
    memcpy(buf + 6, &clo, 2);
    uint32_t chi = chan >> 16;
    memcpy(buf + 8, &chi, 4);
    memcpy(buf + 12, &seq, 4);
    uint16_t l = (uint16_t)len;
    memcpy(buf + 16, &l, 2);
    if (len) memcpy(buf + MODNET_HDR, data, len);
    size_t total = MODNET_HDR + len;
    if (!h->cfg.net.send) return BBM_ERR_UNSUPPORTED;
    int reliable = (flags & (BBM_NET_RELIABLE | CTRL_FLAG)) != 0;
    return h->cfg.net.send(h->cfg.net.user, peer, buf, total, reliable) == 0 ? BBM_OK : BBM_ERR;
}

bbm_status mh_net_send(struct bbm_mod *m, mh_channel *c, uint64_t peer, const void *data, size_t len) {
    modhost *h = m->host;
    if (!h->session.in_session) return BBM_ERR_STATE;
    if (len > MODNET_MAX_PAYLOAD) return BBM_ERR_ARG;
    size_t frame = MODNET_HDR + len;

    if (peer) {
        mh_peer *p = mh_peer_get(h, peer, 1);
        if (!p || !budget_take(h, p, frame)) return BBM_ERR_BUDGET;
        m->entry->net_out += frame;
        return frame_out(h, peer, (uint8_t)c->flags, c->wire_id, c->send_seq++, data, len);
    }
    bbm_status rc = BBM_OK;
    uint32_t seq = c->send_seq++;
    for (size_t i = 0; i < h->npeers; i++) {
        mh_peer *p = &h->peers[i];
        if (!budget_take(h, p, frame)) { rc = BBM_ERR_BUDGET; continue; }
        m->entry->net_out += frame;
        bbm_status r = frame_out(h, p->id, (uint8_t)c->flags, c->wire_id, seq, data, len);
        if (r != BBM_OK) rc = r;
    }
    return rc;
}

static mh_channel *find_channel(modhost *h, uint32_t wire_id, struct bbm_mod **owner) {
    for (size_t i = 0; i < h->nmods; i++) {
        struct bbm_mod *m = h->mods[i].inst;
        if (!m || h->mods[i].state != MODHOST_ACTIVE || m->bypassed) continue;
        for (size_t j = 0; j < m->nres; j++) {
            if (m->res[j].kind != MH_RES_CHANNEL) continue;
            mh_channel *c = m->res[j].p;
            if (c->wire_id == wire_id) { *owner = m; return c; }
        }
    }
    return NULL;
}

/* Build the local id list payload: u64 hash, u16 count, then id strings. */
static size_t build_hello(modhost *h, uint8_t *out, size_t cap) {
    uint64_t hash = mh_modset_hash(h);
    size_t n = 0;
    if (cap < 10) return 0;
    memcpy(out, &hash, 8);
    n = 10; /* reserve count */
    uint16_t count = 0;
    for (size_t i = 0; i < h->nmods; i++) {
        mh_entry *e = &h->mods[i];
        if (e->state != MODHOST_ACTIVE || !e->has_code || e->m.scope == MODHOST_SCOPE_CLIENT) continue;
        int len = snprintf((char *)out + n, cap - n, "%s@%u;", e->m.id, e->m.protocol);
        if (len < 0 || (size_t)len >= cap - n) break;
        n += (size_t)len;
        count++;
    }
    memcpy(out + 8, &count, 2);
    return n;
}

void mh_net_send_handshake(modhost *h, uint64_t peer) {
    uint8_t buf[MODNET_MAX_PAYLOAD];
    size_t n = build_hello(h, buf, sizeof buf);
    frame_out(h, peer, CTRL_FLAG, CTRL_HELLO, 0, buf, n);
}

static void handle_control(modhost *h, uint64_t peer, uint16_t opcode, const uint8_t *data, size_t len) {
    mh_peer *p = mh_peer_get(h, peer, 1);
    if (opcode == CTRL_HELLO) {
        uint64_t their_hash = 0;
        if (len >= 8) memcpy(&their_hash, data, 8);
        uint64_t mine = mh_modset_hash(h);
        if (their_hash == mine) {
            if (p) p->modset_ok = 1;
            frame_out(h, peer, CTRL_FLAG, CTRL_ACCEPT, 0, NULL, 0);
        } else {
            if (p) p->modset_ok = 0;
            char diff[256];
            int dl = snprintf(diff, sizeof diff, "mod set differs (theirs %016llx, mine %016llx)",
                              (unsigned long long)their_hash, (unsigned long long)mine);
            frame_out(h, peer, CTRL_FLAG, CTRL_REJECT, 0, diff, (size_t)dl);
            bbm_event ev = {BBM_EV_MODSET_MISMATCH, 0, peer, 0, diff, (size_t)dl};
            mh_dispatch_event(h, &ev);
        }
    } else if (opcode == CTRL_ACCEPT) {
        if (p) p->modset_ok = 1;
    } else if (opcode == CTRL_REJECT) {
        if (p) p->modset_ok = 0;
        bbm_event ev = {BBM_EV_MODSET_MISMATCH, 0, peer, 0, data, len};
        mh_dispatch_event(h, &ev);
    }
}

void mh_net_receive(modhost *h, uint64_t peer, const uint8_t *frame, size_t len) {
    if (len < MODNET_HDR || memcmp(frame, "BBMD", 4) || frame[4] != MODNET_VER) return;
    uint8_t flags = frame[5];
    uint16_t clo;
    memcpy(&clo, frame + 6, 2);
    uint32_t chi;
    memcpy(&chi, frame + 8, 4);
    uint32_t seq;
    memcpy(&seq, frame + 12, 4);
    uint16_t plen;
    memcpy(&plen, frame + 16, 2);
    if (MODNET_HDR + plen > len) return;
    const uint8_t *payload = frame + MODNET_HDR;

    if (flags & CTRL_FLAG) { handle_control(h, peer, clo, payload, plen); return; }

    mh_peer *p = mh_peer_get(h, peer, 1);
    if (p && p->modset_ok == 0) return; /* ignore data from incompatible peers */

    uint32_t wire = ((uint32_t)chi << 16) | clo;
    struct bbm_mod *owner = NULL;
    mh_channel *c = find_channel(h, wire, &owner);
    if (!c || !c->cb) return;
    /* Session and host-authoritative mods only talk to peers whose mod set
     * was confirmed by the handshake. Until then (-1) the data is dropped. */
    if (owner->entry->m.scope != MODHOST_SCOPE_CLIENT && (!p || p->modset_ok != 1)) return;
    mh_call call = {.kind = MH_CALL_NET, .f = (void (*)(void))c->cb, .a = c->user, .u = peer, .b = payload, .n = plen};
    mh_call_mod(owner, &call);
}
