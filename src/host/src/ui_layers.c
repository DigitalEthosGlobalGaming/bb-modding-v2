/* SPDX-License-Identifier: GPL-2.0-or-later
 * Retained UI layers (ABI 1.11). Mods publish validated draw lists; the host owns
 * the 60 Hz fade clock, arbitration of the foreground interactive layer and input
 * capture. Nothing here calls into a mod to draw. */
#include "internal.h"
#include <math.h>

#define LAYER_LIMIT 64
#define LAYER_PER_MOD 8
#define STREAM_LIMIT (256u * 1024u)
#define COMMAND_LIMIT 4096u
#define FADE_TICKS 12u /* 6 authored frames at 30 fps = 12 ticks at 60 Hz */

typedef struct mh_layer {
    bbm_id id;
    struct bbm_mod *owner;
    bbm_ui_layer d;
    uint8_t *cmds;
    size_t n;
    uint64_t seq;
    int phase; /* 0 hidden, 1 fading in, 2 shown, 3 fading out */
    uint64_t start_tick;
    float from;
} mh_layer;

struct mh_layers {
    mh_layer *v[LAYER_LIMIT];
    size_t n;
    uint64_t seq;
};

static uint64_t tick_at(uint64_t now_ms) { return now_ms * 60u / 1000u; }

static float progress(const mh_layer *l, uint64_t tick) {
    uint64_t e = tick > l->start_tick ? tick - l->start_tick : 0;
    return e >= FADE_TICKS ? 1.0f : (float)e / (float)FADE_TICKS;
}
static float alpha_of(const mh_layer *l, uint64_t tick) {
    switch (l->phase) {
    case 0: return 0.0f;
    case 2: return 1.0f;
    case 1: return l->from + (1.0f - l->from) * progress(l, tick);
    default: return l->from * (1.0f - progress(l, tick));
    }
}
static void settle(mh_layer *l, uint64_t tick) {
    if (l->phase == 1 && progress(l, tick) >= 1.0f) l->phase = 2;
    else if (l->phase == 3 && progress(l, tick) >= 1.0f) l->phase = 0;
}
static void set_wanted(mh_layer *l, int want, uint64_t tick) {
    int wanted = l->phase == 1 || l->phase == 2;
    if (want == wanted) return;
    l->from = alpha_of(l, tick);
    l->phase = want ? 1 : 3;
    l->start_tick = tick;
}

static int interactive(const mh_layer *l) { return l->d.kind == BBM_UI_SCREEN || l->d.kind == BBM_UI_MODAL; }
static int owner_live(const mh_layer *l) {
    struct bbm_mod *m = l->owner;
    return !m->faulted && !m->bypassed && m->entry->state == MODHOST_ACTIVE;
}

static mh_layer *foreground(modhost *h) {
    mh_layer *best = NULL;
    if (!h->layers) return NULL;
    for (size_t i = 0; i < h->layers->n; ++i) {
        mh_layer *l = h->layers->v[i];
        if (!interactive(l) || !owner_live(l) || !(l->phase == 1 || l->phase == 2)) continue;
        if (!best || l->d.kind > best->d.kind ||
            (l->d.kind == best->d.kind && (l->d.order > best->d.order ||
             (l->d.order == best->d.order && l->seq > best->seq)))) best = l;
    }
    return best;
}
static void refresh_capture(modhost *h) {
    h->ui_capture = foreground(h) != NULL;
    h->ui_count = h->layers ? (int32_t)h->layers->n : 0;
}

