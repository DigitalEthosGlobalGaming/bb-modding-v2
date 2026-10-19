/* Inline-hook test mod. Hooks the function at eboot+[test].target and
 * doubles its result. Detour is sysv64 and counts itself in-flight. */
#include <stdlib.h>
#include "bbmod.h"

typedef __attribute__((sysv_abi)) int (*fn_t)(int);
typedef struct bbm_ctx { const bbm_host *h; bbm_handle self; } ctx_t;
static ctx_t g;
static void *g_tramp;

static __attribute__((sysv_abi)) int detour(int x) {
    volatile int32_t *f = g.h->inflight(g.self);
    __atomic_fetch_add(f, 1, __ATOMIC_ACQ_REL);
    int r = ((fn_t)g_tramp)(x) * 2;
    __atomic_fetch_sub(f, 1, __ATOMIC_ACQ_REL);
    return r;
}

static bbm_status on_load(bbm_ctx *c) {
    char buf[32];
    if (c->h->config_get(c->self, "test.target", buf, sizeof buf)) return BBM_ERR_ARG;
    uintptr_t t = c->h->eboot_base(c->self) + (uintptr_t)strtoul(buf, NULL, 0);
    bbm_id id;
    return c->h->hook_inline(c->self, t, (void *)detour, &g_tramp, &id);
}
static void on_unload(bbm_ctx *c) { (void)c; }
static void destroy(bbm_ctx *c) { (void)c; }

BBM_EXPORT bbm_status bbmod_entry(const bbm_host *host, bbm_handle self, bbm_mod_api *api, bbm_ctx **out) {
    g.h = host; g.self = self;
    api->size = sizeof *api; api->abi_major = BBM_ABI_MAJOR; api->abi_minor = BBM_ABI_MINOR;
    api->on_load = on_load; api->on_unload = on_unload; api->destroy = destroy;
    *out = &g;
    return BBM_OK;
}
