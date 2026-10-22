/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MODHOST_INTERNAL_H
#define MODHOST_INTERNAL_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bbmod.h"
#include "modhost.h"

#define MH_PATH 1024
#define MH_NAME 128
#define MH_MAX_DEPS 16
#define MH_ERR 512

/* ---- Platform layer (platform_posix.c / platform_win.c) ---------------- */

uint64_t plat_now_ms(void);
double plat_now_s(void);
void plat_sleep_ms(uint32_t ms);
int plat_stat(const char *path, uint64_t *mtime, uint64_t *size, int *is_dir);
int plat_copy_file(const char *src, const char *dst);
int plat_mkdirs(const char *path);
int plat_remove(const char *path);
typedef void (*plat_dir_cb)(void *user, const char *name, int is_dir);
int plat_list_dir(const char *path, plat_dir_cb cb, void *user);

void *plat_lib_open(const char *path, char *err, size_t cap);
void *plat_lib_sym(void *lib, const char *name);
void plat_lib_close(void *lib);
int plat_lib_range(void *lib, void *sym, uintptr_t *lo, uintptr_t *hi);

void *plat_alloc_exec_near(uintptr_t near_addr, size_t size);
void plat_free_exec(void *p, size_t size);
int plat_write_code(void *addr, const void *src, size_t n);
int plat_read_safe(const void *addr, void *out, size_t n);
int plat_suspend_world(void);
void plat_resume_world(void);
int plat_any_thread_in(uintptr_t lo, uintptr_t hi);
/* First committed readable region ending above addr: 0, or -1 when none. */
int plat_next_region(uintptr_t addr, uintptr_t *base, size_t *size, uint32_t *flags);

typedef struct plat_mutex plat_mutex;
plat_mutex *plat_mutex_new(void);
plat_mutex *plat_mutex_new_recursive(void);
void plat_mutex_free(plat_mutex *m);
int plat_mutex_trylock(plat_mutex *m); /* 1 = acquired */
void plat_mutex_lock(plat_mutex *m);
void plat_mutex_unlock(plat_mutex *m);

void *plat_thread_start(void (*fn)(void *), void *arg);
void plat_thread_join(void *t);
int plat_thread_join_timeout(void *t, uint32_t ms); /* 0 = joined, -1 = still running */

/* ---- Manifest ----------------------------------------------------------- */

typedef struct mh_manifest {
    char id[MH_NAME], name[MH_NAME], dll[MH_PATH], abi[16];
    bbm_version version;
    char after[MH_MAX_DEPS][MH_NAME];
    int n_after;
    char conflicts[MH_MAX_DEPS][MH_NAME];
    int n_conflicts;
    int hot_reload;    /* default 1 */
    int enabled;       /* default 1 */
    int scope;         /* modhost_scope */
    uint32_t protocol; /* default 1 */
    char runtime[32];  /* "native" (default) or "dotnet" */
    int manage;        /* [mod] manage = true: may list/control mods and settings (ABI 1.11) */
} mh_manifest;

int mh_manifest_parse(const char *text, mh_manifest *out, char *err, size_t cap);
int mh_manifest_load(const char *path, mh_manifest *out, char *err, size_t cap);
/* Raw lookup "section.key" in a manifest file, for config_get. */
int mh_manifest_get(const char *path, const char *key, char *out, size_t cap);
int mh_parse_version(const char *s, bbm_version *v);

/* ---- Resources owned by a mod instance --------------------------------- */

typedef enum mh_res_kind {
    MH_RES_PATCH = 1,
    MH_RES_HOOK,
    MH_RES_FRAME,
    MH_RES_PANEL,
    MH_RES_UI_CONTRIBUTION,
    MH_RES_NATIVE_MENU,
    MH_RES_THREAD,
    MH_RES_FILE,
    MH_RES_CHANNEL,
    MH_RES_PLAYER_MODIFIER,
    MH_RES_GROUP, /* several patches applied as one (XML) */
    MH_RES_TEXT,
    MH_RES_UI_LAYER
} mh_res_kind;

typedef struct mh_res {
    bbm_id id;
    mh_res_kind kind;
    void *p; /* kind-specific record, freed by mh_res_release */
} mh_res;

typedef struct mh_patch {
    uintptr_t addr;
    size_t n;
    uint8_t *orig, *bytes;
    struct mh_patch *next_global; /* host-wide list for conflict checks */
    struct bbm_mod *owner;
    const void *group; /* XML group it belongs to, or NULL */
} mh_patch;

