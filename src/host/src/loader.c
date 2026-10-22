/* SPDX-License-Identifier: GPL-2.0-or-later
 * Discovery, load, unload and hot reload. Load/unload always run on the main
 * thread (from modhost_frame or the *_now calls); the watcher only queues
 * requests. */
#include "internal.h"

static const char *GAME_FOLDERS[] = {"action", "chr", "event", "facegen", "font", "map", "menu",
                                     "movie", "msg", "mtd", "obj", "other", "param", "paramdef",
                                     "parts", "remo", "script", "sfx", "shader", "sound", NULL};

/* ---- Discovery ---------------------------------------------------------- */

struct scan_ctx { modhost *h; int new_only; };

static uint64_t newest_mtime(const char *dir);

static void newest_cb(void *user, const char *name, int is_dir) {
    uint64_t *best = user;
    (void)is_dir;
    (void)name;
    (void)best;
}

struct tree_ctx { const char *dir; uint64_t best; };

static void tree_cb(void *user, const char *name, int is_dir) {
    struct tree_ctx *t = user;
    char full[MH_PATH];
    mh_path_join(full, sizeof full, t->dir, name);
    uint64_t mt = 0, sz = 0;
    int d = 0;
    plat_stat(full, &mt, &sz, &d);
    if (mt > t->best) t->best = mt;
    if (is_dir) {
        uint64_t sub = newest_mtime(full);
        if (sub > t->best) t->best = sub;
    }
}

static uint64_t newest_mtime(const char *dir) {
    struct tree_ctx t = {dir, 0};
    plat_list_dir(dir, tree_cb, &t);
    return t.best;
}

static int add_entry(modhost *h, mh_entry **out) {
    if (h->nmods == h->capmods) {
        size_t cap = h->capmods ? h->capmods * 2 : 8;
        mh_entry *n = realloc(h->mods, cap * sizeof *n);
        if (!n) return -1;
        /* Fix up back-pointers from live instances after realloc. */
        for (size_t i = 0; i < h->nmods; i++)
            if (n[i].inst) n[i].inst->entry = &n[i];
        h->mods = n;
        h->capmods = cap;
    }
    mh_entry *e = &h->mods[h->nmods++];
    memset(e, 0, sizeof *e);
    *out = e;
    return 0;
}

static int has_game_folder(const char *dir);

static void gamefolder_cb(void *user, const char *name, int is_dir) {
    int *found = user;
    if (!is_dir) return;
    for (int i = 0; GAME_FOLDERS[i]; i++) {
#ifdef _WIN32
        if (!_stricmp(name, GAME_FOLDERS[i])) { *found = 1; return; }
#else
        if (!strcasecmp(name, GAME_FOLDERS[i])) { *found = 1; return; }
#endif
    }
}

static int has_game_folder(const char *dir) {
    int found = 0;
    plat_list_dir(dir, gamefolder_cb, &found);
    return found;
}

static int has_assets(const char *dir) {
    char dv[MH_PATH];
    mh_path_join(dv, sizeof dv, dir, "dvdroot_ps4");
    int d = 0;
    if (plat_stat(dv, NULL, NULL, &d) == 0 && d) return 1;
    return has_game_folder(dir);
}

