/* SPDX-License-Identifier: GPL-2.0-or-later
 * The integrator surface declared in modhost.h.
 *
 * Threads: the game thread owns the host (modhost_frame, the *_now calls, scan,
 * destroy) and holds run_lock while it works. The overlay thread draws mod
 * panels under the same (recursive) lock but only ever try-locks it, so a
 * reload in progress costs one overlay frame instead of a stall. Anything that
 * arrives from network threads is queued and handled at the sync point. */
#include "internal.h"

/* order_enabled lives in loader.c. */
void order_enabled(modhost *h, mh_entry **list, size_t *n);

static void clear_shadow(void *user, const char *name, int is_dir) {
    modhost *h = user;
    size_t n = strlen(name);
    if (is_dir || n < 4 || strcmp(name + n - 4, ".dll")) return;
    char path[MH_PATH];
    mh_path_join(path, sizeof path, h->cache_dir, name);
    plat_remove(path);
}

modhost *modhost_create(const modhost_config *cfg) {
    modhost *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    h->cfg = *cfg;
    mh_strlcpy(h->mods_dir, cfg->mods_dir ? cfg->mods_dir : "mods", sizeof h->mods_dir);
    mh_strlcpy(h->cache_dir, cfg->cache_dir ? cfg->cache_dir : "modcache", sizeof h->cache_dir);
    h->req_lock = plat_mutex_new();
    h->run_lock = plat_mutex_new_recursive();
    h->vfs_lock = plat_mutex_new();
    h->in_lock = plat_mutex_new();
    mh_fill_api(&h->api);
    h->last_discovery_ms = plat_now_ms();
    plat_mkdirs(h->cache_dir);
    /* Shadow copies left by a previous run (crash, leaked DLL) are stale now. */
    plat_list_dir(h->cache_dir, clear_shadow, h);
    mh_native_observation_start(h);
    mh_item_actions_start(h);
    mh_menu_start(h);
    if(!h->menu){modhost_destroy(h);return NULL;}
    return h;
}

static void free_inbox(modhost *h) {
    for (size_t i = 0; i < h->nin; i++) free(h->inq[i].data);
    h->nin = 0;
}

void modhost_destroy(modhost *h) {
    if (!h) return;
    plat_mutex_lock(h->run_lock);
    mh_item_actions_stop(h);
    mh_native_observation_stop(h);
    for (size_t i = h->nmods; i-- > 0;)
        if (h->mods[i].inst) mh_unload(h, &h->mods[i], 0, NULL);
    mh_menu_stop(h);
    mh_inventory_stop(h);
    mh_characters_stop(h);
    mh_deferred_flush(h);
    plat_mutex_unlock(h->run_lock);
    free_inbox(h);
    for (size_t i = 0; i < h->nmods; i++) mh_settings_free(&h->mods[i]);
    mh_ui_layers_free(h);
    free(h->mods);
    free(h->vfs);
    plat_mutex_free(h->req_lock);
    plat_mutex_free(h->run_lock);
    plat_mutex_free(h->vfs_lock);
    plat_mutex_free(h->in_lock);
    free(h);
}

int modhost_try_lock(modhost *h) { return plat_mutex_trylock(h->run_lock); }
void modhost_unlock(modhost *h) { plat_mutex_unlock(h->run_lock); }

int modhost_scan(modhost *h) {
    plat_mutex_lock(h->run_lock);
    mh_discover(h);
    mh_entry **order = malloc((h->nmods ? h->nmods : 1) * sizeof *order);
    if (!order) { plat_mutex_unlock(h->run_lock); return -1; }
    size_t n = 0;
    order_enabled(h, order, &n);
    for (size_t i = 0; i < n; i++)
        if (order[i]->state != MODHOST_ACTIVE) mh_load(h, order[i]);
    mh_vfs_rebuild_assets(h);
    free(order);
    h->last_discovery_ms = plat_now_ms();
    int count = (int)h->nmods;
    plat_mutex_unlock(h->run_lock);
    return count;
}

