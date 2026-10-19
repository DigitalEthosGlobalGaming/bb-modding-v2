/* SPDX-License-Identifier: GPL-2.0-or-later
 * The bbm_host table a mod sees, and the per-mod resource registry that makes
 * unload total: whatever a mod creates is recorded here and reverted in
 * reverse order when it unloads. */
#include "internal.h"

void mh_group_free(modhost *h, mh_group *g); /* xmlpatch.c */

void mh_log(modhost *h, const char *mod_id, int level, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (h->cfg.log) h->cfg.log(h->cfg.log_user, mod_id, level, buf);
}

void mh_set_error(struct bbm_mod *m, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m->last_error, sizeof m->last_error, fmt, ap);
    va_end(ap);
    mh_strlcpy(m->entry->last_error, m->last_error, sizeof m->entry->last_error);
    mh_log(m->host, m->entry->m.id, BBM_LOG_ERROR, "%s", m->last_error);
}

mh_entry *mh_find(modhost *h, const char *id) {
    for (size_t i = 0; i < h->nmods; i++)
        if (!strcmp(h->mods[i].m.id, id)) return &h->mods[i];
    return NULL;
}

void mh_request(modhost *h, mh_req_kind kind, const char *id) {
    plat_mutex_lock(h->req_lock);
    if (h->nreqs < sizeof h->reqs / sizeof h->reqs[0]) {
        h->reqs[h->nreqs].kind = kind;
        mh_strlcpy(h->reqs[h->nreqs].id, id, sizeof h->reqs[h->nreqs].id);
        h->nreqs++;
    }
    plat_mutex_unlock(h->req_lock);
}

/* ---- Calls into mod code ------------------------------------------------- */

static void call_thunk(void *p) {
    mh_call *c = p;
    switch (c->kind) {
    case MH_CALL_VOID1: ((void (*)(void *))c->f)(c->a); break;
    case MH_CALL_STATUS1: c->rc = ((bbm_status (*)(void *))c->f)(c->a); break;
    case MH_CALL_SAVE: c->rc = ((bbm_status (*)(void *, bbm_blob *))c->f)(c->a, (bbm_blob *)c->b); break;
    case MH_CALL_RESTORE:
        c->rc = ((bbm_status (*)(void *, const bbm_blob *, bbm_version))c->f)(c->a, c->b, c->v);
        break;
    case MH_CALL_EVENT: ((void (*)(void *, const bbm_event *))c->f)(c->a, c->b); break;
    case MH_CALL_NET: ((bbm_net_cb)c->f)(c->a, c->u, c->b, c->n); break;
    case MH_CALL_ENTRY:
        c->rc = ((bbm_entry_fn)c->f)((const bbm_host *)c->a, (bbm_handle)c->b2, (bbm_mod_api *)c->c,
                                     (bbm_ctx **)c->d);
        break;
    }
}

void mh_mod_faulted(struct bbm_mod *m, const char *what) {
    modhost *h = m->host;
    if (m->faulted) return;
    mh_item_actions_cancel(m);
    m->faulted = 1;
    m->bypassed = 1;
    for (size_t j = 0; j < m->nres; j++)
        if (m->res[j].kind == MH_RES_HOOK) mh_hook_bypass(m->res[j].p, 1);
    m->entry->state = MODHOST_FAULTED;
    snprintf(m->entry->last_error, sizeof m->entry->last_error, "crashed in %s (disabled)", what);
    mh_log(h, m->entry->m.id, BBM_LOG_ERROR, "crashed in %s; the mod is disabled, the game keeps running", what);
    if (h->cfg.ui.notify) h->cfg.ui.notify(h->cfg.ui.user, m->entry->m.id, m->entry->last_error);
    mh_request(h, MH_REQ_UNLOAD, m->entry->m.id);
}