typedef struct mh_group {
    mh_patch **patches;
    size_t n;
} mh_group;

typedef struct mh_hook {
    int is_slot;
    uintptr_t target; /* inline: function address; slot: slot address */
    size_t stolen;    /* inline: bytes overwritten at target */
    uint8_t orig[32];
    uint8_t *block;   /* inline: exec block holding gate + trampoline */
    size_t block_size;
    volatile uintptr_t *gate_slot; /* inline: where the gate jumps */
    uintptr_t trampoline;
    void *detour;
    uintptr_t slot_original;
    struct mh_hook *next_global;
    struct bbm_mod *owner;
} mh_hook;

typedef struct mh_frame_cb {
    bbm_frame_cb cb;
    void *user;
} mh_frame_cb;

typedef struct mh_panel {
    char title[MH_NAME];
    bbm_panel_cb draw;
    void *user;
} mh_panel;

void mh_ui_slots_fill_api(bbm_host *api);
void mh_ui_slots_draw(modhost *h); /* caller holds run_lock */

typedef struct mh_thread {
    struct bbm_mod *owner;
    void *handle;
    volatile int32_t stop;
    bbm_thread_fn fn;
    void *user;
} mh_thread;

typedef struct mh_file {
    char game_path[MH_PATH], source[MH_PATH];
} mh_file;

typedef struct mh_channel {
    char name[MH_NAME];
    uint32_t wire_id; /* hash of mod id + name */
    uint32_t flags;
    bbm_net_cb cb;
    void *user;
    uint32_t send_seq;
} mh_channel;

/* ---- Mod entry (discovered) and instance (loaded) ---------------------- */

typedef struct mh_entry mh_entry;

struct bbm_mod {
    modhost *host;
    mh_entry *entry;
    void *lib;
    char shadow[MH_PATH];
    uintptr_t lib_lo, lib_hi;
    bbm_mod_api api;
    bbm_ctx *ctx;
    mh_res *res;
    size_t nres, capres;
    volatile int32_t inflight;
    int bypassed; /* hooks routed to trampolines (draining or faulted) */
    int faulted;  /* crashed: never call into it again, only revert and unmap */
    int leak_lib; /* a mod thread would not stop: keep the DLL mapped */
    uint32_t events;
    int ui_table_open;
    char last_error[MH_ERR];
};

struct mh_entry {
    mh_manifest m;
    char dir[MH_PATH], dll_path[MH_PATH], manifest_path[MH_PATH];
    int has_manifest, has_code;
    modhost_state state;
    struct bbm_mod *inst;
    char last_error[MH_ERR];
    uint64_t seen_mtime, seen_size;       /* DLL as last loaded */
    uint64_t pend_mtime, pend_size, pend_since; /* debounce */
    uint32_t reload_count;
    double frame_ms;
    uint64_t net_out;
    uint64_t asset_stamp; /* newest mtime under dvdroot_ps4 */
    struct mh_settings *settings; /* declarations and values (settings.c); survives reload */
};

typedef enum { MH_REQ_LOAD = 1, MH_REQ_UNLOAD, MH_REQ_RELOAD } mh_req_kind;
typedef struct mh_req {
    mh_req_kind kind;
    char id[MH_NAME];
} mh_req;

typedef struct mh_vfs_entry {
    char key[MH_PATH]; /* lower-case game path */
    char source[MH_PATH];
    struct bbm_mod *owner; /* NULL = asset from a mod folder */
    int order;
} mh_vfs_entry;

typedef struct mh_peer {
    uint64_t id;
    double tokens;
    double last_refill;
    int modset_ok; /* -1 unknown, 0 mismatch, 1 ok */
    uint32_t recv_seq[64];
    uint32_t recv_chan[64];
} mh_peer;

/* Events queued from other threads, delivered at the sync point. */
typedef enum {
    MH_IN_NET = 1, MH_IN_SESSION_BEGIN, MH_IN_SESSION_END, MH_IN_PEER_JOIN, MH_IN_PEER_LEAVE,
    MH_IN_EVENT, MH_IN_UI_ACTION, MH_IN_SETTING
} mh_in_kind;
typedef struct mh_in {
    mh_in_kind kind;
    uint64_t peer, a, b;
    int flag;
    uint8_t *data;
    size_t len;
} mh_in;