static void discover_cb(void *user, const char *name, int is_dir) {
    struct scan_ctx *s = user;
    modhost *h = s->h;
    if (!is_dir) return;

    char dir[MH_PATH];
    mh_path_join(dir, sizeof dir, h->mods_dir, name);

    /* Periodic scans must not refresh live manifests, resurrect manually
     * unloaded mods, or rewalk asset trees belonging to known folders. */
    if (s->new_only) for (size_t i = 0; i < h->nmods; ++i)
        if (!strcmp(h->mods[i].dir, dir)) return;

    mh_entry tmp;
    memset(&tmp, 0, sizeof tmp);
    mh_strlcpy(tmp.dir, dir, sizeof tmp.dir);

    char manifest[MH_PATH];
    mh_path_join(manifest, sizeof manifest, dir, "mod.toml");
    int has_manifest = plat_stat(manifest, NULL, NULL, &(int){0}) == 0;

    char err[MH_ERR] = "";
    if (has_manifest) {
        if (mh_manifest_load(manifest, &tmp.m, err, sizeof err) != 0) {
            mh_log(h, name, BBM_LOG_WARN, "ignored: %s", err);
            return;
        }
        tmp.has_manifest = 1;
        mh_strlcpy(tmp.manifest_path, manifest, sizeof tmp.manifest_path);
        if (tmp.m.dll[0]) {
            mh_path_join(tmp.dll_path, sizeof tmp.dll_path, dir, tmp.m.dll);
            int dll_is_dir = 0;
            uint64_t dll_size = 0;
            tmp.has_code = plat_stat(tmp.dll_path, NULL, &dll_size, &dll_is_dir) == 0 &&
                           !dll_is_dir && dll_size > 0;
            if (!tmp.has_code) return; /* manifest copied before DLL: retry next scan */
        }
    } else {
        if (!has_assets(dir)) return; /* not a mod */
        mh_strlcpy(tmp.m.id, name, sizeof tmp.m.id);
        mh_strlcpy(tmp.m.name, name, sizeof tmp.m.name);
        tmp.m.hot_reload = 1;
        tmp.m.enabled = 1;
        tmp.m.protocol = 1;
    }

    /* Merge with an existing entry (rescan) or append. */
    mh_entry *e = mh_find(h, tmp.m.id);
    if (e && s->new_only) return; /* duplicate ID cannot replace a running mod */
    if (!e) {
        if (add_entry(h, &e)) return;
        tmp.state = MODHOST_DISCOVERED;
        *e = tmp;
    } else {
        /* Keep runtime state, refresh manifest + paths. */
        e->m = tmp.m;
        e->has_manifest = tmp.has_manifest;
        e->has_code = tmp.has_code;
        mh_strlcpy(e->dir, tmp.dir, sizeof e->dir);
        mh_strlcpy(e->dll_path, tmp.dll_path, sizeof e->dll_path);
        mh_strlcpy(e->manifest_path, tmp.manifest_path, sizeof e->manifest_path);
    }
    if (e->has_code) plat_stat(e->dll_path, &e->pend_mtime, &e->pend_size, &(int){0});
    char dv[MH_PATH];
    mh_path_join(dv, sizeof dv, dir, "dvdroot_ps4");
    e->asset_stamp = newest_mtime(plat_stat(dv, NULL, NULL, &(int){0}) == 0 ? dv : dir);
    (void)newest_cb;
}

int mh_discover(modhost *h) {
    struct scan_ctx s = {h, 0};
    plat_list_dir(h->mods_dir, discover_cb, &s);
    return (int)h->nmods;
}

int mh_discover_new(modhost *h) {
    struct scan_ctx s = {h, 1};
    plat_list_dir(h->mods_dir, discover_cb, &s);
    return (int)h->nmods;
}

/* ---- Dependency order (stable topological by `after`) ------------------- */

static int depends_on(mh_entry *a, mh_entry *b) {
    for (int i = 0; i < a->m.n_after; i++)
        if (!strcmp(a->m.after[i], b->m.id)) return 1;
    return 0;
}

void order_enabled(modhost *h, mh_entry **list, size_t *n) {
    size_t cnt = 0;
    for (size_t i = 0; i < h->nmods; i++)
        if (h->mods[i].m.enabled && !h->cfg.safe_mode) list[cnt++] = &h->mods[i];
    /* Simple stable insertion topo-sort; cycles fall back to discovery order. */
    for (size_t i = 1; i < cnt; i++) {
        for (size_t j = i; j > 0 && depends_on(list[j - 1], list[j]); j--) {
            mh_entry *t = list[j]; list[j] = list[j - 1]; list[j - 1] = t;
        }
    }
    *n = cnt;
}

/* ---- Instance load / unload --------------------------------------------- */

static int conflict_active(modhost *h, mh_entry *e) {
    for (int i = 0; i < e->m.n_conflicts; i++) {
        mh_entry *c = mh_find(h, e->m.conflicts[i]);
        if (c && c->state == MODHOST_ACTIVE) {
            snprintf(e->last_error, sizeof e->last_error, "conflicts with active mod %s", c->m.id);
            return 1;
        }
    }
    return 0;
}

static int abi_ok(mh_entry *e) {
    bbm_version v = {0};
    mh_parse_version(e->m.abi, &v);
    return v.major == BBM_ABI_MAJOR; /* minor/patch forward-compatible */
}

