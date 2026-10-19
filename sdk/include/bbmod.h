/* SPDX-License-Identifier: MIT OR Apache-2.0
 *
 * bbmod.h - the only interface between the bbport mod host and a mod.
 *
 * Rules (see docs/ABI.md):
 *   - Plain C types only. No CRT objects cross the boundary.
 *   - Whoever allocates frees. Blobs handed to the host use host->alloc.
 *   - Strings are UTF-8 and NUL-terminated unless a length is given.
 *   - Every resource a mod creates is owned by its bbm_handle and released
 *     automatically when the mod unloads. Explicit release is optional.
 *   - BBM_ABI_MAJOR changes break mods. BBM_ABI_MINOR adds compatible features
 *     or appends fields to the END of bbm_host; check host->size for new fields.
 *   - Host functions use the platform C calling convention. Functions that
 *     GAME code calls (hook detours) use System V (sysv64), as PS4 code does.
 */
#ifndef BBMOD_H
#define BBMOD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BBM_ABI_MAJOR 1
#define BBM_ABI_MINOR 10

#if defined(_WIN32)
#define BBM_EXPORT __declspec(dllexport)
#else
#define BBM_EXPORT __attribute__((visibility("default")))
#endif

/* ---- Basic types ------------------------------------------------------ */

typedef int32_t bbm_status;
enum {
    BBM_OK = 0,
    BBM_ERR = -1,             /* generic failure, see last_error */
    BBM_ERR_ARG = -2,         /* bad argument */
    BBM_ERR_CONFLICT = -3,    /* overlaps another mod's patch or hook */
    BBM_ERR_UNSUPPORTED = -4, /* not available on this host/platform */
    BBM_ERR_NOMEM = -5,
    BBM_ERR_STATE = -6,       /* not allowed now (e.g. outside the sync point) */
    BBM_ERR_NOTFOUND = -7,
    BBM_ERR_BUDGET = -8       /* network budget exceeded, message dropped */
};

typedef struct bbm_mod *bbm_handle; /* opaque, one per loaded mod instance */
typedef struct bbm_ctx bbm_ctx;     /* mod-defined state, opaque to the host */
typedef uint64_t bbm_id;            /* any owned resource; 0 is never valid */

/* ABI 1.10: one owned system text dialog. Strings are copied during begin.
 * max_bytes excludes the terminator (1..4095), counts UTF-8 bytes, not glyphs.
 * Release cancels a pending dialog. Terminal results are stable until release. */
enum { BBM_TEXT_PENDING=0, BBM_TEXT_ACCEPTED=1, BBM_TEXT_CANCELLED=2, BBM_TEXT_FAILED=3 };
typedef struct bbm_text_request {
    uint32_t size,max_bytes;
    char title[128],initial[4096];
} bbm_text_request;
typedef struct bbm_text_result {
    uint32_t size,state;
    bbm_status status;
    uint32_t reserved;
    char text[4096];
} bbm_text_result;

typedef struct bbm_version {
    uint16_t major, minor, patch, reserved;
} bbm_version;

typedef struct bbm_blob {
    uint8_t *data; /* allocated with host->alloc; the host frees it */
    size_t len;
} bbm_blob;

enum { BBM_LOG_DEBUG = 0, BBM_LOG_INFO = 1, BBM_LOG_WARN = 2, BBM_LOG_ERROR = 3 };

/* ---- Events ----------------------------------------------------------- */

