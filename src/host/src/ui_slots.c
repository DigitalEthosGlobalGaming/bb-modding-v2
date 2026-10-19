/* SPDX-License-Identifier: GPL-2.0-or-later
 * Shared overlay insertion points. Each contribution remains mod-owned;
 * sorted copies prevent mutation of the registry from invalidating iteration. */
#include "internal.h"
#include <ctype.h>

#define UI_SLOT_LIMIT 256
typedef struct contribution {
    char slot[MH_NAME], key[MH_NAME];
    int32_t order;
    bbm_panel_cb draw;
    void *user;
} contribution;
typedef struct item {
    struct bbm_mod *owner;
    bbm_id id;
    contribution value;
} item;

static int valid_name(const char *s) {
    if (!s || !*s || strlen(s) >= MH_NAME) return 0;
    for (; *s; ++s)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
              (*s >= '0' && *s <= '9') || *s == '_' || *s == '.' || *s == '-')) return 0;
    return 1;
}
static bbm_status contribute_locked(bbm_handle m, const char *slot, const char *key,
                            int32_t order, bbm_panel_cb draw, void *user, bbm_id *out) {
    if (!m || !m->entry || !draw || !out || !valid_name(slot) || !valid_name(key)) return BBM_ERR_ARG;
    if (m->faulted || m->bypassed) return BBM_ERR_STATE;
    modhost *h = m->host;
    if (!h->cfg.ui.begin_panel || !h->cfg.ui.end_panel ||
        !h->cfg.ui.push_scope || !h->cfg.ui.pop_scope) return BBM_ERR_UNSUPPORTED;
    size_t count = 0;
    for (size_t i=0; i<h->nmods; ++i) {
        struct bbm_mod *other = h->mods[i].inst;
        if (!other) continue;
        for (size_t j=0; j<other->nres; ++j) if (other->res[j].kind == MH_RES_UI_CONTRIBUTION) ++count;
    }
    if (count >= UI_SLOT_LIMIT) return BBM_ERR_STATE;
    for (size_t i=0; i<m->nres; ++i) if (m->res[i].kind == MH_RES_UI_CONTRIBUTION) {
        contribution *p = m->res[i].p;
        if (!strcmp(p->slot,slot) && !strcmp(p->key,key)) return BBM_ERR_CONFLICT;
    }
    contribution *p = calloc(1,sizeof *p);
    if (!p) return BBM_ERR_NOMEM;
    mh_strlcpy(p->slot,slot,sizeof p->slot); mh_strlcpy(p->key,key,sizeof p->key);
    p->order=order; p->draw=draw; p->user=user;
    bbm_id id=mh_res_add(m,MH_RES_UI_CONTRIBUTION,p);
    if (!id) { free(p); return BBM_ERR_NOMEM; }
    *out=id; return BBM_OK;
}
static bbm_status contribute(bbm_handle m, const char *slot, const char *key,
                            int32_t order, bbm_panel_cb draw, void *user, bbm_id *out) {
    if (!m || !m->host) return BBM_ERR_ARG;
    plat_mutex_lock(m->host->run_lock);
    bbm_status result=contribute_locked(m,slot,key,order,draw,user,out);
    plat_mutex_unlock(m->host->run_lock);
    return result;
}
static int compare(const void *aa,const void *bb) {
    const item *a=aa,*b=bb;
    int n=strcmp(a->value.slot,b->value.slot); if (n) return n;
    if (a->value.order != b->value.order) return a->value.order < b->value.order ? -1 : 1;
    n=strcmp(a->owner->entry->m.id,b->owner->entry->m.id); if (n) return n;
    return strcmp(a->value.key,b->value.key);
}
static int live(const item *x) {
    if (x->owner->bypassed || x->owner->faulted || x->owner->entry->state != MODHOST_ACTIVE) return 0;
    for (size_t i=0; i<x->owner->nres; ++i)
        if (x->owner->res[i].id == x->id && x->owner->res[i].kind == MH_RES_UI_CONTRIBUTION) return 1;
    return 0;
}
void mh_ui_slots_draw(modhost *h) {
    modhost_ui_backend *ui=&h->cfg.ui;
    if (!ui->begin_panel || !ui->end_panel || !ui->push_scope || !ui->pop_scope) return;
    item *items=calloc(UI_SLOT_LIMIT,sizeof *items);
    if (!items) return;
    size_t count=0;
    for (size_t i=0; i<h->nmods; ++i) {
        struct bbm_mod *m=h->mods[i].inst;
        if (!m || m->bypassed || h->mods[i].state != MODHOST_ACTIVE) continue;
        for (size_t j=0; j<m->nres && count<UI_SLOT_LIMIT; ++j) if (m->res[j].kind == MH_RES_UI_CONTRIBUTION) {
            items[count++]=(item){m,m->res[j].id,*(contribution*)m->res[j].p};
        }
    }
    qsort(items,count,sizeof *items,compare);
    for (size_t i=0; i<count;) {
        size_t end=i+1;
        while (end<count && !strcmp(items[i].value.slot,items[end].value.slot)) ++end;
        int visible=ui->begin_panel(ui->user,items[i].value.slot,"bbmod.shared-slot");
        if (visible) for (size_t j=i; j<end; ++j) {
            item *x=&items[j]; if (!live(x)) continue;
            ui->push_scope(ui->user,x->owner->entry->m.id,x->value.key);
            h->current_panel_mod=x->owner;
            mh_call call={.kind=MH_CALL_VOID1,.f=(void(*)(void))x->value.draw,.a=x->value.user};
            mh_call_mod(x->owner,&call);
            mh_ui_tables_cleanup(x->owner);
            h->current_panel_mod=NULL;
            ui->pop_scope(ui->user);
        }
        ui->end_panel(ui->user); i=end;
    }
    free(items);
}
void mh_ui_slots_fill_api(bbm_host *api) { api->ui_contribute=contribute; }