static void run_requests(modhost *h) {
    mh_req local[64];
    size_t n;
    plat_mutex_lock(h->req_lock);
    n = h->nreqs;
    memcpy(local, h->reqs, n * sizeof local[0]);
    h->nreqs = 0;
    plat_mutex_unlock(h->req_lock);
    for (size_t i = 0; i < n; i++) {
        mh_entry *e = mh_find(h, local[i].id);
        if (!e) continue;
        if (local[i].kind == MH_REQ_LOAD) mh_load(h, e);
        else if (local[i].kind == MH_REQ_UNLOAD) mh_unload(h, e, 0, NULL);
        else if (local[i].kind == MH_REQ_RELOAD) mh_reload(h, e);
    }
}

/* ---- Queued events (network threads -> sync point) ----------------------- */

void mh_enqueue(modhost *h, mh_in_kind kind, uint64_t peer, uint64_t a, uint64_t b, int flag,
                    const void *data, size_t len) {
    uint8_t *copy = NULL;
    if (len) {
        copy = malloc(len);
        if (!copy) return;
        memcpy(copy, data, len);
    }
    plat_mutex_lock(h->in_lock);
    if (h->nin < sizeof h->inq / sizeof h->inq[0]) {
        mh_in *e = &h->inq[h->nin++];
        e->kind = kind; e->peer = peer; e->a = a; e->b = b; e->flag = flag;
        e->data = copy; e->len = len;
        copy = NULL;
    }
    plat_mutex_unlock(h->in_lock);
    free(copy); /* queue full: dropped */
}

static void session_begin(modhost *h, uint64_t local_peer, uint64_t owner_peer, int dev) {
    h->session.in_session = 1;
    h->session.is_owner = (local_peer == owner_peer);
    h->session.dev_session = dev ? 1 : 0;
    h->session.local_peer = local_peer;
    h->session.owner_peer = owner_peer;
    h->session.peer_count = 0;
    h->npeers = 0;
    bbm_event ev = {BBM_EV_SESSION_JOIN, 0, local_peer, 0, NULL, 0};
    mh_dispatch_event(h, &ev);
}

static void session_end(modhost *h) {
    bbm_event ev = {BBM_EV_SESSION_LEAVE, 0, 0, 0, NULL, 0};
    mh_dispatch_event(h, &ev);
    h->session.in_session = 0;
    h->session.peer_count = 0;
    h->npeers = 0;
}

static void peer_join(modhost *h, uint64_t peer) {
    mh_peer_get(h, peer, 1);
    h->session.peer_count = (uint32_t)h->npeers;
    mh_net_send_handshake(h, peer); /* exchange mod sets */
    bbm_event ev = {BBM_EV_PEER_JOIN, 0, peer, 0, NULL, 0};
    mh_dispatch_event(h, &ev);
}

static void peer_leave(modhost *h, uint64_t peer) {
    for (size_t i = 0; i < h->npeers; i++) {
        if (h->peers[i].id == peer) {
            h->peers[i] = h->peers[--h->npeers];
            break;
        }
    }
    h->session.peer_count = (uint32_t)h->npeers;
    bbm_event ev = {BBM_EV_PEER_LEAVE, 0, peer, 0, NULL, 0};
    mh_dispatch_event(h, &ev);
}