enum {
    BBM_EV_AREA_LOADED = 1u << 0,   /* a = area id (when known) */
    BBM_EV_SAVE_LOADED = 1u << 1,
    BBM_EV_SESSION_JOIN = 1u << 2,  /* a = local peer id */
    BBM_EV_SESSION_LEAVE = 1u << 3,
    BBM_EV_PEER_JOIN = 1u << 4,     /* a = peer id */
    BBM_EV_PEER_LEAVE = 1u << 5,    /* a = peer id */
    BBM_EV_OWNER_CHANGED = 1u << 6, /* a = new owner peer id */
    BBM_EV_PAUSE = 1u << 7,
    BBM_EV_RESUME = 1u << 8,
    BBM_EV_MODSET_MISMATCH = 1u << 9, /* a = peer id, data = UTF-8 diff text */
    BBM_EV_PLAYER_CHANGED = 1u << 11, /* data: bbm_player_change, sampled, NOT damage */
    BBM_EV_EQUIPMENT_CHANGED = 1u << 15, /* data: bbm_equipment_change; sampled diagnostic */
    BBM_EV_INVENTORY_CHANGED = 1u << 17, /* data: bbm_inventory_change; sampled */
    BBM_EV_CHARACTERS_CHANGED = 1u << 19, /* sampled registry revision; data: bbm_character_summary */
    BBM_EV_ATTACK_OBSERVED = 1u << 18, /* data: bbm_attack_observed; behavior observation */
    BBM_EV_ITEM_USE_OBSERVED = 1u << 16, /* data: bbm_item_use; native action result */
    BBM_EV_PAD_CHANGED = 1u << 10 /* read-only: a = current buttons, b = previous buttons */
};

typedef struct bbm_event {
    uint32_t type; /* one BBM_EV_* bit */
    uint32_t reserved;
    uint64_t a, b;
    const void *data;
    size_t data_len;
} bbm_event;

/* ---- Shared gameplay (ABI 1.4) --------------------------------------- */
enum { BBM_PLAYER_AVAILABLE = 1u, BBM_RESOURCE_HEALTH = 1u, BBM_RESOURCE_STAMINA = 2u };
typedef struct bbm_player_snapshot {
    uint32_t size, flags;
    uint64_t generation;
    uintptr_t actor, component;
    int32_t health, max_health, stamina, max_stamina;
} bbm_player_snapshot;
typedef struct bbm_player_change {
    bbm_player_snapshot previous, current;
} bbm_player_change;

/* Raw diagnostic equipment layout. No named hand/armor mapping or inventory API.
 * References are opaque, not catalog IDs; saved IDs may lag runtime changes. */
typedef struct bbm_equipment_snapshot {
    uint32_t size, flags; /* bit 0: available */
    uint64_t generation;
    uintptr_t actor, character;
    uint32_t mode, selections[6], runtime_references[16], saved_item_ids[16];
    uint32_t selected_even, selected_odd; /* array indices or UINT32_MAX */
    uint32_t reserved;
} bbm_equipment_snapshot;
typedef struct bbm_equipment_change {
    bbm_equipment_snapshot previous, current;
} bbm_equipment_change;
/* source 1: successful behavior caller; source 2: stamina-update caller.
 * Behavior identifiers are not weapon IDs; both sites may report one action. */
typedef struct bbm_attack_observed {
    uint32_t size, source;
    uint64_t generation;
    uintptr_t actor;
    int32_t behavior_id;
    uint32_t reserved;
} bbm_attack_observed;

typedef struct bbm_item_use {
    uint32_t size, success;
    uint64_t generation;
    uintptr_t actor;
    int32_t item_id;
    uint32_t reserved;
} bbm_item_use;

/* Queued native item award. SUBMITTED means the native award batch was called;
 * it does not guarantee later inventory insertion or persistence. Terminal
 * status polling consumes the request. Pending requests cancel on unload. */
enum { BBM_ITEM_PENDING=1, BBM_ITEM_RUNNING=2, BBM_ITEM_SUBMITTED=3,
       BBM_ITEM_CANCELLED=4, BBM_ITEM_FAILED=5 };
typedef struct bbm_item_give_result {
    uint32_t size, state;
    uint64_t request_id, generation;
    uint32_t encoded_item_id, quantity;
} bbm_item_give_result;

/* Read-only carried inventory. Pages share a bounded host cache refreshed no
 * more often than every 250 ms. revision=0 selects its latest snapshot; pass the
 * returned revision for every following page, or STATE rejects mixed versions.
 * flags bit0 means available/resolved respectively; IDs are category encoded.
 * Metadata words remain deliberately opaque. No storage or mutation API. */