static int load_instance(modhost *h, mh_entry *e, const bbm_blob *state, bbm_version from) {
    if (!e->has_code) { e->state = MODHOST_ACTIVE; return 0; } /* assets-only */
    if (!strcmp(e->m.runtime, "dotnet")) {
        /* The .NET bridge is itself a native mod that loads managed mods; a
         * plain dotnet entry needs that bridge present. */
        if (!mh_find(h, "bbmod.dotnet")) {
            snprintf(e->last_error, sizeof e->last_error, "needs the bbmod.dotnet bridge mod");
            e->state = MODHOST_FAULTED;
            return -1;
        }
    }
    if (!abi_ok(e)) {
        snprintf(e->last_error, sizeof e->last_error, "built for ABI %s, host is %d.%d", e->m.abi,
                 BBM_ABI_MAJOR, BBM_ABI_MINOR);
        e->state = MODHOST_FAULTED;
        return -1;
    }

    struct bbm_mod *m = calloc(1, sizeof *m);
    if (!m) return -1;
    m->host = h;
    m->entry = e;

    /* Shadow copy so the original DLL stays writable for the compiler. */
    uint64_t mt = 0, sz = 0;
    plat_stat(e->dll_path, &mt, &sz, &(int){0});
    /* Unique per load: a DLL left mapped (see leak_lib) must never be reused. */
    snprintf(m->shadow, sizeof m->shadow, "%s/%s-%llx-%u.dll", h->cache_dir, e->m.id,
             (unsigned long long)mt, ++h->shadow_seq);
    plat_mkdirs(h->cache_dir);
    if (plat_copy_file(e->dll_path, m->shadow) != 0) mh_strlcpy(m->shadow, e->dll_path, sizeof m->shadow);

    char err[MH_ERR] = "";
    m->lib = plat_lib_open(m->shadow, err, sizeof err);
    if (!m->lib) {
        snprintf(e->last_error, sizeof e->last_error, "load failed: %s", err);
        free(m);
        e->state = MODHOST_FAULTED;
        return -1;
    }
    bbm_entry_fn entry = (bbm_entry_fn)plat_lib_sym(m->lib, BBM_ENTRY_SYMBOL);
    if (!entry) {
        snprintf(e->last_error, sizeof e->last_error, "no %s symbol", BBM_ENTRY_SYMBOL);
        plat_lib_close(m->lib);
        free(m);
        e->state = MODHOST_FAULTED;
        return -1;
    }
    plat_lib_range(m->lib, (void *)entry, &m->lib_lo, &m->lib_hi);

    mh_call c = {.kind = MH_CALL_ENTRY, .f = (void (*)(void))entry, .a = &h->api, .b2 = m, .c = &m->api,
                 .d = &m->ctx};
    int crashed = mh_call_mod(m, &c) != 0;
    bbm_status rc = c.rc;
    if (crashed || rc != BBM_OK || m->api.abi_major != BBM_ABI_MAJOR || !m->api.on_load) {
        if (!crashed)
            snprintf(e->last_error, sizeof e->last_error, "entry rejected (rc=%d, abi %d.%d)", rc,
                     m->api.abi_major, m->api.abi_minor);
        plat_lib_close(m->lib);
        plat_remove(m->shadow);
        free(m);
        e->state = MODHOST_FAULTED;
        return -1;
    }

    e->inst = m;
    c = (mh_call){.kind = MH_CALL_STATUS1, .f = (void (*)(void))m->api.on_load, .a = m->ctx};
    crashed = mh_call_mod(m, &c) != 0;
    rc = c.rc;
    if (crashed || rc != BBM_OK) {
        if (!crashed) mh_log(h, e->m.id, BBM_LOG_ERROR, "on_load failed (rc=%d): %s", rc, m->last_error);
        mh_res_release_all(m);
    mh_settings_undeclare(m->entry);
        if (!m->faulted && m->api.destroy) {
            mh_call d = {.kind = MH_CALL_VOID1, .f = (void (*)(void))m->api.destroy, .a = m->ctx};
            mh_call_mod(m, &d);
        }
        if (!m->leak_lib) { plat_lib_close(m->lib); plat_remove(m->shadow); }
        e->inst = NULL;
        if (!crashed && !e->last_error[0]) snprintf(e->last_error, sizeof e->last_error, "on_load failed (rc=%d)", rc);
        free(m);
        e->state = MODHOST_FAULTED;
        return -1;
    }
    if (state && state->len && m->api.on_restore_state) {
        c = (mh_call){.kind = MH_CALL_RESTORE, .f = (void (*)(void))m->api.on_restore_state, .a = m->ctx,
                      .b = state, .v = from};
        mh_call_mod(m, &c);
        if (m->faulted) return -1; /* unload already queued */
    }
    e->state = MODHOST_ACTIVE;
    e->last_error[0] = 0;
    plat_stat(e->dll_path, &e->seen_mtime, &e->seen_size, &(int){0});
    mh_log(h, e->m.id, BBM_LOG_INFO, "loaded %s v%u.%u.%u", e->m.name, e->m.version.major,
           e->m.version.minor, e->m.version.patch);
    return 0;
}