struct modhost {
    struct mh_menu_service *menu;
    struct mh_layers *layers;
    volatile int32_t ui_capture; /* a visible interactive layer owns input (lock-free read) */
    volatile int32_t ui_count;   /* published layers exist (lock-free read) */
    modhost_config cfg;
    plat_mutex *run_lock; /* recursive: frame, panels, load/unload */
    plat_mutex *vfs_lock;
    plat_mutex *in_lock;
    mh_in inq[256];
    size_t nin;
    uint8_t keys_now[BBM_KEY_COUNT], keys_prev[BBM_KEY_COUNT];
    uint32_t pad_now, pad_prev;
    struct bbm_mod *trigger_owner[2], *led_owner, *haptics_owner;
    uint8_t trigger_desired[2][11], trigger_sent[2][11], led[3], led_sent[3];
    int pad_valid, led_valid, pad_failed, pad_urgent, pad_capture;
    uint64_t pad_device, pad_next_ms;

    char mods_dir[MH_PATH], cache_dir[MH_PATH];
    bbm_host api;
    mh_entry *mods;
    size_t nmods, capmods;
    uint64_t next_id;

    plat_mutex *req_lock;
    mh_req reqs[64];
    size_t nreqs;

    mh_patch *patches;
    mh_hook *hooks;

    /* Trampoline blocks freed a couple of frames after unhooking. */
    struct { void *p; size_t n; uint64_t free_at_frame; } deferred[256];
    size_t ndeferred;

    mh_vfs_entry *vfs;
    size_t nvfs, capvfs;
    int vfs_any;

    struct mh_inventory_state *inventory;
    struct mh_characters_state *characters;
    bbm_player_snapshot player_last;
    bbm_equipment_snapshot equipment_last;
    int equipment_profile_state;
    uint64_t player_generation;
    uintptr_t player_actor, player_component;
    int player_available;
    int gameplay_in_frame;
    struct bbm_mod *gameplay_caller;
    uint64_t frame_no;
    uint32_t shadow_seq;
    uint64_t last_watch_ms;
    uint64_t last_discovery_ms;
    struct bbm_mod *current_panel_mod;

    /* Session */
    bbm_session_info session;
    mh_peer peers[16];
    size_t npeers;
};

/* host.c: every call into mod code goes through mh_call, which counts it as
 * in-flight and runs it under the platform's fault guard. */
typedef enum {
    MH_CALL_VOID1 = 1, /* f(a) */
    MH_CALL_STATUS1,   /* rc = f(a) */
    MH_CALL_SAVE,      /* rc = f(a, (bbm_blob *)b) */
    MH_CALL_RESTORE,   /* rc = f(a, (const bbm_blob *)b, v) */
    MH_CALL_EVENT,     /* f(a, (const bbm_event *)b) */
    MH_CALL_NET,       /* f(a, u, b, n) */
    MH_CALL_ENTRY      /* rc = f((const bbm_host *)a, (bbm_handle)b2, (bbm_mod_api *)c, (bbm_ctx **)d) */
} mh_call_kind;
typedef struct mh_call {
    mh_call_kind kind;
    void (*f)(void);
    void *a, *b2, *c, *d;
    const void *b;
    uint64_t u;
    size_t n;
    bbm_version v;
    int32_t rc;
} mh_call;
int mh_call_mod(struct bbm_mod *m, mh_call *c); /* 0 ok, -1 the mod faulted (now disabled) */
void mh_mod_faulted(struct bbm_mod *m, const char *what);
void mh_request(modhost *h, mh_req_kind kind, const char *id);

void mh_log(modhost *h, const char *mod_id, int level, const char *fmt, ...);
void mh_set_error(struct bbm_mod *m, const char *fmt, ...);
bbm_id mh_res_add(struct bbm_mod *m, mh_res_kind kind, void *p);
void mh_res_release_all(struct bbm_mod *m);
int mh_res_release_one(struct bbm_mod *m, bbm_id id);
void mh_fill_api(bbm_host *api);
mh_entry *mh_find(modhost *h, const char *id);
void mh_dispatch_event(modhost *h, const bbm_event *ev);
void mh_dispatch_event_to(struct bbm_mod *m, const bbm_event *ev);
void mh_enqueue(modhost *h, mh_in_kind kind, uint64_t peer, uint64_t a, uint64_t b, int flag, const void *data, size_t len);