static void drain_inbox(modhost *h) {
    mh_in local[256];
    size_t n;
    plat_mutex_lock(h->in_lock);
    n = h->nin;
    memcpy(local, h->inq, n * sizeof local[0]);
    h->nin = 0;
    plat_mutex_unlock(h->in_lock);
    for (size_t i = 0; i < n; i++) {
        mh_in *e = &local[i];
        switch (e->kind) {
        case MH_IN_NET: mh_net_receive(h, e->peer, e->data, e->len); break;
        case MH_IN_SESSION_BEGIN: session_begin(h, e->a, e->b, e->flag); break;
        case MH_IN_SESSION_END: session_end(h); break;
        case MH_IN_PEER_JOIN: peer_join(h, e->peer); break;
        case MH_IN_PEER_LEAVE: peer_leave(h, e->peer); break;
        case MH_IN_UI_ACTION: mh_ui_action_deliver(h, (uint32_t)e->a); break;
        case MH_IN_SETTING: {
            typedef struct { char id[MH_NAME]; bbm_setting_event ev; } setting_blob;
            if (e->len == sizeof(setting_blob)) {
                const setting_blob *b = (const void *)e->data;
                mh_entry *t = mh_find(h, b->id);
                if (t && t->inst && t->state == MODHOST_ACTIVE) {
                    bbm_event ev = {BBM_EV_SETTING_CHANGED, 0, 0, 0, &b->ev, sizeof b->ev};
                    mh_dispatch_event_to(t->inst, &ev);
                }
            }
            break;
        }
        case MH_IN_EVENT: {
            bbm_event ev = {(uint32_t)e->flag, 0, e->a, e->b, e->data, e->len};
            mh_dispatch_event(h, &ev);
            break;
        }
        }
        free(e->data);
    }
}

void modhost_post_event(modhost *h, uint32_t type, uint64_t a, uint64_t b) {
    mh_enqueue(h, MH_IN_EVENT, 0, a, b, (int)type, NULL, 0);
}
void modhost_session_begin(modhost *h, uint64_t local_peer, uint64_t owner_peer, int dev_session) {
    mh_enqueue(h, MH_IN_SESSION_BEGIN, 0, local_peer, owner_peer, dev_session, NULL, 0);
}
void modhost_session_end(modhost *h) { mh_enqueue(h, MH_IN_SESSION_END, 0, 0, 0, 0, NULL, 0); }
void modhost_peer_join(modhost *h, uint64_t peer) { mh_enqueue(h, MH_IN_PEER_JOIN, peer, 0, 0, 0, NULL, 0); }
void modhost_peer_leave(modhost *h, uint64_t peer) { mh_enqueue(h, MH_IN_PEER_LEAVE, peer, 0, 0, 0, NULL, 0); }
void modhost_net_receive(modhost *h, uint64_t peer, const void *frame, size_t len) {
    if (len) mh_enqueue(h, MH_IN_NET, peer, 0, 0, 0, frame, len);
}

/* ---- The sync point ------------------------------------------------------- */

static void sample_input(modhost *h) {
    memcpy(h->keys_prev, h->keys_now, sizeof h->keys_now);
    h->pad_prev = h->pad_now;
    memset(h->keys_now, 0, sizeof h->keys_now);
    h->pad_now = 0;
    modhost_input_backend *in = &h->cfg.input;
    if (in->keyboard) {
        int count = 0;
        const uint8_t *k = in->keyboard(in->user, &count);
        if (k) {
            if (count > BBM_KEY_COUNT) count = BBM_KEY_COUNT;
            for (int i = 0; i < count; i++) h->keys_now[i] = k[i] ? 1 : 0;
        }
    }
    if (in->pad_buttons) h->pad_now = in->pad_buttons(in->user);
}

/* Called only at the sync point with run_lock held. Existing entries and
 * their runtime states are left intact; only newly appended entries load. */
static void discover_tick(modhost *h) {
    uint64_t now = plat_now_ms();
    if (now - h->last_discovery_ms < 5000) return;
    h->last_discovery_ms = now;
    size_t before = h->nmods;
    mh_discover_new(h);
    if (h->nmods == before) {
        int pending = 0;
        for (size_t i = 0; i < h->nmods; ++i)
            if (h->mods[i].state == MODHOST_DISCOVERED && h->mods[i].m.enabled && !h->cfg.safe_mode) pending = 1;
        if (!pending) return;
    }
    mh_entry **order = malloc(h->nmods * sizeof *order);
    if (!order) return;
    size_t n = 0;
    order_enabled(h, order, &n);
    for (size_t i = 0; i < n; ++i) {
        if (order[i]->state == MODHOST_DISCOVERED)
            mh_load(h, order[i]);
    }
    free(order);
    mh_vfs_rebuild_assets(h);
    if (h->nmods != before) mh_log(h, NULL, BBM_LOG_INFO, "Discovered %zu new mod(s)", h->nmods - before);
}