/* Walk a stream; reject anything malformed. Returns the command count or -1. */
static int is_fin(float v) { return isfinite(v) && fabsf(v) < 100000.0f; }
static int rd_f32(const uint8_t *p, size_t n, size_t *at, float *out) {
    if (*at + 4 > n) return 0;
    uint32_t u = (uint32_t)p[*at] | (uint32_t)p[*at + 1] << 8 | (uint32_t)p[*at + 2] << 16 | (uint32_t)p[*at + 3] << 24;
    memcpy(out, &u, 4);
    *at += 4;
    return is_fin(*out);
}
static int printable(const uint8_t *s, size_t n) {
    for (size_t i = 0; i < n; ++i) if (s[i] < 32 || s[i] == 127) return 0;
    return 1;
}
static int validate_stream(const uint8_t *p, size_t n) {
    if (n < 12 || n > STREAM_LIMIT || memcmp(p, "BBUI", 4) || p[4] != 1 || p[5] || p[6] || p[7]) return -1;
    uint32_t count = (uint32_t)p[8] | (uint32_t)p[9] << 8 | (uint32_t)p[10] << 16 | (uint32_t)p[11] << 24;
    if (count > COMMAND_LIMIT) return -1;
    size_t at = 12;
    for (uint32_t i = 0; i < count; ++i) {
        if (at >= n) return -1;
        uint8_t op = p[at++];
        float f;
        for (int k = 0; k < 4; ++k) if (!rd_f32(p, n, &at, &f)) return -1;
        if (op == 1) { if (at + 4 > n) return -1; at += 4; }
        else if (op == 2) { if (!rd_f32(p, n, &at, &f) || at + 4 > n) return -1; at += 4; }
        else if (op == 3) {
            if (!rd_f32(p, n, &at, &f) || f <= 0.0f || at + 4 + 2 + 2 > n) return -1;
            at += 4;
            if (p[at] > 2 || p[at + 1] > 1) return -1;
            size_t len = (size_t)p[at + 2] | (size_t)p[at + 3] << 8;
            at += 4;
            if (len > 1024 || at + len > n || !printable(p + at, len)) return -1;
            at += len;
        } else if (op == 4) {
            if (at + 5 > n) return -1;
            at += 4;
            size_t len = p[at++];
            if (!len || at + len > n || !printable(p + at, len)) return -1;
            at += len;
        } else return -1;
    }
    return at == n ? (int)count : -1;
}

static bbm_status layer_set(bbm_handle m, bbm_id *layer, const bbm_ui_layer *d, const void *cmds, size_t n) {
    if (!m || !m->host || !layer || !d || !cmds || d->size != sizeof *d || m->faulted || m->bypassed) return BBM_ERR_ARG;
    if (d->kind < BBM_UI_HUD || d->kind > BBM_UI_MODAL || (d->flags & ~BBM_UI_VISIBLE)) return BBM_ERR_ARG;
    if (d->kind == BBM_UI_MODAL && (!is_fin(d->x) || !is_fin(d->y) || !is_fin(d->w) || !is_fin(d->h) ||
                                    d->w <= 0.0f || d->h <= 0.0f)) return BBM_ERR_ARG;
    if (validate_stream(cmds, n) < 0) return BBM_ERR_ARG;
    modhost *h = m->host;
    plat_mutex_lock(h->run_lock);
    bbm_status rc = BBM_OK;
    uint64_t tick = tick_at(plat_now_ms());
    if (!h->layers && !(h->layers = calloc(1, sizeof *h->layers))) { plat_mutex_unlock(h->run_lock); return BBM_ERR_NOMEM; }
    mh_layer *l = NULL;
    if (*layer) {
        for (size_t i = 0; i < m->nres; ++i)
            if (m->res[i].id == *layer && m->res[i].kind == MH_RES_UI_LAYER) l = m->res[i].p;
        if (!l) rc = BBM_ERR_NOTFOUND;
        else if (l->d.kind != d->kind) rc = BBM_ERR_ARG;
    } else {
        size_t mine = 0;
        for (size_t i = 0; i < h->layers->n; ++i) mine += h->layers->v[i]->owner == m;
        if (mine >= LAYER_PER_MOD || h->layers->n >= LAYER_LIMIT) rc = BBM_ERR_STATE;
    }
    uint8_t *copy = NULL;
    if (!rc && !(copy = malloc(n))) rc = BBM_ERR_NOMEM;
    if (!rc) {
        memcpy(copy, cmds, n);
        if (!l) {
            l = calloc(1, sizeof *l);
            bbm_id id = l ? mh_res_add(m, MH_RES_UI_LAYER, l) : 0;
            if (!id) { free(l); free(copy); rc = BBM_ERR_NOMEM; }
            else { l->id = id; l->owner = m; l->seq = ++h->layers->seq; h->layers->v[h->layers->n++] = l; *layer = id; }
        }
    }
    if (!rc) {
        free(l->cmds);
        l->cmds = copy;
        l->n = n;
        l->d = *d;
        settle(l, tick);
        set_wanted(l, (d->flags & BBM_UI_VISIBLE) != 0, tick);
        refresh_capture(h);
    }
    plat_mutex_unlock(h->run_lock);
    return rc;
}

