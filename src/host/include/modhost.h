/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * modhost.h - the integrator side of the mod host.
 *
 * bbport (or the test harness) creates one modhost, calls modhost_frame() once
 * per frame from the game's main thread (the "sync point"), and wires the
 * optional callbacks below to its own systems. Everything is instance based:
 * several hosts can live in one process (the network tests use two).
 */
#ifndef MODHOST_H
#define MODHOST_H

#include <stddef.h>
#include <stdint.h>

#include "bbmod.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct modhost modhost;

/* Optional overrides so the host can respect bbport's own memory systems
 * (GPU write tracking, the vectored exception handler, guest threads).
 * Any NULL member falls back to the built-in platform implementation. */
typedef struct modhost_platform_hooks {
    void *user;
    /* Write code or patch bytes: make writable, copy, restore, flush icache. */
    int (*write_code)(void *user, void *addr, const void *src, size_t n);
    /* Fault-safe read; return 0 on success. */
    int (*read_safe)(void *user, const void *addr, void *out, size_t n);
    /* Suspend every other thread that can run game code (0 = ok). */
    int (*suspend_world)(void *user);
    void (*resume_world)(void *user);
    /* While suspended: 1 if any thread's instruction pointer is in [lo, hi). */
    int (*any_thread_in)(void *user, uintptr_t lo, uintptr_t hi);
    /* Run fn(arg); if it faults, recover and return nonzero. bbport implements
     * this with its runtime_fault_recover/bb_longjmp path. NULL = plain call. */
    int (*call_guarded)(void *user, void (*fn)(void *), void *arg);
} modhost_platform_hooks;

/* Input for mods, sampled at each sync point. */
typedef struct modhost_input_backend {
    void *user;
    /* SDL_GetKeyboardState-style array (1 byte per scancode), or NULL. Return
     * NULL while the game's input is captured by a menu. */
    const uint8_t *(*keyboard)(void *user, int *count);
    uint32_t (*pad_buttons)(void *user);
} modhost_input_backend;

/* How ModNet frames leave this machine. bbport maps reliable frames to
 * shadNet room messages and unreliable ones to its direct UDP path. */
typedef struct modhost_net_transport {
    void *user;
    /* peer == 0 means every peer in the room. Return 0 on success. */
    int (*send)(void *user, uint64_t peer, const void *frame, size_t len, int reliable);
} modhost_net_transport;

/* Widgets for mod panels; bbport implements these with Dear ImGui. */
typedef struct modhost_ui_backend {
    void *user;
    int (*begin_panel)(void *user, const char *title, const char *mod_id); /* 1 = visible */
    void (*end_panel)(void *user);
    void (*text)(void *user, const char *text);
    int (*button)(void *user, const char *label);
    int (*checkbox)(void *user, const char *label, int32_t *value);
    int (*slider_float)(void *user, const char *label, float *value, float min, float max);
    void (*separator)(void *user);
    void (*same_line)(void *user);
    void (*notify)(void *user, const char *mod_id, const char *msg);
    void *(*imgui_context)(void *user);
    int (*slider_int)(void *user, const char *label, int32_t *value, int32_t min, int32_t max);
    int (*input_text)(void *user, const char *label, char *buf, size_t cap);
    int (*collapsing_header)(void *user, const char *label);
    int (*combo)(void *user, const char *label, int32_t *current, const char *items);
    void (*text_colored)(void *user, float r, float g, float b, float a, const char *text);
    /* Required for shared contribution slots: isolate identical widget labels. */
    void (*push_scope)(void *user, const char *owner, const char *key);
    void (*pop_scope)(void *user);
    int (*table_begin)(void*,const char*,const char*const*,uint32_t,float,int32_t*,int32_t*);
    void (*table_next_row)(void*);
    void (*table_next_column)(void*);
    void (*table_end)(void*);
} modhost_ui_backend;

typedef struct modhost_pad_backend {
    void *user;
    int (*info)(void *user, bbm_pad_info *out);
    int (*send_triggers)(void *user, const uint8_t *right11, const uint8_t *left11);
    int (*lightbar)(void *user, uint8_t r, uint8_t g, uint8_t b);
    int (*captured)(void *user);
    int (*haptics)(void *user, const float *stereo, uint32_t frames);
} modhost_pad_backend;