int mh_call_mod(struct bbm_mod *m, mh_call *c) {
    modhost *h = m->host;
    if (m->faulted) return -1;
    c->rc = BBM_ERR;
    __atomic_add_fetch(&m->inflight, 1, __ATOMIC_ACQ_REL);
    int bad = 0;
    struct bbm_mod *previous_caller = h->gameplay_caller;
    h->gameplay_caller = m;
    if (h->cfg.platform.call_guarded) bad = h->cfg.platform.call_guarded(h->cfg.platform.user, call_thunk, c);
    else call_thunk(c);
    h->gameplay_caller = previous_caller;
    __atomic_sub_fetch(&m->inflight, 1, __ATOMIC_ACQ_REL);
    if (!bad) return 0;
    static const char *names[] = {"?", "a callback", "a callback", "on_save_state", "on_restore_state",
                                  "on_event", "a network callback", "bbmod_entry"};
    mh_mod_faulted(m, names[c->kind < 8 ? c->kind : 0]);
    return -1;
}

/* ---- Resource registry -------------------------------------------------- */

bbm_id mh_res_add(struct bbm_mod *m, mh_res_kind kind, void *p) {
    if (m->nres == m->capres) {
        size_t cap = m->capres ? m->capres * 2 : 8;
        mh_res *r = realloc(m->res, cap * sizeof *r);
        if (!r) return 0;
        m->res = r;
        m->capres = cap;
    }
    bbm_id id = ++m->host->next_id;
    m->res[m->nres].id = id;
    m->res[m->nres].kind = kind;
    m->res[m->nres].p = p;
    m->nres++;
    return id;
}

static void release_res(struct bbm_mod *m, mh_res *r) {
    modhost *h = m->host;
    switch (r->kind) {
    case MH_RES_PATCH: mh_patch_revert(h, r->p); break;
    case MH_RES_GROUP: mh_group_free(h, r->p); break;
    case MH_RES_HOOK: mh_hook_remove(h, r->p); break;
    case MH_RES_FRAME: free(r->p); break;
    case MH_RES_PLAYER_MODIFIER: free(r->p); break;
    case MH_RES_PANEL: free(r->p); break;
    case MH_RES_UI_CONTRIBUTION: free(r->p); break;
    case MH_RES_NATIVE_MENU: mh_menu_release(m,r->p); break;
    case MH_RES_TEXT: mh_text_release(m,r->p); break;
    case MH_RES_FILE: {
        mh_file *f = r->p;
        mh_vfs_remove_owner(h, m, f->game_path);
        free(f);
        break;
    }
    case MH_RES_CHANNEL: free(r->p); break;
    case MH_RES_THREAD: {
        mh_thread *t = r->p;
        t->stop = 1;
        if (plat_thread_join_timeout(t->handle, 2000) != 0) {
            /* It ignores its stop flag. Unmapping its code would crash it, so the
             * DLL stays loaded (and this record leaks) until the game exits. */
            mh_log(h, m->entry->m.id, BBM_LOG_WARN, "a mod thread did not stop within 2 s; keeping its DLL loaded");
            m->leak_lib = 1;
            break;
        }
        free(t);
        break;
    }
    }
}

void mh_res_release_all(struct bbm_mod *m) {
    mh_pad_release_owner(m);
    /* Reverse order: hooks and patches last so callbacks/threads stop first. */
    for (size_t i = m->nres; i-- > 0;) release_res(m, &m->res[i]);
    free(m->res);
    m->res = NULL;
    m->nres = m->capres = 0;
}

int mh_res_release_one(struct bbm_mod *m, bbm_id id) {
    for (size_t i = 0; i < m->nres; i++) {
        if (m->res[i].id != id) continue;
        release_res(m, &m->res[i]);
        memmove(&m->res[i], &m->res[i + 1], (m->nres - i - 1) * sizeof m->res[0]);
        m->nres--;
        return 0;
    }
    return -1;
}

/* ---- Event dispatch ----------------------------------------------------- */

void mh_dispatch_event(modhost *h, const bbm_event *ev) {
    for (size_t i = 0; i < h->nmods; i++) {
        struct bbm_mod *m = h->mods[i].inst;
        if (!m || h->mods[i].state != MODHOST_ACTIVE) continue;
        if (m->bypassed || !(m->events & ev->type) || !m->api.on_event) continue;
        mh_call c = {.kind = MH_CALL_EVENT, .f = (void (*)(void))m->api.on_event, .a = m->ctx, .b = ev};
        mh_call_mod(m, &c);
    }
}