int mh_load(modhost *h, mh_entry *e) {
    if (e->state == MODHOST_ACTIVE) return 0;
    if (conflict_active(h, e)) { e->state = MODHOST_DISABLED; return -1; }
    mh_vfs_rebuild_assets(h);
    int rc = load_instance(h, e, NULL, (bbm_version){0});
    mh_vfs_rebuild_assets(h);
    return rc;
}

/* Drain in-flight calls, then run on_unload and revert everything. */
int mh_unload(modhost *h, mh_entry *e, int keep_state, bbm_blob *state_out) {
    struct bbm_mod *m = e->inst;
    if (!m) { if (e->state != MODHOST_FAULTED) e->state = MODHOST_UNLOADED; mh_vfs_rebuild_assets(h); return 0; }

    mh_item_actions_cancel(m);
    /* 1. Route hooks past the mod so new calls skip it. */
    m->bypassed = 1;
    for (size_t i = 0; i < m->nres; i++)
        if (m->res[i].kind == MH_RES_HOOK) mh_hook_bypass(m->res[i].p, 1);

    /* 2. Wait for in-flight calls to drain. */
    uint64_t deadline = plat_now_ms() + (h->cfg.drain_timeout_ms ? h->cfg.drain_timeout_ms : 500);
    while (__atomic_load_n(&m->inflight, __ATOMIC_ACQUIRE) > 0 && plat_now_ms() < deadline)
        plat_sleep_ms(1);
    if (__atomic_load_n(&m->inflight, __ATOMIC_ACQUIRE) > 0) {
        mh_log(h, e->m.id, BBM_LOG_WARN, "unload: %d call(s) still in flight; keeping loaded",
               m->inflight);
        m->bypassed = 0;
        for (size_t i = 0; i < m->nres; i++)
            if (m->res[i].kind == MH_RES_HOOK) mh_hook_bypass(m->res[i].p, 0);
        return -1;
    }

    /* 3. Save state for a reload (never from a mod that crashed). */
    if (keep_state && state_out && m->api.on_save_state && !m->faulted) {
        mh_call c = {.kind = MH_CALL_SAVE, .f = (void (*)(void))m->api.on_save_state, .a = m->ctx, .b = state_out};
        if (mh_call_mod(m, &c) != 0 || c.rc != BBM_OK) { state_out->data = NULL; state_out->len = 0; }
    }

    /* 4. on_unload, then revert all resources, then destroy. A crashed mod is
     * not called again: its resources are host-owned, so reverting needs nothing
     * from it, and its ctx is leaked rather than trusted. */
    if (m->api.on_unload && !m->faulted) {
        mh_call c = {.kind = MH_CALL_VOID1, .f = (void (*)(void))m->api.on_unload, .a = m->ctx};
        mh_call_mod(m, &c);
    }
    mh_res_release_all(m);
    mh_settings_undeclare(m->entry);
    if (m->api.destroy && !m->faulted) {
        mh_call c = {.kind = MH_CALL_VOID1, .f = (void (*)(void))m->api.destroy, .a = m->ctx};
        mh_call_mod(m, &c);
    }

    void *lib = m->lib;
    char shadow[MH_PATH];
    mh_strlcpy(shadow, m->shadow, sizeof shadow);
    /* 5. Hooks are reverted, so nothing new can enter the DLL. Make sure no
     * thread is still finishing a detour inside it before it is unmapped. If
     * one will not leave, leak the library instead of crashing the game. */
    int unmap_ok = !m->leak_lib;
    if (lib && unmap_ok && mh_quiesce_range(h, m->lib_lo, m->lib_hi) != 0) {
        mh_log(h, e->m.id, BBM_LOG_WARN, "a thread is still inside the mod's code; leaving it mapped");
        unmap_ok = 0;
    }
    e->inst = NULL;
    free(m);
    if (lib && unmap_ok) plat_lib_close(lib);
    if (shadow[0] && unmap_ok && strcmp(shadow, e->dll_path)) plat_remove(shadow);

    /* A mod that faulted stays flagged until the user reloads it. */
    if (e->state != MODHOST_FAULTED) e->state = MODHOST_UNLOADED;
    mh_vfs_rebuild_assets(h);
    return 0;
}

