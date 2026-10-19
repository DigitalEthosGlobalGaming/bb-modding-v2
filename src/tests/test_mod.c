/* A tiny mod used by the test harness. It exercises patches, a slot hook,
 * a frame callback, a panel, a file override and state hand-off. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "bbmod.h"

typedef struct bbm_ctx {
    const bbm_host *h;
    bbm_handle self;
    uint32_t frames;
    uint32_t generation;
    uint32_t presses, pad_presses;
} ctx_t;

static ctx_t g_ctx; /* test only: a real mod would heap allocate */

/* ABI 1.1 fields exist only if the host's table is big enough. */
#define HAS(h, field) ((h)->size >= offsetof(bbm_host, field) + sizeof((h)->field))

static void on_frame(void *user) {
    ctx_t *c = user;
    c->frames++;
    if (HAS(c->h, key_pressed) && c->h->key_pressed(c->self, 62 /* F5 */)) c->presses++;
    if (HAS(c->h, pad_pressed) && (c->h->pad_pressed(c->self) & BBM_PAD_TRIANGLE)) c->pad_presses++;
}
static void on_panel(void *user) {
    ctx_t *c = user;
    char t[64];
    snprintf(t, sizeof t, "presses=%u pad=%u", c->presses, c->pad_presses);
    c->h->ui_text(c->self, t);
}

/* sysv64 detour for the slot hook the harness set up */
static __attribute__((sysv_abi)) int detour(int x) { return x + 1000; }
static void *g_orig;

static bbm_status on_load(bbm_ctx *ctx) {
    const bbm_host *h = ctx->h;
    bbm_id id;
    uintptr_t base = h->eboot_base(ctx->self);
    static const uint8_t nops[4] = {0x90, 0x90, 0x90, 0x90};
    if (h->patch_bytes(ctx->self, base + 0x10, nops, 4, &id)) return BBM_ERR;
    if (h->hook_slot(ctx->self, (void **)(base + 0x100), (void *)detour, &g_orig, &id)) return BBM_ERR;
    if (h->on_frame(ctx->self, on_frame, ctx, &id)) return BBM_ERR;
    if (h->ui_panel(ctx->self, "Test", on_panel, ctx, &id)) return BBM_ERR;
    if (h->file_override(ctx->self, "dvdroot_ps4/test/a.bin", "/tmp/a.bin", &id)) return BBM_ERR;
    ctx->generation++;
    return BBM_OK;
}
static void on_unload(bbm_ctx *ctx) { (void)ctx; }
static bbm_status on_save(bbm_ctx *ctx, bbm_blob *out) {
    uint32_t v[2] = {ctx->frames, ctx->generation};
    ctx->presses = ctx->pad_presses = 0;
    out->data = ctx->h->alloc(sizeof v);
    if (!out->data) return BBM_ERR_NOMEM;
    memcpy(out->data, v, sizeof v);
    out->len = sizeof v;
    return BBM_OK;
}
static bbm_status on_restore(bbm_ctx *ctx, const bbm_blob *s, bbm_version from) {
    (void)from;
    uint32_t v[2];
    if (s->len != sizeof v) return BBM_ERR_ARG;
    memcpy(v, s->data, sizeof v);
    ctx->frames = v[0];
    ctx->generation = v[1];
    return BBM_OK;
}
static void destroy(bbm_ctx *ctx) { (void)ctx; }

BBM_EXPORT bbm_status bbmod_entry(const bbm_host *host, bbm_handle self, bbm_mod_api *api,
                                  bbm_ctx **out_ctx) {
    if (host->abi_major != BBM_ABI_MAJOR) return BBM_ERR;
    g_ctx.h = host;
    g_ctx.self = self;
    api->size = sizeof *api;
    api->abi_major = BBM_ABI_MAJOR;
    api->abi_minor = BBM_ABI_MINOR;
    api->on_load = on_load;
    api->on_unload = on_unload;
    api->on_save_state = on_save;
    api->on_restore_state = on_restore;
    api->destroy = destroy;
    *out_ctx = &g_ctx;
    return BBM_OK;
}