/* ---- ABI implementation ------------------------------------------------- */

static void api_log(bbm_handle self, int32_t level, const char *msg) {
    mh_log(self->host, self->entry->m.id, level, "%s", msg);
}
static const char *api_last_error(bbm_handle self) { return self->last_error; }
static void *api_alloc(size_t n) { return malloc(n); }
static void api_free(void *p) { free(p); }

static bbm_status api_config_get(bbm_handle self, const char *key, char *out, size_t cap) {
    if (!self->entry->has_manifest) return BBM_ERR_NOTFOUND;
    /* User settings beside bbmods, layered over packaged defaults. */
    char override[MH_PATH];
    int n = snprintf(override, sizeof override, "%s/../config/%s.toml",
                     self->host->mods_dir, self->entry->m.id);
    if (n > 0 && (size_t)n < sizeof override &&
        mh_manifest_get(override, key, out, cap) == 0) return BBM_OK;
    return mh_manifest_get(self->entry->manifest_path, key, out, cap) == 0 ? BBM_OK : BBM_ERR_NOTFOUND;
}

static void api_mod_info(bbm_handle self, const char **id, bbm_version *v) {
    if (id) *id = self->entry->m.id;
    if (v) *v = self->entry->m.version;
}

static volatile int32_t *api_inflight(bbm_handle self) { return &self->inflight; }

static bbm_status api_release(bbm_handle self, bbm_id id) {
    return mh_res_release_one(self, id) == 0 ? BBM_OK : BBM_ERR_NOTFOUND;
}

static uintptr_t api_eboot_base(bbm_handle self) { return self->host->cfg.eboot_base; }

static uintptr_t api_ps4_to_host(bbm_handle self, uint64_t ps4) {
    modhost *h = self->host;
    uint64_t base = h->cfg.ps4_eboot_base ? h->cfg.ps4_eboot_base : 0x400000;
    return h->cfg.eboot_base + (uintptr_t)(ps4 - base);
}

static bbm_status api_read(bbm_handle self, uintptr_t addr, void *out, size_t n) {
    return mh_read_safe(self->host, (void *)addr, out, n) == 0 ? BBM_OK : BBM_ERR;
}

static bbm_status api_write(bbm_handle self, uintptr_t addr, const void *src, size_t n) {
    return mh_write_code(self->host, (void *)addr, src, n) == 0 ? BBM_OK : BBM_ERR;
}

static bbm_status api_scan(bbm_handle self, const char *pat, uintptr_t start, size_t len, uintptr_t *out) {
    return mh_scan(self->host, pat, start, len, out);
}

static bbm_status api_patch_bytes(bbm_handle self, uintptr_t addr, const void *bytes, size_t n, bbm_id *out) {
    mh_patch *p;
    bbm_status rc = mh_patch_apply(self, addr, bytes, n, NULL, &p);
    if (rc != BBM_OK) return rc;
    bbm_id id = mh_res_add(self, MH_RES_PATCH, p);
    if (!id) { mh_patch_revert(self->host, p); return BBM_ERR_NOMEM; }
    if (out) *out = id;
    return BBM_OK;
}

static bbm_status api_patch_xml(bbm_handle self, const char *path, const char *name, bbm_id *out) {
    mh_group *g;
    bbm_status rc = mh_patch_xml(self, path, name, &g);
    if (rc != BBM_OK) return rc;
    bbm_id id = mh_res_add(self, MH_RES_GROUP, g);
    if (!id) { mh_group_free(self->host, g); return BBM_ERR_NOMEM; }
    if (out) *out = id;
    return BBM_OK;
}

static bbm_status api_hook_inline(bbm_handle self, uintptr_t target, void *detour, void **tramp, bbm_id *out) {
    mh_hook *k;
    bbm_status rc = mh_hook_inline(self, target, detour, tramp, &k);
    if (rc != BBM_OK) return rc;
    bbm_id id = mh_res_add(self, MH_RES_HOOK, k);
    if (!id) { mh_hook_remove(self->host, k); return BBM_ERR_NOMEM; }
    if (out) *out = id;
    return BBM_OK;
}