int mh_reload(modhost *h, mh_entry *e) {
    if (!e->has_code) { /* assets only: just rebuild the vfs */
        mh_vfs_rebuild_assets(h);
        e->reload_count++;
        mh_log(h, e->m.id, BBM_LOG_INFO, "assets reloaded");
        return 0;
    }
    if (!e->m.hot_reload) {
        mh_log(h, e->m.id, BBM_LOG_WARN, "mod is marked hot_reload = false; restart to apply");
        return -1;
    }
    if (h->session.in_session && (e->m.scope == MODHOST_SCOPE_SESSION ||
                                  e->m.scope == MODHOST_SCOPE_HOST_AUTHORITATIVE) &&
        !h->session.dev_session && h->cfg.in_session_reload_policy == 0) {
        mh_log(h, e->m.id, BBM_LOG_WARN, "session mod: reload blocked while in a session");
        return -1;
    }

    bbm_version from = e->inst ? e->m.version : (bbm_version){0};
    bbm_blob state = {0};
    uint32_t keep = e->reload_count;
    if (mh_unload(h, e, 1, &state) != 0) return -1;

    /* Re-read manifest/paths in case they changed with the DLL. */
    char manifest[MH_PATH];
    mh_path_join(manifest, sizeof manifest, e->dir, "mod.toml");
    if (plat_stat(manifest, NULL, NULL, &(int){0}) == 0) {
        char err[MH_ERR];
        mh_manifest_load(manifest, &e->m, err, sizeof err);
    }
    mh_vfs_rebuild_assets(h);
    int rc = load_instance(h, e, &state, from);
    if (state.data) h->api.free(state.data);
    e->reload_count = keep + 1;
    if (rc == 0) mh_log(h, e->m.id, BBM_LOG_INFO, "reloaded (#%u)", e->reload_count);
    return rc;
}

/* ---- Watcher ------------------------------------------------------------ */

void mh_watch(modhost *h) {
    uint64_t now = plat_now_ms();
    uint32_t interval = h->cfg.watch_interval_ms ? h->cfg.watch_interval_ms : 250;
    if (now - h->last_watch_ms < interval) return;
    h->last_watch_ms = now;

    for (size_t i = 0; i < h->nmods; i++) {
        mh_entry *e = &h->mods[i];
        int changed = 0;
        if (e->has_code) {
            uint64_t mt = 0, sz = 0;
            if (plat_stat(e->dll_path, &mt, &sz, &(int){0}) == 0) {
                if (mt != e->seen_mtime || sz != e->seen_size) {
                    if (mt != e->pend_mtime || sz != e->pend_size) {
                        e->pend_mtime = mt;
                        e->pend_size = sz;
                        e->pend_since = now;
                    } else if (now - e->pend_since >= interval) {
                        changed = 1;
                    }
                }
            }
        }
        /* Asset changes reload the vfs for any mod (cheap). */
        char dv[MH_PATH];
        mh_path_join(dv, sizeof dv, e->dir, "dvdroot_ps4");
        uint64_t stamp = newest_mtime(plat_stat(dv, NULL, NULL, &(int){0}) == 0 ? dv : e->dir);
        int assets_changed = stamp != e->asset_stamp && e->asset_stamp != 0;
        e->asset_stamp = stamp;

        if (changed) {
            mh_req r = {MH_REQ_RELOAD, ""};
            mh_strlcpy(r.id, e->m.id, sizeof r.id);
            plat_mutex_lock(h->req_lock);
            if (h->nreqs < sizeof h->reqs / sizeof h->reqs[0]) h->reqs[h->nreqs++] = r;
            plat_mutex_unlock(h->req_lock);
        } else if (assets_changed && e->state == MODHOST_ACTIVE) {
            mh_vfs_rebuild_assets(h);
        }
    }
}
