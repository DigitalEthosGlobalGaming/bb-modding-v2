/* Applies every <Metadata Name="..."> in [test] xml one at a time, checks it
 * patched something, releases it, and reports through ui_notify. */
#include <stdio.h>
#include <string.h>
#include "bbmod.h"

typedef struct bbm_ctx { int dummy; } ctx_t;
static ctx_t g_ctx;
static const bbm_host *H;
static bbm_handle S;
static char xml[600], text[1 << 20];

static bbm_status on_load(bbm_ctx *c) {
    (void)c;
    if (H->config_get(S, "test.xml", xml, sizeof xml)) return BBM_ERR_ARG;
    FILE *f = fopen(xml, "rb");
    if (!f) return BBM_ERR_NOTFOUND;
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    int ok = 0, bad = 0;
    char msg[700];
    for (char *p = text; (p = strstr(p, "<Metadata ")); p++) {
        char *q = strstr(p, " Name=\"");
        if (!q) break;
        q += 7;
        char *e = strchr(q, '"');
        char name[200];
        size_t len = (size_t)(e - q) < sizeof name - 1 ? (size_t)(e - q) : sizeof name - 1;
        memcpy(name, q, len);
        name[len] = 0;
        /* XML escapes in names (e.g. &apos;) are kept as written: the host matches the raw attribute. */
        bbm_id id;
        if (H->patch_xml(S, xml, name, &id) == BBM_OK) {
            ok++;
            H->release(S, id);
        } else {
            bad++;
            snprintf(msg, sizeof msg, "FAILED %s: %s", name, H->last_error(S));
            H->ui_notify(S, msg);
        }
    }
    snprintf(msg, sizeof msg, "xml ok=%d bad=%d", ok, bad);
    H->ui_notify(S, msg);
    return BBM_OK;
}
static void noop(bbm_ctx *c) { (void)c; }

BBM_EXPORT bbm_status bbmod_entry(const bbm_host *host, bbm_handle self, bbm_mod_api *api, bbm_ctx **out) {
    H = host; S = self;
    api->size = sizeof *api; api->abi_major = BBM_ABI_MAJOR; api->abi_minor = BBM_ABI_MINOR;
    api->on_load = on_load; api->on_unload = noop; api->destroy = noop;
    *out = &g_ctx;
    return BBM_OK;
}