static bbm_status api_hook_slot(bbm_handle self, void **slot, void *detour, void **orig, bbm_id *out) {
    mh_hook *k;
    bbm_status rc = mh_hook_slot(self, slot, detour, orig, &k);
    if (rc != BBM_OK) return rc;
    bbm_id id = mh_res_add(self, MH_RES_HOOK, k);
    if (!id) { mh_hook_remove(self->host, k); return BBM_ERR_NOMEM; }
    if (out) *out = id;
    return BBM_OK;
}

static bbm_status api_on_frame(bbm_handle self, bbm_frame_cb cb, void *user, bbm_id *out) {
    mh_frame_cb *f = calloc(1, sizeof *f);
    if (!f) return BBM_ERR_NOMEM;
    f->cb = cb;
    f->user = user;
    bbm_id id = mh_res_add(self, MH_RES_FRAME, f);
    if (!id) { free(f); return BBM_ERR_NOMEM; }
    if (out) *out = id;
    return BBM_OK;
}

struct thread_args { bbm_thread_fn fn; void *user; const volatile int32_t *stop; };
static void thread_thunk(void *p) {
    struct thread_args *a = p;
    a->fn(a->user, a->stop);
}
static void thread_entry(void *arg) {
    mh_thread *t = arg;
    struct thread_args a = {t->fn, t->user, &t->stop};
    if (t->owner && t->owner->host->cfg.platform.call_guarded) {
        modhost *h = t->owner->host;
        if (h->cfg.platform.call_guarded(h->cfg.platform.user, thread_thunk, &a)) mh_mod_faulted(t->owner, "a mod thread");
    } else {
        thread_thunk(&a);
    }
}

static bbm_status api_thread_spawn(bbm_handle self, bbm_thread_fn fn, void *user, bbm_id *out) {
    mh_thread *t = calloc(1, sizeof *t);
    if (!t) return BBM_ERR_NOMEM;
    t->fn = fn;
    t->user = user;
    t->owner = self;
    t->handle = plat_thread_start(thread_entry, t);
    if (!t->handle) { free(t); return BBM_ERR; }
    bbm_id id = mh_res_add(self, MH_RES_THREAD, t);
    if (!id) { t->stop = 1; plat_thread_join(t->handle); free(t); return BBM_ERR_NOMEM; }
    if (out) *out = id;
    return BBM_OK;
}

static bbm_status api_subscribe(bbm_handle self, uint32_t mask) {
    self->events |= mask;
    return BBM_OK;
}

static bbm_status api_ui_panel(bbm_handle self, const char *title, bbm_panel_cb draw, void *user, bbm_id *out) {
    mh_panel *p = calloc(1, sizeof *p);
    if (!p) return BBM_ERR_NOMEM;
    mh_strlcpy(p->title, title ? title : self->entry->m.name, sizeof p->title);
    p->draw = draw;
    p->user = user;
    bbm_id id = mh_res_add(self, MH_RES_PANEL, p);
    if (!id) { free(p); return BBM_ERR_NOMEM; }
    if (out) *out = id;
    return BBM_OK;
}

static modhost_ui_backend *ui(bbm_handle self) { return &self->host->cfg.ui; }

static void api_ui_notify(bbm_handle self, const char *msg) {
    if (ui(self)->notify) ui(self)->notify(ui(self)->user, self->entry->m.id, msg);
}
static void api_ui_text(bbm_handle self, const char *text) {
    if (ui(self)->text) ui(self)->text(ui(self)->user, text);
}
static int32_t api_ui_button(bbm_handle self, const char *label) {
    return ui(self)->button ? ui(self)->button(ui(self)->user, label) : 0;
}
static int32_t api_ui_checkbox(bbm_handle self, const char *label, int32_t *v) {
    return ui(self)->checkbox ? ui(self)->checkbox(ui(self)->user, label, v) : 0;
}
static int32_t api_ui_slider(bbm_handle self, const char *label, float *v, float lo, float hi) {
    return ui(self)->slider_float ? ui(self)->slider_float(ui(self)->user, label, v, lo, hi) : 0;
}
static void api_ui_separator(bbm_handle self) { if (ui(self)->separator) ui(self)->separator(ui(self)->user); }
static void api_ui_same_line(bbm_handle self) { if (ui(self)->same_line) ui(self)->same_line(ui(self)->user); }
static void *api_ui_imgui(bbm_handle self) {
    return ui(self)->imgui_context ? ui(self)->imgui_context(ui(self)->user) : NULL;
}

