/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"
#include "equipment.h"

static int read_adapter(void *user, uintptr_t address, void *out, size_t size) {
    return mh_read_safe(user,(const void *)address,out,size);
}
static bbm_status read_snapshot(modhost *h, bbm_equipment_snapshot *out) {
    memset(out,0,sizeof *out); out->size=sizeof *out;
    out->selected_even=out->selected_odd=UINT32_MAX;
    if (!h->equipment_profile_state)
        h->equipment_profile_state=h->cfg.eboot_size>=0x553e880 &&
            !mh_equipment_profile(h->cfg.eboot_base,read_adapter,h) ? 1:-1;
    if(h->equipment_profile_state<0) return BBM_ERR_UNSUPPORTED;
    bbm_player_snapshot player;
    bbm_status rc=mh_gameplay_read(h,&player);
    out->generation=player.generation;
    if(rc!=BBM_OK) return rc;
    mh_equipment_snapshot raw;
    if(mh_equipment_read(h->cfg.eboot_base,player.actor,read_adapter,h,&raw)) return BBM_ERR_NOTFOUND;
    out->flags=1; out->actor=raw.actor; out->character=raw.character; out->mode=raw.mode;
    memcpy(out->selections,raw.selections,sizeof raw.selections);
    memcpy(out->runtime_references,raw.runtime_references,sizeof raw.runtime_references);
    memcpy(out->saved_item_ids,raw.saved_item_ids,sizeof raw.saved_item_ids);
    out->selected_even=raw.selected_even; out->selected_odd=raw.selected_odd;
    return BBM_OK;
}
static bbm_status snapshot(bbm_handle self, bbm_equipment_snapshot *out) {
    if(!self || !out) return BBM_ERR_ARG;
    modhost *h=self->host; plat_mutex_lock(h->run_lock);
    bbm_status rc=read_snapshot(h,out); plat_mutex_unlock(h->run_lock); return rc;
}
void mh_equipment_tick(modhost *h) {
    bbm_equipment_snapshot current;
    read_snapshot(h,&current);
    if(memcmp(&h->equipment_last,&current,sizeof current)) {
        bbm_equipment_change change={h->equipment_last,current};
        h->equipment_last=current;
        bbm_event ev={BBM_EV_EQUIPMENT_CHANGED,0,current.generation,0,&change,sizeof change};
        mh_dispatch_event(h,&ev);
    }
}
void mh_equipment_fill(bbm_host *api) { api->equipment_snapshot=snapshot; }