#define BBM_INVENTORY_PAGE_SIZE 64
#define BBM_INVENTORY_MAX_ITEMS 4096
#define BBM_INVENTORY_AVAILABLE 1u
#define BBM_INVENTORY_ID_RESOLVED 1u
/* ABI 1.6: shared, read-only loaded-character cache, at most every 250 ms.
 * A missing character is not proof of death. Flags gate optional fields.
 * Tokens are host sample lifetimes, not native handles or save identities. */
enum { BBM_CHARACTER_AVAILABLE=1, BBM_CHARACTER_NPC_PARAM=2,
       BBM_CHARACTER_AI_MODULE=4, BBM_CHARACTER_SCRIPT_VALUE=8, BBM_CHARACTER_HEALTH=16 };
#define BBM_CHARACTER_PAGE_SIZE 32
#define BBM_CHARACTER_MAX 512
typedef struct bbm_character {
    uint32_t size, flags;
    uint64_t token;
    uint32_t handle, kind; /* 0 unknown; 1 local player; 2 player class; 3 NpcIns (not necessarily hostile) */
    int32_t health, max_health, team_type, npc_param, name_id, script_value;
    int32_t backread_state, update_interval; /* raw diagnostic scheduling values */
} bbm_character;
typedef struct bbm_character_summary {
    uint32_t size, flags; /* flags=1 means a complete valid sample */
    uint64_t revision, epoch;
    uint32_t count, reserved;
} bbm_character_summary;
typedef struct bbm_character_page {
    bbm_character_summary snapshot;
    uint32_t offset, count;
    bbm_character entries[BBM_CHARACTER_PAGE_SIZE];
} bbm_character_page;

typedef struct bbm_inventory_entry {
    uint32_t slot, reference, encoded_item_id, quantity;
    uint32_t metadata_04, metadata_0c, flags, reserved;
} bbm_inventory_entry;
typedef struct bbm_inventory_summary {
    uint32_t size, flags;
    uint64_t generation, revision;
    uintptr_t actor, character;
    uint32_t slot_count, item_count;
} bbm_inventory_summary;
typedef struct bbm_inventory_page {
    bbm_inventory_summary snapshot;
    uint32_t offset, count;
    bbm_inventory_entry entries[BBM_INVENTORY_PAGE_SIZE];
} bbm_inventory_page;
typedef struct bbm_inventory_change {
    bbm_inventory_summary previous, current;
} bbm_inventory_change;

/* ---- Multiplayer ------------------------------------------------------ */

enum { BBM_NET_UNRELIABLE = 0, BBM_NET_RELIABLE = 1u << 0, BBM_NET_ORDERED = 1u << 1 };

typedef struct bbm_session_info {
    uint32_t in_session;  /* 0 or 1 */
    uint32_t is_owner;    /* 1 when the local player owns the room */
    uint32_t dev_session; /* 1 when the owner allows hot reload in session */
    uint32_t peer_count;  /* remote peers, not counting the local player */
    uint64_t local_peer;
    uint64_t owner_peer;
    uint64_t modset_hash; /* hash of session/host-authoritative mods */
} bbm_session_info;

typedef void (*bbm_net_cb)(void *user, uint64_t peer, const void *data, size_t len);

/* ---- Input ------------------------------------------------------------- */

/* Keys are SDL scancodes (= USB HID usage ids): A..Z = 4..29, 1..9,0 = 30..39,
 * Enter 40, Escape 41, Backspace 42, Tab 43, Space 44, F1..F12 = 58..69,
 * Insert 73 (opens bbport's menu), Home 74, PageUp 75, Delete 76, End 77,
 * PageDown 78, Right 79, Left 80, Down 81, Up 82, keypad 1..9,0 = 89..98,
 * LCtrl 224, LShift 225, LAlt 226, RCtrl 228, RShift 229, RAlt 230.
 * Input reads as released while bbport's menu or a text box has focus. */
#define BBM_KEY_COUNT 512