static bbm_status api_file_override(bbm_handle self, const char *game_path, const char *source, bbm_id *out) {
    if (!game_path || !source) return BBM_ERR_ARG;
    mh_file *f = calloc(1, sizeof *f);
    if (!f) return BBM_ERR_NOMEM;
    mh_strlcpy(f->game_path, game_path, sizeof f->game_path);
    if (source[0] == '/' || (source[0] && source[1] == ':')) mh_strlcpy(f->source, source, sizeof f->source);
    else mh_path_join(f->source, sizeof f->source, self->entry->dir, source);
    bbm_id id = mh_res_add(self, MH_RES_FILE, f);
    if (!id) { free(f); return BBM_ERR_NOMEM; }
    /* Overrides sit above folder assets; order 1000000 keeps them on top. */
    mh_vfs_add(self->host, f->game_path, f->source, self, 1000000);
    if (out) *out = id;
    return BBM_OK;
}

static bbm_status api_net_channel_open(bbm_handle self, const char *name, uint32_t flags, bbm_id *out) {
    mh_channel *c = calloc(1, sizeof *c);
    if (!c) return BBM_ERR_NOMEM;
    mh_strlcpy(c->name, name, sizeof c->name);
    c->wire_id = mh_hash32(self->entry->m.id, name);
    c->flags = flags;
    bbm_id id = mh_res_add(self, MH_RES_CHANNEL, c);
    if (!id) { free(c); return BBM_ERR_NOMEM; }
    if (out) *out = id;
    return BBM_OK;
}

static mh_channel *channel_of(bbm_handle self, bbm_id id) {
    for (size_t i = 0; i < self->nres; i++)
        if (self->res[i].id == id && self->res[i].kind == MH_RES_CHANNEL) return self->res[i].p;
    return NULL;
}

static bbm_status api_net_on_message(bbm_handle self, bbm_id ch, bbm_net_cb cb, void *user) {
    mh_channel *c = channel_of(self, ch);
    if (!c) return BBM_ERR_NOTFOUND;
    c->cb = cb;
    c->user = user;
    return BBM_OK;
}

static bbm_status api_net_send(bbm_handle self, bbm_id ch, uint64_t peer, const void *data, size_t len) {
    mh_channel *c = channel_of(self, ch);
    if (!c) return BBM_ERR_NOTFOUND;
    return mh_net_send(self, c, peer, data, len);
}

static bbm_status api_net_broadcast(bbm_handle self, bbm_id ch, const void *data, size_t len) {
    mh_channel *c = channel_of(self, ch);
    if (!c) return BBM_ERR_NOTFOUND;
    return mh_net_send(self, c, 0, data, len);
}

static bbm_status api_session_info(bbm_handle self, bbm_session_info *out) {
    *out = self->host->session;
    out->modset_hash = mh_modset_hash(self->host);
    return BBM_OK;
}

/* ---- ABI 1.1 -------------------------------------------------------------- */

static int32_t api_key_down(bbm_handle self, int32_t sc) {
    return sc >= 0 && sc < BBM_KEY_COUNT && self->host->keys_now[sc];
}
static int32_t api_key_pressed(bbm_handle self, int32_t sc) {
    modhost *h = self->host;
    return sc >= 0 && sc < BBM_KEY_COUNT && h->keys_now[sc] && !h->keys_prev[sc];
}
static uint32_t api_pad_buttons(bbm_handle self) { return self->host->pad_now; }
static uint32_t api_pad_pressed(bbm_handle self) { return self->host->pad_now & ~self->host->pad_prev; }