void modhost_frame(modhost *h) {
    plat_mutex_lock(h->run_lock);
    h->frame_no++;
    discover_tick(h);
    sample_input(h);
    run_requests(h);
    if (h->cfg.dev_mode) mh_watch(h);
    run_requests(h); /* watcher may have queued a reload */
    h->gameplay_in_frame = 1;
    drain_inbox(h);
    mh_gameplay_tick(h);
    mh_equipment_tick(h);
    mh_inventory_tick(h);
    mh_characters_tick(h);
    mh_native_observation_tick(h);
    mh_item_actions_tick(h);
    mh_menu_tick(h);

    /* One host-owned observation, fanned out to all interested active mods.
     * No subscriber installs a detour or gets a mutable game pointer. Dispatch
     * after lifecycle requests so unloaded DLLs cannot receive an event. */
    if (h->pad_now != h->pad_prev) {
        bbm_event ev = {BBM_EV_PAD_CHANGED, 0, h->pad_now, h->pad_prev, NULL, 0};
        mh_dispatch_event(h, &ev);
    }

    for (size_t i = 0; i < h->nmods; i++) {
        struct bbm_mod *m = h->mods[i].inst;
        if (!m || h->mods[i].state != MODHOST_ACTIVE || m->bypassed) continue;
        double t0 = plat_now_s();
        /* Registration/release from a callback can move the resource array.
         * Snapshot IDs and values, then recheck ownership before each call.
         * Newly registered callbacks start on the next frame. */
        typedef struct { bbm_id id; mh_frame_cb value; } frame_item;
        size_t count = 0;
        for (size_t j = 0; j < m->nres; ++j)
            if (m->res[j].kind == MH_RES_FRAME) ++count;
        frame_item local[16];
        frame_item *items = count > 16 ? calloc(count, sizeof *items) : local;
        if (items) {
            size_t at = 0;
            for (size_t j = 0; j < m->nres; ++j)
                if (m->res[j].kind == MH_RES_FRAME)
                    items[at++] = (frame_item){m->res[j].id, *(mh_frame_cb *)m->res[j].p};
            for (size_t j = 0; j < count && !m->faulted; ++j) {
                int live = 0;
                for (size_t k = 0; k < m->nres; ++k)
                    if (m->res[k].id == items[j].id && m->res[k].kind == MH_RES_FRAME) { live = 1; break; }
                if (!live || !items[j].value.cb) continue;
                mh_call c = {.kind = MH_CALL_VOID1, .f = (void (*)(void))items[j].value.cb, .a = items[j].value.user};
                mh_call_mod(m, &c);
            }
            if (items != local) free(items);
        }
        h->mods[i].frame_ms = (plat_now_s() - t0) * 1000.0;
        if (h->mods[i].frame_ms > 2.0)
            mh_log(h, h->mods[i].m.id, BBM_LOG_WARN, "frame callback took %.1f ms", h->mods[i].frame_ms);
    }
    h->gameplay_in_frame = 0;
    run_requests(h); /* unloads queued by a crash in this frame */
    mh_pad_tick(h, plat_now_ms());
    mh_ui_layers_tick(h);
    mh_deferred_tick(h);
    plat_mutex_unlock(h->run_lock);
}

/* ---- Control ------------------------------------------------------------ */

int modhost_request_load(modhost *h, const char *id) { mh_request(h, MH_REQ_LOAD, id); return 0; }
int modhost_request_unload(modhost *h, const char *id) { mh_request(h, MH_REQ_UNLOAD, id); return 0; }
int modhost_request_reload(modhost *h, const char *id) { mh_request(h, MH_REQ_RELOAD, id); return 0; }

