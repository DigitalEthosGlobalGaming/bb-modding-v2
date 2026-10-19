/* SPDX-License-Identifier: GPL-2.0-or-later
 * Shared CUSA03173 1.09 player service. Layout evidence: JamesCicada/bb-tools,
 * 15fdfda3af45fbb0315ac626f36bbe8f083258ea (MIT); see docs/BB-TOOLS-LICENSE.txt.
 * Observations are samples, NOT damage callbacks. No executable game patch. */
#include "internal.h"
#include <limits.h>

typedef struct { uint32_t field, scale; int32_t priority; } player_modifier;
typedef struct { struct bbm_mod *owner; bbm_id id; player_modifier value; } claim;
static const struct { size_t offset; const char *hex; } fingerprints[] = {
    {0x18c2d00, "554889e54156534889fb4c8d3567bbc703"},
    {0x18c2d3d, "483958600f94c0"}, {0x18f78b1, "8b88f8000000"},
    {0x18f78db, "8b8034010000"}, {0x1900278, "498b96c00300008b8294000000"},
    {0x203a4a6, "8b4740c38b4748c38b4750c38b4758c38b4760c38b4768c38b4770c3"},
    {0x1a571a7, "41f6860002000080"}
};
static int nibble(char c) { return c <= '9' ? c-'0' : c-'a'+10; }
static int image_valid(modhost *h) {
    if (!h->cfg.eboot_base || h->cfg.eboot_size < 0x553e880) return 0;
    for (size_t i=0; i<sizeof fingerprints/sizeof fingerprints[0]; ++i) {
        uint8_t actual[32]; size_t n=strlen(fingerprints[i].hex)/2;
        if (h->cfg.eboot_base > UINTPTR_MAX-fingerprints[i].offset ||
            mh_read_safe(h,(void *)(h->cfg.eboot_base+fingerprints[i].offset),actual,n)) return 0;
        for (size_t j=0;j<n;++j)
            if (actual[j] != (uint8_t)((nibble(fingerprints[i].hex[j*2])<<4)|nibble(fingerprints[i].hex[j*2+1]))) return 0;
    }
    return 1;
}
static int pointer(modhost *h, uintptr_t base, size_t offset, uintptr_t *out) {
    return base > UINTPTR_MAX-offset || mh_read_safe(h,(void *)(base+offset),out,sizeof *out)
        || *out<0x10000 || (*out & 7);
}
static bbm_status unavailable(modhost *h, bbm_player_snapshot *out, bbm_status rc) {
    if (h->player_available) ++h->player_generation;
    h->player_available=0; h->player_actor=h->player_component=0;
    memset(out,0,sizeof *out); out->size=sizeof *out; out->generation=h->player_generation;
    return rc;
}
bbm_status mh_gameplay_read(modhost *h, bbm_player_snapshot *out) {
    if (!image_valid(h)) return unavailable(h,out,BBM_ERR_UNSUPPORTED);
    uintptr_t world,actor,modules,component,owner,again;
    if (pointer(h,h->cfg.eboot_base,0x553e878,&world) || pointer(h,world,0x60,&actor)
        || pointer(h,actor,0x3b0,&modules) || pointer(h,modules,0x20,&component)
        || pointer(h,component,8,&owner) || owner!=actor || component>UINTPTR_MAX-0x13c)
        return unavailable(h,out,BBM_ERR_NOTFOUND);
    int32_t data[17];
    if (mh_read_safe(h,(void *)(component+0xf8),data,sizeof data) ||
        pointer(h,world,0x60,&again) || again!=actor ||
        data[1]<1 || data[1]>100000 || data[0]<0 || data[0]>data[1] ||
        data[16]<1 || data[16]>10000 || data[15]<0 || data[15]>data[16])
        return unavailable(h,out,BBM_ERR_NOTFOUND);
    if (!h->player_available || actor!=h->player_actor || component!=h->player_component) ++h->player_generation;
    h->player_available=1; h->player_actor=actor; h->player_component=component;
    *out=(bbm_player_snapshot){sizeof *out,BBM_PLAYER_AVAILABLE,h->player_generation,actor,component,data[0],data[1],data[15],data[16]};
    return BBM_OK;
}
static bbm_status snapshot(bbm_handle self, bbm_player_snapshot *out) {
    if (!out) return BBM_ERR_ARG;
    modhost *h=self->host; plat_mutex_lock(h->run_lock);
    bbm_status rc=mh_gameplay_read(h,out); plat_mutex_unlock(h->run_lock); return rc;
}
static int compare_claim(const void *a, const void *b) {
    const claim *x=a,*y=b;
    if (x->value.priority!=y->value.priority) return x->value.priority<y->value.priority ? -1:1;
    int n=strcmp(x->owner->entry->m.id,y->owner->entry->m.id);
    if (n) return n;
    return x->id<y->id ? -1:x->id!=y->id;
}
static size_t claims(modhost *h, uint32_t field, claim *out) {
    size_t n=0;
    for (size_t i=0;i<h->nmods;++i) {
        struct bbm_mod *m=h->mods[i].inst;
        if (!m || m->faulted || m->bypassed || (field && h->mods[i].state!=MODHOST_ACTIVE)) continue;
        for (size_t j=0;j<m->nres;++j) {
            if (m->res[j].kind!=MH_RES_PLAYER_MODIFIER) continue;
            player_modifier *v=m->res[j].p;
            if (field && v->field!=field) continue;
            if (n>=64) return 65;
            if (out) out[n]=(claim){m,m->res[j].id,*v};
            ++n;
        }
    }
    return n;
}
static bbm_status modifier(bbm_handle self, uint32_t field, uint32_t scale, int32_t priority, bbm_id *out) {
    if (!out || (field!=BBM_RESOURCE_HEALTH && field!=BBM_RESOURCE_STAMINA) || scale>4000) return BBM_ERR_ARG;
    modhost *h=self->host; plat_mutex_lock(h->run_lock);
    bbm_status rc=BBM_OK;
    if (self->faulted || self->bypassed) rc=BBM_ERR_STATE;
    else if (claims(h,0,NULL)>=64) rc=BBM_ERR_BUDGET;
    else {
        player_modifier *p=malloc(sizeof *p);
        if (!p) rc=BBM_ERR_NOMEM;
        else { *p=(player_modifier){field,scale,priority}; *out=mh_res_add(self,MH_RES_PLAYER_MODIFIER,p);
            if (!*out) {free(p);rc=BBM_ERR_NOMEM;} }
    }
    plat_mutex_unlock(h->run_lock); return rc;
}
static bbm_status adjust(bbm_handle self,uint32_t field,uint64_t generation,int32_t expected,int32_t delta,int32_t *actual) {
    if (!actual || (field!=BBM_RESOURCE_HEALTH && field!=BBM_RESOURCE_STAMINA)) return BBM_ERR_ARG;
    *actual=0;
    modhost *h=self->host; plat_mutex_lock(h->run_lock);
    bbm_status rc=BBM_ERR_STATE;
    if (!h->gameplay_in_frame || h->gameplay_caller!=self || self->faulted || self->bypassed || h->session.in_session) goto done;
    bbm_player_snapshot before,again;
    rc=mh_gameplay_read(h,&before); if (rc!=BBM_OK) goto done;
    int32_t current=field==BBM_RESOURCE_HEALTH ? before.health:before.stamina;
    int32_t maximum=field==BBM_RESOURCE_HEALTH ? before.max_health:before.max_stamina;
    if (!before.health || generation!=before.generation || current!=expected) {rc=BBM_ERR_STATE;goto done;}
    claim list[64]; size_t n=claims(h,field,list);
    if(n>64){rc=BBM_ERR_BUDGET;goto done;}
    qsort(list,n,sizeof list[0],compare_claim);
    int64_t change=delta;
    for(size_t i=0;i<n;++i) {
        change=change*list[i].value.scale/1000;
        /* Bounded saturation prevents overflow across arbitrarily amplified chains. */
        if(change>INT32_MAX) change=INT32_MAX;
        if(change<INT32_MIN) change=INT32_MIN;
    }
    int64_t value=(int64_t)current+change;
    int32_t minimum=field==BBM_RESOURCE_HEALTH ? 1:0;
    if(value<minimum)value=minimum;
    if(value>maximum)value=maximum;
    int32_t replacement=(int32_t)value;
    rc=mh_gameplay_read(h,&again); if(rc!=BBM_OK)goto done;
    if(memcmp(&before,&again,sizeof before)) {rc=BBM_ERR_STATE;goto done;}
    /* Optimistic guard: game threads are not stopped. A game write can race this
     * operation; success reports our committed delta, not a guaranteed later value. */
    if(replacement!=current && mh_write_code(h,(void *)(before.component+(field==BBM_RESOURCE_HEALTH?0xf8:0x134)),&replacement,sizeof replacement)) {rc=BBM_ERR;goto done;}
    *actual=replacement-current; rc=BBM_OK;
done:
    plat_mutex_unlock(h->run_lock); return rc;
}
void mh_gameplay_tick(modhost *h) {
    bbm_player_snapshot current;
    mh_gameplay_read(h,&current);
    if(memcmp(&h->player_last,&current,sizeof current)) {
        bbm_player_change change={h->player_last,current};
        h->player_last=current; /* update before callbacks; callbacks may issue explicit writes */
        bbm_event ev={BBM_EV_PLAYER_CHANGED,0,current.generation,0,&change,sizeof change};
        mh_dispatch_event(h,&ev);
    }
}
void mh_gameplay_fill(bbm_host *api) {
    api->player_snapshot=snapshot; api->player_adjust=adjust; api->player_modifier=modifier;
}