/* ui_layers.c, settings.c */
void mh_ui_layers_fill(bbm_host *api);
void mh_ui_layer_release(struct bbm_mod *m, void *p);
void mh_ui_layers_free(modhost *h);
void mh_ui_layers_tick(modhost *h);
void mh_ui_action_deliver(modhost *h, uint32_t action);
void mh_settings_fill(bbm_host *api);
void mh_settings_undeclare(mh_entry *e);
void mh_settings_free(mh_entry *e);

/* code.c (patches and hooks) */
int mh_write_code(modhost *h, void *addr, const void *src, size_t n);
int mh_read_safe(modhost *h, const void *addr, void *out, size_t n);
bbm_status mh_patch_apply(struct bbm_mod *m, uintptr_t addr, const void *bytes, size_t n,
                          const void *group, mh_patch **out);
void mh_patch_revert(modhost *h, mh_patch *p);
bbm_status mh_hook_inline(struct bbm_mod *m, uintptr_t target, void *detour, void **tramp,
                          mh_hook **out);
bbm_status mh_hook_slot(struct bbm_mod *m, void **slot, void *detour, void **orig, mh_hook **out);
void mh_hook_bypass(mh_hook *k, int bypass);
void mh_hook_remove(modhost *h, mh_hook *k);
int mh_quiesce_range(modhost *h, uintptr_t lo, uintptr_t hi);
void mh_deferred_tick(modhost *h);
void mh_deferred_flush(modhost *h);
bbm_status mh_scan(modhost *h, const char *pattern, uintptr_t start, size_t len, uintptr_t *out);

/* xmlpatch.c */
bbm_status mh_patch_xml(struct bbm_mod *m, const char *path, const char *name, mh_group **out);

/* loader.c */
int mh_discover(modhost *h);
int mh_discover_new(modhost *h);
int mh_load(modhost *h, mh_entry *e);
int mh_unload(modhost *h, mh_entry *e, int keep_state, bbm_blob *state_out);
int mh_reload(modhost *h, mh_entry *e);
void mh_watch(modhost *h);

/* net.c */
uint32_t mh_hash32(const char *a, const char *b);
uint64_t mh_modset_hash(modhost *h);
bbm_status mh_net_send(struct bbm_mod *m, mh_channel *c, uint64_t peer, const void *data,
                       size_t len);
void mh_net_receive(modhost *h, uint64_t peer, const uint8_t *frame, size_t len);
void mh_net_send_handshake(modhost *h, uint64_t peer);
mh_peer *mh_peer_get(modhost *h, uint64_t id, int create);

/* vfs.c */
void mh_vfs_rebuild_assets(modhost *h);
int mh_vfs_add(modhost *h, const char *game_path, const char *source, struct bbm_mod *owner,
               int order);
void mh_vfs_remove_owner(modhost *h, struct bbm_mod *owner, const char *game_path);

void mh_pad_fill(bbm_host *api);
void mh_pad_release_owner(struct bbm_mod *m);
void mh_pad_tick(modhost *h, uint64_t now);

void mh_inventory_fill(bbm_host *api);
void mh_inventory_tick(modhost *h);
void mh_inventory_stop(modhost *h);
void mh_gameplay_fill(bbm_host *api);
void mh_equipment_fill(bbm_host *api);
void mh_equipment_tick(modhost *h);
void mh_item_actions_start(modhost*h);
void mh_item_actions_tick(modhost*h);
void mh_item_actions_stop(modhost*h);
void mh_item_actions_cancel(struct bbm_mod*m);
void mh_item_actions_fill(bbm_host*a);
void mh_native_observation_start(modhost *h);
void mh_menu_fill(bbm_host *api);
void mh_text_fill(bbm_host *api);
void mh_text_release(struct bbm_mod*,void*);
void mh_menu_start(modhost *h);
void mh_menu_tick(modhost *h);
void mh_menu_stop(modhost *h);
void mh_menu_release(struct bbm_mod *m, void *p);
void mh_native_observation_tick(modhost *h);
void mh_native_observation_stop(modhost *h);
void mh_gameplay_tick(modhost *h);
bbm_status mh_gameplay_read(modhost *h, bbm_player_snapshot *out);

/* util */
void mh_strlcpy(char *dst, const char *src, size_t cap);
void mh_path_join(char *out, size_t cap, const char *a, const char *b);
void mh_lower(char *s);

#endif

void mh_characters_fill(bbm_host*);
void mh_characters_tick(modhost*);
void mh_characters_stop(modhost*);

void mh_ui_tables_fill(bbm_host*);
void mh_ui_tables_cleanup(struct bbm_mod*);