typedef int (*entry_op)(modhost *h, mh_entry *e);
static int unload_op(modhost *h, mh_entry *e) { return mh_unload(h, e, 0, NULL); }
static int locked_op(modhost *h, const char *id, entry_op op) {
    plat_mutex_lock(h->run_lock);
    mh_entry *e = mh_find(h, id);
    int rc = e ? op(h, e) : -1;
    plat_mutex_unlock(h->run_lock);
    return rc;
}
int modhost_load_now(modhost *h, const char *id) { return locked_op(h, id, mh_load); }
int modhost_unload_now(modhost *h, const char *id) { return locked_op(h, id, unload_op); }
int modhost_reload_now(modhost *h, const char *id) { return locked_op(h, id, mh_reload); }

/* ---- Introspection ------------------------------------------------------ */

size_t modhost_mod_count(const modhost *h) { return h->nmods; }

int modhost_mod_view_at(const modhost *h, size_t i, modhost_mod_view *out) {
    if (i >= h->nmods) return -1;
    const mh_entry *e = &h->mods[i];
    memset(out, 0, sizeof *out);
    out->id = e->m.id;
    out->name = e->m.name;
    out->dir = e->dir;
    out->last_error = e->last_error[0] ? e->last_error : NULL;
    out->version = e->m.version;
    out->state = e->state;
    out->scope = (modhost_scope)e->m.scope;
    out->protocol = e->m.protocol;
    out->has_code = e->has_code;
    out->hot_reload = e->m.hot_reload;
    out->reload_count = e->reload_count;
    out->resources = e->inst ? (uint32_t)e->inst->nres : 0;
    if (e->inst) {
        for (int j = 0; j < 2; ++j) if (h->trigger_owner[j] == e->inst) ++out->resources;
        if (h->led_owner == e->inst) ++out->resources;
    }
    out->frame_ms = e->frame_ms;
    out->net_bytes_out = e->net_out;
    return 0;
}

void modhost_draw_panels(modhost *h) {
    modhost_ui_backend *ui = &h->cfg.ui;
    if (!ui->begin_panel) return;
    if (!plat_mutex_trylock(h->run_lock)) return; /* game thread is reloading: skip a frame */
    for (size_t i = 0; i < h->nmods; i++) {
        struct bbm_mod *m = h->mods[i].inst;
        if (!m || h->mods[i].state != MODHOST_ACTIVE || m->bypassed) continue;
        for (size_t j = 0; j < m->nres && !m->faulted; j++) {
            if (m->res[j].kind != MH_RES_PANEL) continue;
            mh_panel *p = m->res[j].p;
            if (!p->draw) continue;
            if (ui->begin_panel(ui->user, p->title, m->entry->m.id)) {
                h->current_panel_mod = m;
                mh_call c = {.kind = MH_CALL_VOID1, .f = (void (*)(void))p->draw, .a = p->user};
                mh_call_mod(m, &c);
                mh_ui_tables_cleanup(m);
            h->current_panel_mod = NULL;
            }
            ui->end_panel(ui->user);
        }
    }
    mh_ui_slots_draw(h);
    plat_mutex_unlock(h->run_lock);
}

/* ---- Faults ------------------------------------------------------------- */

const char *modhost_fault_owner(const modhost *h, uintptr_t ip) {
    for (size_t i = 0; i < h->nmods; i++) {
        struct bbm_mod *m = h->mods[i].inst;
        if (m && ip >= m->lib_lo && ip < m->lib_hi) return h->mods[i].m.id;
    }
    return NULL;
}

/* From the exception handler, any thread, no locks (the faulting thread may
 * hold them). Marks the mod; the unload happens at the next sync point. */
int modhost_on_fault(modhost *h, uintptr_t ip) {
    for (size_t i = 0; i < h->nmods; i++) {
        struct bbm_mod *m = h->mods[i].inst;
        if (!m || ip < m->lib_lo || ip >= m->lib_hi) continue;
        mh_mod_faulted(m, "game code calling into it (a hook)");
        return 1;
    }
    return 0;
}
