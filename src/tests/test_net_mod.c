/* ModNet test mod: opens channel "chat", notifies on receive, sends one
 * message (or a flood) on its first frame once it has peers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bbmod.h"

typedef struct bbm_ctx { int dummy; } ctx_t;
static ctx_t g_ctx;
static const bbm_host *H;
static bbm_handle S;
static bbm_id chan;
static int sent, flood;
static char name[32] = "?";

static void on_msg(void *u, uint64_t peer, const void *d, size_t n) {
    (void)u; (void)peer;
    char b[160];
    snprintf(b, sizeof b, "rx:%.*s", (int)(n > 100 ? 100 : n), (const char *)d);
    H->ui_notify(S, b);
}

static void on_frame(void *u) {
    (void)u;
    bbm_session_info si;
    H->session_info(S, &si);
    if (!si.in_session || si.peer_count == 0 || sent) return;
    sent = 1;
    char b[96];
    if (flood > 0) {
        static char big[1200];
        memset(big, 'x', sizeof big);
        int ok = 0, bud = 0;
        for (int i = 0; i < flood; i++) {
            bbm_status rc = H->net_broadcast(S, chan, big, sizeof big);
            if (rc == BBM_OK) ok++; else if (rc == BBM_ERR_BUDGET) bud++;
        }
        snprintf(b, sizeof b, "flood ok=%d budget=%d", ok, bud);
        H->ui_notify(S, b);
    } else {
        snprintf(b, sizeof b, "hi-%s", name);
        H->net_broadcast(S, chan, b, strlen(b));
    }
}

static bbm_status on_load(bbm_ctx *c) {
    (void)c;
    char buf[32];
    if (H->config_get(S, "test.name", name, sizeof name)) strcpy(name, "?");
    flood = H->config_get(S, "test.flood", buf, sizeof buf) ? 0 : atoi(buf);
    bbm_id id;
    if (H->net_channel_open(S, "chat", BBM_NET_RELIABLE | BBM_NET_ORDERED, &chan)) return BBM_ERR;
    if (H->net_on_message(S, chan, on_msg, NULL)) return BBM_ERR;
    if (H->on_frame(S, on_frame, NULL, &id)) return BBM_ERR;
    return H->subscribe(S, BBM_EV_MODSET_MISMATCH);
}
static void on_event(bbm_ctx *c, const bbm_event *ev) {
    (void)c;
    if (ev->type == BBM_EV_MODSET_MISMATCH) H->ui_notify(S, "mismatch");
}
static void noop(bbm_ctx *c) { (void)c; }

BBM_EXPORT bbm_status bbmod_entry(const bbm_host *host, bbm_handle self, bbm_mod_api *api, bbm_ctx **out) {
    H = host; S = self;
    api->size = sizeof *api; api->abi_major = BBM_ABI_MAJOR; api->abi_minor = BBM_ABI_MINOR;
    api->on_load = on_load; api->on_unload = noop; api->destroy = noop; api->on_event = on_event;
    *out = &g_ctx;
    return BBM_OK;
}