typedef struct modhost_config {
    const char *mods_dir;  /* folder with one sub-folder per mod */
    const char *cache_dir; /* shadow copies of mod DLLs (created if missing) */

    uintptr_t eboot_base;    /* host address of the loaded eboot image */
    size_t eboot_size;       /* bytes, for scan() and patch range checks */
    uint64_t ps4_eboot_base; /* 0x400000 for Bloodborne; 0 = use 0x400000 */

    int dev_mode;  /* 1 = watch DLLs/XML and hot reload on change */
    int safe_mode; /* 1 = discover mods but load none */
    int in_session_reload_policy; /* 0 = block session mods in session unless dev session */

    uint32_t watch_interval_ms;        /* default 250 */
    uint32_t drain_timeout_ms;         /* default 500 */
    uint32_t net_budget_bytes_per_sec; /* per peer, default 16384 */

    void *log_user;
    void (*log)(void *user, const char *mod_id, int level, const char *msg);

    modhost_platform_hooks platform;
    modhost_net_transport net;
    modhost_ui_backend ui;
    modhost_input_backend input;
    modhost_pad_backend pad;
    int native_observation; /* opt-in verified built-in item observer; default 0 */
} modhost_config;

typedef enum modhost_state {
    MODHOST_DISCOVERED = 0,
    MODHOST_ACTIVE,
    MODHOST_UNLOADED,
    MODHOST_FAULTED,
    MODHOST_DISABLED
} modhost_state;

typedef enum modhost_scope {
    MODHOST_SCOPE_CLIENT = 0,
    MODHOST_SCOPE_SESSION,
    MODHOST_SCOPE_HOST_AUTHORITATIVE
} modhost_scope;

typedef struct modhost_mod_view {
    const char *id, *name, *dir, *last_error;
    bbm_version version;
    modhost_state state;
    modhost_scope scope;
    uint32_t protocol;
    int has_code, hot_reload;
    uint32_t reload_count;
    uint32_t resources;     /* live owned resources */
    double frame_ms;        /* last frame's callback time */
    uint64_t net_bytes_out; /* lifetime */
} modhost_mod_view;

/* Lifecycle */
modhost *modhost_create(const modhost_config *cfg);
void modhost_destroy(modhost *h); /* unloads every mod */
int modhost_scan(modhost *h);     /* discover, sort, load enabled; returns mods found */
void modhost_frame(modhost *h);   /* sync point: new-folder discovery (5 s), reloads, callbacks */

/* Control (safe from any thread: requests run at the next sync point) */
int modhost_request_load(modhost *h, const char *id);
int modhost_request_unload(modhost *h, const char *id);
int modhost_request_reload(modhost *h, const char *id);

/* Immediate control (main thread only, outside game hooks) */
int modhost_load_now(modhost *h, const char *id);
int modhost_unload_now(modhost *h, const char *id);
int modhost_reload_now(modhost *h, const char *id);

/* Threads. modhost_frame, the *_now calls, scan and destroy belong to the game
 * thread. The overlay (another thread) draws with:
 *     if (modhost_try_lock(h)) { ...modhost_mod_view_at...; modhost_draw_panels(h); modhost_unlock(h); }
 * try_lock never blocks, so a reload in progress just skips one overlay frame.
 * Events from network threads (net_receive, session_*, peer_*, post_event) are
 * queued and delivered at the next sync point. modhost_vfs_resolve is safe from
 * any thread. */
int modhost_try_lock(modhost *h);
void modhost_unlock(modhost *h);

/* Introspection for the launcher / overlay (hold modhost_try_lock) */
size_t modhost_mod_count(const modhost *h);
int modhost_mod_view_at(const modhost *h, size_t i, modhost_mod_view *out);
void modhost_draw_panels(modhost *h); /* overlay thread; takes the lock itself (recursive) */

/* Faults (call from the exception handler) */
const char *modhost_fault_owner(const modhost *h, uintptr_t ip);
int modhost_on_fault(modhost *h, uintptr_t ip); /* 1 = ip was in a mod; mod bypassed */

/* Game and session events */
void modhost_post_event(modhost *h, uint32_t type, uint64_t a, uint64_t b);
void modhost_session_begin(modhost *h, uint64_t local_peer, uint64_t owner_peer, int dev_session);
void modhost_session_end(modhost *h);
void modhost_peer_join(modhost *h, uint64_t peer);
void modhost_peer_leave(modhost *h, uint64_t peer);
void modhost_net_receive(modhost *h, uint64_t peer, const void *frame, size_t len);
uint64_t modhost_modset_hash(modhost *h);

/* Files: 1 and a path in out when a mod overrides game_path. */
int modhost_vfs_resolve(modhost *h, const char *game_path, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* MODHOST_H */