/* Pad buttons as the game sees them (PS4 bits). */
enum {
    BBM_PAD_L3 = 0x2, BBM_PAD_R3 = 0x4, BBM_PAD_OPTIONS = 0x8,
    BBM_PAD_UP = 0x10, BBM_PAD_RIGHT = 0x20, BBM_PAD_DOWN = 0x40, BBM_PAD_LEFT = 0x80,
    BBM_PAD_L2 = 0x100, BBM_PAD_R2 = 0x200, BBM_PAD_L1 = 0x400, BBM_PAD_R1 = 0x800,
    BBM_PAD_TRIANGLE = 0x1000, BBM_PAD_CIRCLE = 0x2000, BBM_PAD_CROSS = 0x4000,
    BBM_PAD_SQUARE = 0x8000, BBM_PAD_TOUCHPAD = 0x100000
};

/* ---- Memory regions (ABI 1.2) ------------------------------------------ */

enum { BBM_REGION_WRITE = 1u << 0, BBM_REGION_EXEC = 1u << 1, BBM_REGION_IMAGE = 1u << 2 };

typedef struct bbm_region {
    uintptr_t base;
    size_t size;
    uint32_t flags; /* BBM_REGION_* */
    uint32_t reserved;
} bbm_region;

/* ---- Callback types --------------------------------------------------- */

typedef void (*bbm_frame_cb)(void *user);
typedef void (*bbm_panel_cb)(void *user);
typedef void (*bbm_thread_fn)(void *user, const volatile int32_t *stop);

/* ---- Host function table ---------------------------------------------- */

/* ABI 1.3: physical controller effects. Calls serialized by the host. */
enum { BBM_PAD_NONE, BBM_PAD_DUALSHOCK4, BBM_PAD_DUALSENSE, BBM_PAD_DUALSENSE_EDGE, BBM_PAD_OTHER };
enum { BBM_PAD_UNKNOWN, BBM_PAD_USB, BBM_PAD_BLUETOOTH };
enum { BBM_PAD_HAS_TRIGGERS = 1, BBM_PAD_HAS_LIGHTBAR = 2, BBM_PAD_CAPTURED = 4, BBM_PAD_SEND_FAILED = 8 };
enum { BBM_TRIGGER_LEFT, BBM_TRIGGER_RIGHT };
typedef struct bbm_pad_info {
    uint32_t kind, connection, features;
    uint8_t l2_depth, r2_depth;
    uint16_t reserved;
    uint64_t device_id; /* changes on reconnect; zero means absent */
} bbm_pad_info;

/* ABI 1.8: declarative native menus. All strings are copied. No engine callback
 * points into a mod DLL. Keys are local to the owner; custom paths are
 * "<mod-id>/<screen-key>". Supported locations are capability gated. */
#define BBM_EV_NATIVE_MENU (1u << 20)
#define BBM_MENU_SCREEN 1u
#define BBM_MENU_ACTION 2u
#define BBM_MENU_LINK 3u
#define BBM_MENU_TOGGLE 4u
#define BBM_MENU_INFO 5u /* read-only text/value row; never emits an action */
#define BBM_MENU_ENABLED 1u
#define BBM_MENU_CAP_REGISTRY 1u
#define BBM_MENU_CAP_LISTS 2u
#define BBM_MENU_CAP_SYSTEM 4u
#define BBM_MENU_CAP_INGAME 8u
#define BBM_MENU_CAP_OBSERVE 16u
#define BBM_MENU_CAP_SELECTION 32u
#define BBM_MENU_CAP_LIVE 64u /* refreshes open custom lists on native UI thread */
#define BBM_MENU_OPENED 1u
#define BBM_MENU_CLOSED 2u
#define BBM_MENU_ACTIVATED 3u
#define BBM_MENU_VALUE_CHANGED 4u
#define BBM_MENU_CREATED 5u
#define BBM_MENU_REGISTRY_CHANGED 6u
#define BBM_MENU_SELECTION_CHANGED 7u
typedef struct bbm_menu_node {
    uint32_t size, kind, flags;
    int32_t order, value;
    char key[64], parent[192], label[128], help[256], target[192];
} bbm_menu_node;
typedef struct bbm_menu_state {
    uint32_t size, capabilities;
    uint64_t revision, generation;
    char screen[192]; /* empty = no observed active menu */
} bbm_menu_state;
typedef struct bbm_menu_event {
    uint32_t size, kind;
    uint64_t generation, revision, node;
    int32_t value;
    uint32_t reserved;
    uint64_t control; /* opaque native list token; never a game pointer */
    int32_t previous_value;
    uint32_t flags;
    char owner[128], key[64], screen[192];
} bbm_menu_event;