static int32_t api_ui_slider_int(bbm_handle self, const char *label, int32_t *v, int32_t lo, int32_t hi) {
    return ui(self)->slider_int ? ui(self)->slider_int(ui(self)->user, label, v, lo, hi) : 0;
}
static int32_t api_ui_input_text(bbm_handle self, const char *label, char *buf, size_t cap) {
    return ui(self)->input_text ? ui(self)->input_text(ui(self)->user, label, buf, cap) : 0;
}
static int32_t api_ui_collapsing_header(bbm_handle self, const char *label) {
    return ui(self)->collapsing_header ? ui(self)->collapsing_header(ui(self)->user, label) : 1;
}
static int32_t api_ui_combo(bbm_handle self, const char *label, int32_t *cur, const char *items) {
    return ui(self)->combo ? ui(self)->combo(ui(self)->user, label, cur, items) : 0;
}
static void api_ui_text_colored(bbm_handle self, float r, float g, float b, float a, const char *text) {
    if (ui(self)->text_colored) ui(self)->text_colored(ui(self)->user, r, g, b, a, text);
    else if (ui(self)->text) ui(self)->text(ui(self)->user, text);
}

/* ---- ABI 1.2 -------------------------------------------------------------- */

static size_t api_eboot_size(bbm_handle self) { return self->host->cfg.eboot_size; }

static bbm_status api_mem_region(bbm_handle self, uintptr_t addr, bbm_region *out) {
    (void)self;
    if (!out) return BBM_ERR_ARG;
    uintptr_t base;
    size_t size;
    uint32_t flags;
    if (plat_next_region(addr, &base, &size, &flags) != 0) return BBM_ERR_NOTFOUND;
    out->base = base;
    out->size = size;
    out->flags = flags;
    out->reserved = 0;
    return BBM_OK;
}

void mh_fill_api(bbm_host *a) {
    memset(a, 0, sizeof *a);
    a->size = sizeof *a;
    a->abi_major = BBM_ABI_MAJOR;
    a->abi_minor = BBM_ABI_MINOR;
    mh_pad_fill(a);
    mh_gameplay_fill(a);
    mh_equipment_fill(a);
    mh_inventory_fill(a);
    mh_characters_fill(a);
    mh_item_actions_fill(a);
    mh_ui_slots_fill_api(a);
    mh_ui_tables_fill(a);
    mh_menu_fill(a);
    mh_text_fill(a);
    a->log = api_log;
    a->last_error = api_last_error;
    a->alloc = api_alloc;
    a->free = api_free;
    a->config_get = api_config_get;
    a->mod_info = api_mod_info;
    a->inflight = api_inflight;
    a->release = api_release;
    a->eboot_base = api_eboot_base;
    a->ps4_to_host = api_ps4_to_host;
    a->read = api_read;
    a->write = api_write;
    a->scan = api_scan;
    a->patch_bytes = api_patch_bytes;
    a->patch_xml = api_patch_xml;
    a->hook_inline = api_hook_inline;
    a->hook_slot = api_hook_slot;
    a->on_frame = api_on_frame;
    a->thread_spawn = api_thread_spawn;
    a->subscribe = api_subscribe;
    a->ui_panel = api_ui_panel;
    a->ui_notify = api_ui_notify;
    a->ui_text = api_ui_text;
    a->ui_button = api_ui_button;
    a->ui_checkbox = api_ui_checkbox;
    a->ui_slider_float = api_ui_slider;
    a->ui_separator = api_ui_separator;
    a->ui_same_line = api_ui_same_line;
    a->ui_imgui_context = api_ui_imgui;
    a->file_override = api_file_override;
    a->net_channel_open = api_net_channel_open;
    a->net_on_message = api_net_on_message;
    a->net_send = api_net_send;
    a->net_broadcast = api_net_broadcast;
    a->session_info = api_session_info;
    a->key_down = api_key_down;
    a->key_pressed = api_key_pressed;
    a->pad_buttons = api_pad_buttons;
    a->pad_pressed = api_pad_pressed;
    a->ui_slider_int = api_ui_slider_int;
    a->ui_input_text = api_ui_input_text;
    a->ui_collapsing_header = api_ui_collapsing_header;
    a->ui_combo = api_ui_combo;
    a->ui_text_colored = api_ui_text_colored;
    a->eboot_size = api_eboot_size;
    a->mem_region = api_mem_region;
}
