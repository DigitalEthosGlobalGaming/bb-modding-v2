/* A mod that misbehaves on purpose, chosen by [test] crash = ...:
 *   frame  - writes through NULL on its 3rd frame
 *   panel  - writes through NULL when its panel is drawn
 *   load   - writes through NULL in on_load
 *   thread - starts a thread that ignores its stop flag
 *   none   - behaves */
#include <string.h>
#include "bbmod.h"

typedef struct bbm_ctx { int dummy; } ctx_t;
static ctx_t g_ctx;
static const bbm_host *H;
static bbm_handle S;
static char mode[16];
static int frames;

static void crash(void) { *(volatile int *)(uintptr_t)0 = 42; }
static void on_frame(void *u) { (void)u; if (++frames == 3 && !strcmp(mode, "frame")) crash(); }
static void on_panel(void *u) { (void)u; if (!strcmp(mode, "panel")) crash(); H->ui_text(S, "crash mod ok"); }
static void stubborn(void *u, const volatile int32_t *stop) { (void)u; (void)stop; for (;;) { } }

static bbm_status on_load(bbm_ctx *c) {
    (void)c;
    if (H->config_get(S, "test.crash", mode, sizeof mode)) strcpy(mode, "none");
    if (!strcmp(mode, "load")) crash();
    bbm_id id;
    static const uint8_t b = 0x90;
    if (H->patch_bytes(S, H->eboot_base(S) + 0x20, &b, 1, &id)) return BBM_ERR;
    if (H->on_frame(S, on_frame, NULL, &id)) return BBM_ERR;
    if (H->ui_panel(S, "Crash", on_panel, NULL, &id)) return BBM_ERR;
    if (!strcmp(mode, "thread") && H->thread_spawn(S, stubborn, NULL, &id)) return BBM_ERR;
    return BBM_OK;
}
static void noop(bbm_ctx *c) { (void)c; }

BBM_EXPORT bbm_status bbmod_entry(const bbm_host *host, bbm_handle self, bbm_mod_api *api, bbm_ctx **out) {
    H = host; S = self; frames = 0;
    api->size = sizeof *api; api->abi_major = BBM_ABI_MAJOR; api->abi_minor = BBM_ABI_MINOR;
    api->on_load = on_load; api->on_unload = noop; api->destroy = noop;
    *out = &g_ctx;
    return BBM_OK;
}