typedef struct bbm_host {
    uint32_t size; /* sizeof(bbm_host) as built by the host */
    uint16_t abi_major, abi_minor;

    /* Core */
    void (*log)(bbm_handle self, int32_t level, const char *msg);
    const char *(*last_error)(bbm_handle self);
    void *(*alloc)(size_t n);
    void (*free)(void *p);
    /* Reads [section] key from the mod's mod.toml (or a user override).
     * Copies a NUL-terminated string into out. */
    bbm_status (*config_get)(bbm_handle self, const char *key, char *out, size_t cap);
    void (*mod_info)(bbm_handle self, const char **id, bbm_version *version);
    /* Pointer to this mod's in-flight counter. SDK detours increment it on
     * entry and decrement on exit, so unload can wait for them to drain. */
    volatile int32_t *(*inflight)(bbm_handle self);
    /* Releases any owned resource (patch, hook, callback, panel, ...). */
    bbm_status (*release)(bbm_handle self, bbm_id id);

    /* Memory (guest pointers are host pointers in bbport) */
    uintptr_t (*eboot_base)(bbm_handle self);
    /* PS4 virtual address as used by XML patches (eboot at 0x400000) to host. */
    uintptr_t (*ps4_to_host)(bbm_handle self, uint64_t ps4_addr);
    /* Fault-safe read: returns BBM_ERR on unmapped memory instead of crashing. */
    bbm_status (*read)(bbm_handle self, uintptr_t addr, void *out, size_t n);
    /* Unowned data write (not reverted on unload). Use patch_bytes for code. */
    bbm_status (*write)(bbm_handle self, uintptr_t addr, const void *src, size_t n);
    /* IDA-style pattern, e.g. "48 8B ?? ?? 89". len 0 = the whole eboot image. */
    bbm_status (*scan)(bbm_handle self, const char *pattern, uintptr_t start, size_t len,
                       uintptr_t *out);

    /* Patches (owned, reverted on unload) */
    bbm_status (*patch_bytes)(bbm_handle self, uintptr_t addr, const void *bytes, size_t n,
                              bbm_id *out);
    /* Loads a shadPS4/GoldHEN XML patch file. name = Metadata Name to apply,
     * or NULL for every entry with isEnabled="true". Path may be relative to
     * the mod folder. */
    bbm_status (*patch_xml)(bbm_handle self, const char *path, const char *name, bbm_id *out);

    /* Hooks (owned, removed on unload) */
    /* Inline hook. detour must be sysv64. *trampoline receives a callable
     * pointer to the original function. */
    bbm_status (*hook_inline)(bbm_handle self, uintptr_t target, void *detour,
                              void **trampoline, bbm_id *out);
    /* Pointer-slot hook (vtable entry, function table). *original receives
     * the previous slot value. */
    bbm_status (*hook_slot)(bbm_handle self, void **slot, void *detour, void **original,
                            bbm_id *out);

    /* Scheduling (callbacks run on the game's main thread at the sync point) */
    bbm_status (*on_frame)(bbm_handle self, bbm_frame_cb cb, void *user, bbm_id *out);
    bbm_status (*thread_spawn)(bbm_handle self, bbm_thread_fn fn, void *user, bbm_id *out);
    bbm_status (*subscribe)(bbm_handle self, uint32_t event_mask);

    /* Overlay UI. ui_panel registers a window; the draw callback may call
     * the ui_* widgets below, which are only valid during that callback. */
    bbm_status (*ui_panel)(bbm_handle self, const char *title, bbm_panel_cb draw, void *user,
                           bbm_id *out);
    void (*ui_notify)(bbm_handle self, const char *msg);
    void (*ui_text)(bbm_handle self, const char *text);
    int32_t (*ui_button)(bbm_handle self, const char *label);
    int32_t (*ui_checkbox)(bbm_handle self, const char *label, int32_t *value);
    int32_t (*ui_slider_float)(bbm_handle self, const char *label, float *value, float min,
                               float max);
    void (*ui_separator)(bbm_handle self);
    void (*ui_same_line)(bbm_handle self);
    /* Raw ImGui context for mods that link their own matching Dear ImGui. */
    void *(*ui_imgui_context)(bbm_handle self);

    /* Files (owned, removed on unload): serve source_path when the game
     * opens game_path (e.g. "dvdroot_ps4/param/gameparam.parambnd.dcx"). */
    bbm_status (*file_override)(bbm_handle self, const char *game_path, const char *source_path,
                                bbm_id *out);

    /* Multiplayer (ModNet) */
    bbm_status (*net_channel_open)(bbm_handle self, const char *name, uint32_t flags,
                                   bbm_id *out);
    bbm_status (*net_on_message)(bbm_handle self, bbm_id channel, bbm_net_cb cb, void *user);
    bbm_status (*net_send)(bbm_handle self, bbm_id channel, uint64_t peer, const void *data,
                           size_t len);
    bbm_status (*net_broadcast)(bbm_handle self, bbm_id channel, const void *data, size_t len);
    bbm_status (*session_info)(bbm_handle self, bbm_session_info *out);

    /* ---- ABI 1.1: check host->size covers a field before calling it ---- */

    /* Input, sampled once per frame at the sync point. */
    int32_t (*key_down)(bbm_handle self, int32_t scancode);
    int32_t (*key_pressed)(bbm_handle self, int32_t scancode); /* went down this frame */
    uint32_t (*pad_buttons)(bbm_handle self);
    uint32_t (*pad_pressed)(bbm_handle self);                  /* bits that went down this frame */

    /* More widgets (panel callbacks only). */
    int32_t (*ui_slider_int)(bbm_handle self, const char *label, int32_t *value, int32_t min,
                             int32_t max);
    int32_t (*ui_input_text)(bbm_handle self, const char *label, char *buf, size_t cap);
    int32_t (*ui_collapsing_header)(bbm_handle self, const char *label); /* 1 = open */
    /* items: "First\0Second\0Third\0\0" */
    int32_t (*ui_combo)(bbm_handle self, const char *label, int32_t *current, const char *items);
    void (*ui_text_colored)(bbm_handle self, float r, float g, float b, float a, const char *text);

    /* ---- ABI 1.2 ---- */

    /* Bytes of the eboot image starting at eboot_base (code, data and bss). */
    size_t (*eboot_size)(bbm_handle self);
    /* The first committed, readable region that ends above addr (memory scanners
     * walk the address space with it: start at 0, then at base + size).
     * BBM_ERR_NOTFOUND past the last one. Any thread. */
    bbm_status (*mem_region)(bbm_handle self, uintptr_t addr, bbm_region *out);
    /* ABI 1.3. release is idempotent for an unclaimed trigger. */
    bbm_status (*pad_info)(bbm_handle self, bbm_pad_info *out);
    bbm_status (*pad_trigger)(bbm_handle self, uint32_t which, const uint8_t effect[11]);
    bbm_status (*pad_release)(bbm_handle self, uint32_t which);
    bbm_status (*pad_lightbar)(bbm_handle self, uint8_t r, uint8_t g, uint8_t b);

    /* ABI 1.4: bounded CUSA03173 1.09 lookup; sampled observations, not detours.
     * Writes are explicit deltas, never reverted on unload. They require a game
     * callback, current generation and expected value; no resurrection/killing.
     * Scales 0..4000 permille compose by priority, mod id, then resource id,
     * rounding toward zero at each stage. They affect only player_adjust.
     * Claims are owned resources; release/unload removes them. */
    bbm_status (*player_snapshot)(bbm_handle self, bbm_player_snapshot *out);
    bbm_status (*player_adjust)(bbm_handle self, uint32_t field, uint64_t generation,
                               int32_t expected, int32_t delta, int32_t *actual_delta);
    bbm_status (*player_modifier)(bbm_handle self, uint32_t field, uint32_t scale_milli,
                                 int32_t priority, bbm_id *out);
    bbm_status (*ui_contribute)(bbm_handle self, const char *slot, const char *key,
                               int32_t order, bbm_panel_cb draw, void *user, bbm_id *out);
    bbm_status (*equipment_snapshot)(bbm_handle self, bbm_equipment_snapshot *out);
    bbm_status (*inventory_page)(bbm_handle self, uint64_t revision, uint32_t offset,
                                uint32_t limit, bbm_inventory_page *out);

    /* ABI 1.5: native player-update item queue; see bbm_item_give_result. */
    bbm_status (*item_give)(bbm_handle self, uint32_t encoded_item_id, uint32_t quantity,
                            uint64_t generation, uint64_t *request_id);
    bbm_status (*item_give_status)(bbm_handle self, uint64_t request_id, bbm_item_give_result *out);
    /* Optional size-gated extension: stereo actuator PCM, 48 kHz, <= 12000
     * frames, finite samples in [-0.5,0.5]. Replaces the previous finite clip.
     * frames=0 releases ownership and stops playback. No pointer retained. */
    bbm_status (*pad_haptics)(bbm_handle self, const float *stereo, uint32_t frames);
    bbm_status (*characters_page)(bbm_handle self, uint64_t revision, uint32_t offset, uint32_t limit, bbm_character_page *out);
    /* ABI 1.7: one scoped sortable table per mod draw. Begin: 1 visible,
     * 0 clipped, -1 unsupported/invalid. Headers are borrowed for this call.
     * sort_column/direction receive the current single-column sort (0/1). */
    int32_t (*ui_table_begin)(bbm_handle,const char*,const char*const*,uint32_t,float,int32_t*,int32_t*);
    void (*ui_table_next_row)(bbm_handle);
    void (*ui_table_next_column)(bbm_handle);
    void (*ui_table_end)(bbm_handle);
    /* Registration works offline; capabilities distinguish registry from a
     * connected native adapter. Updates take effect on the next menu build.
     * An open native list is immutable. Activation is validated again against
     * resource ID/revision at the sync point; stale actions are discarded. */
    bbm_status (*menu_register)(bbm_handle, const bbm_menu_node*, bbm_id*);
    bbm_status (*menu_update)(bbm_handle, bbm_id, const bbm_menu_node*);
    bbm_status (*menu_state)(bbm_handle, bbm_menu_state*);
    bbm_status (*text_begin)(bbm_handle, const bbm_text_request*, bbm_id*);
    bbm_status (*text_poll)(bbm_handle, bbm_id, bbm_text_result*);
} bbm_host;

/* ---- Mod function table (filled by the mod in bbmod_entry) ------------- */

typedef struct bbm_mod_api {
    uint32_t size; /* sizeof(bbm_mod_api) as built by the mod */
    uint16_t abi_major, abi_minor;
    bbm_status (*on_load)(bbm_ctx *ctx);
    void (*on_unload)(bbm_ctx *ctx);
    /* Optional: hand state to the next version across a hot reload. */
    bbm_status (*on_save_state)(bbm_ctx *ctx, bbm_blob *out);
    bbm_status (*on_restore_state)(bbm_ctx *ctx, const bbm_blob *state, bbm_version from);
    /* Optional: events chosen with host->subscribe. */
    void (*on_event)(bbm_ctx *ctx, const bbm_event *ev);
    /* Called after on_unload; frees ctx. */
    void (*destroy)(bbm_ctx *ctx);
} bbm_mod_api;

/* The only symbol a mod exports. */
typedef bbm_status (*bbm_entry_fn)(const bbm_host *host, bbm_handle self, bbm_mod_api *out_api,
                                   bbm_ctx **out_ctx);
#define BBM_ENTRY_SYMBOL "bbmod_entry"

#ifdef __cplusplus
}
#endif

#endif /* BBMOD_H */
