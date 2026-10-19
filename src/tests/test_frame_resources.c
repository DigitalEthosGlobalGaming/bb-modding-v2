/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"
#include <assert.h>

typedef struct {
    struct bbm_mod *mod;
    bbm_id earlier, removed, added;
    int first, second, stale, newcomer;
} fixture;
static void newcomer(void *u) { ((fixture *)u)->newcomer++; }
static void stale(void *u) { ((fixture *)u)->stale++; }
static void second(void *u) { ((fixture *)u)->second++; }
static void first(void *u) {
    fixture *f = u;
    if (f->first++ == 0) {
        modhost *h = f->mod->host;
        assert(!h->api.release(f->mod, f->earlier));
        assert(!h->api.release(f->mod, f->removed));
        assert(!h->api.on_frame(f->mod, newcomer, f, &f->added));
    }
}
static void panel(void *u) { (void)u; }
int main(void) {
    modhost_config cfg = {0};
    cfg.cache_dir = "frame-resource-test-cache";
    cfg.mods_dir = "frame-resource-no-mods";
    modhost *h = modhost_create(&cfg); assert(h);
    h->mods = calloc(1, sizeof *h->mods); h->nmods = h->capmods = 1;
    struct bbm_mod mod = {.host=h, .entry=&h->mods[0]};
    h->mods[0].inst = &mod; h->mods[0].state = MODHOST_ACTIVE;
    fixture f = {.mod=&mod}; bbm_id a, b;
    assert(!h->api.ui_panel(&mod, "earlier", panel, &f, &f.earlier));
    assert(!h->api.on_frame(&mod, first, &f, &a));
    assert(!h->api.on_frame(&mod, second, &f, &b));
    assert(!h->api.on_frame(&mod, stale, &f, &f.removed));
    modhost_frame(h);
    assert(f.first==1 && f.second==1 && f.stale==0 && f.newcomer==0);
    modhost_frame(h);
    assert(f.first==2 && f.second==2 && f.stale==0 && f.newcomer==1);
    mh_res_release_all(&mod);
    for (int i=0; i<17; ++i) assert(!h->api.on_frame(&mod, second, &f, &b));
    modhost_frame(h); assert(f.second==19);
    mh_res_release_all(&mod); h->mods[0].inst=NULL;
    modhost_destroy(h);
    puts("frame resources: removal cannot skip neighbors; removed callbacks stop; new callbacks deferred: OK");
    return 0;
}