void mh_ui_layer_release(struct bbm_mod *m, void *p) {
    mh_layer *l = p;
    modhost *h = m->host;
    if (h->layers) {
        for (size_t i = 0; i < h->layers->n; ++i) {
            if (h->layers->v[i] != l) continue;
            memmove(&h->layers->v[i], &h->layers->v[i + 1], (h->layers->n - i - 1) * sizeof h->layers->v[0]);
            --h->layers->n;
            break;
        }
    }
    free(l->cmds);
    free(l);
    refresh_capture(h);
}

void mh_ui_layers_free(modhost *h) { free(h->layers); h->layers = NULL; }
void mh_ui_layers_tick(modhost *h) { if (h->layers) refresh_capture(h); }

static int cmp_view(const void *a, const void *b) {
    const mh_layer *x = *(mh_layer *const *)a, *y = *(mh_layer *const *)b;
    if (x->d.kind != y->d.kind) return x->d.kind < y->d.kind ? -1 : 1;
    if (x->d.order != y->d.order) return x->d.order < y->d.order ? -1 : 1;
    return x->seq < y->seq ? -1 : x->seq > y->seq;
}

size_t modhost_ui_layers_at(modhost *h, uint64_t now_ms, modhost_ui_layer_view *out, size_t cap) {
    if (!h->layers || !out || !cap) return 0;
    uint64_t tick = tick_at(now_ms);
    mh_layer *order[LAYER_LIMIT];
    size_t n = 0;
    for (size_t i = 0; i < h->layers->n; ++i) {
        mh_layer *l = h->layers->v[i];
        settle(l, tick);
        if (l->phase != 0 && owner_live(l)) order[n++] = l;
    }
    qsort(order, n, sizeof order[0], cmp_view);
    mh_layer *fg = foreground(h);
    size_t k = 0;
    for (size_t i = 0; i < n && k < cap; ++i) {
        mh_layer *l = order[i];
        out[k++] = (modhost_ui_layer_view){l->id, l->d.kind, alpha_of(l, tick), l->d.x, l->d.y, l->d.w, l->d.h,
                                           l == fg, l->cmds, l->n, l->owner->entry->m.id};
    }
    refresh_capture(h);
    return k;
}
size_t modhost_ui_layers(modhost *h, modhost_ui_layer_view *out, size_t cap) {
    return modhost_ui_layers_at(h, plat_now_ms(), out, cap);
}

int modhost_ui_captures(modhost *h) { return h->ui_capture != 0; }
int modhost_ui_active(modhost *h) { return h->ui_count != 0; }

void modhost_ui_input(modhost *h, uint32_t action) {
    if (!h->ui_capture || action < BBM_UI_ACT_UP || action > BBM_UI_ACT_NEXT_TAB) return;
    mh_enqueue(h, MH_IN_UI_ACTION, 0, action, 0, 0, NULL, 0);
}

/* Sync point: deliver to the owner of the foreground layer, if it subscribed. */
void mh_ui_action_deliver(modhost *h, uint32_t action) {
    mh_layer *l = foreground(h);
    if (!l) return;
    struct bbm_mod *m = l->owner;
    if (!(m->events & BBM_EV_UI_ACTION) || !m->api.on_event) return;
    bbm_ui_action a = {sizeof a, action, l->id};
    bbm_event ev = {BBM_EV_UI_ACTION, 0, action, l->id, &a, sizeof a};
    mh_dispatch_event_to(m, &ev);
}

void mh_ui_layers_fill(bbm_host *api) { api->ui_layer_set = layer_set; }
