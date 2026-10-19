/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"
#include "inventory.h"

/* Heap storage avoids copying 128 KiB snapshots onto the game's call stack. */
struct mh_inventory_state {
    mh_inventory_snapshot cached, scratch;
    bbm_inventory_summary current, last_sent;
    uint64_t next_refresh;
    int has_sample;
    bbm_status status;
};
static struct mh_inventory_state *state(modhost*h){
    if(!h->inventory)h->inventory=calloc(1,sizeof *h->inventory);
    return h->inventory;
}
static int read_adapter(void*u,uintptr_t at,void*out,size_t n){return mh_read_safe(u,(void*)at,out,n);}
static void refresh(modhost*h,struct mh_inventory_state*s,uint64_t now){
    if(s->has_sample&&now<s->next_refresh)return;
    s->has_sample=1;s->next_refresh=now>UINT64_MAX-250?UINT64_MAX:now+250;
    bbm_inventory_summary next={0};next.size=sizeof next;next.revision=s->current.revision;
    bbm_player_snapshot player;
    bbm_status rc=mh_gameplay_read(h,&player);next.generation=player.generation;
    if(rc==BBM_OK){
        if(h->cfg.eboot_size<0x553e998||mh_inventory_profile(h->cfg.eboot_base,read_adapter,h))rc=BBM_ERR_UNSUPPORTED;
        else if(mh_inventory_read(h->cfg.eboot_base,player.actor,read_adapter,h,&s->scratch))rc=BBM_ERR_NOTFOUND;
        else{
            next.flags=BBM_INVENTORY_AVAILABLE;next.actor=s->scratch.actor;next.character=s->scratch.character;
            next.slot_count=s->scratch.slot_count;next.item_count=s->scratch.count;
        }
    }
    int changed=memcmp(&next,&s->current,sizeof next)!=0;
    if(!changed&&rc==BBM_OK)changed=memcmp(s->scratch.entries,s->cached.entries,next.item_count*sizeof s->scratch.entries[0])!=0;
    if(changed){
        if(++next.revision==0)++next.revision;
        s->current=next;
        if(rc==BBM_OK)memcpy(&s->cached,&s->scratch,sizeof s->cached);
        else memset(&s->cached,0,sizeof s->cached);
    }
    s->status=rc;
}
static bbm_status page(bbm_handle self,uint64_t revision,uint32_t offset,uint32_t limit,bbm_inventory_page*out){
    if(!self||!out)return BBM_ERR_ARG;
    memset(out,0,sizeof *out);
    if(!limit||limit>BBM_INVENTORY_PAGE_SIZE||offset>BBM_INVENTORY_MAX_ITEMS)return BBM_ERR_ARG;
    modhost*h=self->host;plat_mutex_lock(h->run_lock);
    struct mh_inventory_state*s=state(h);bbm_status rc=BBM_ERR_NOMEM;
    if(!s)goto done;
    refresh(h,s,plat_now_ms());out->snapshot=s->current;out->offset=offset;
    if(revision&&revision!=s->current.revision){rc=BBM_ERR_STATE;goto done;}
    rc=s->status;if(rc!=BBM_OK)goto done;
    if(offset>s->current.item_count){rc=BBM_ERR_ARG;goto done;}
    uint32_t n=s->current.item_count-offset;if(n>limit)n=limit;
    out->count=n;
    for(uint32_t i=0;i<n;++i){const mh_inventory_entry*e=&s->cached.entries[offset+i];
        out->entries[i]=(bbm_inventory_entry){e->slot,e->reference,e->encoded_item_id,e->quantity,e->metadata_04,e->metadata_0c,e->flags,0};}
    rc=BBM_OK;
done:
    plat_mutex_unlock(h->run_lock);return rc;
}
void mh_inventory_tick(modhost*h){
    int interested=0;
    for(size_t i=0;i<h->nmods&&!interested;++i){struct bbm_mod*m=h->mods[i].inst;
        interested=m&&!m->bypassed&&!m->faulted&&h->mods[i].state==MODHOST_ACTIVE&&(m->events&BBM_EV_INVENTORY_CHANGED);}
    if(!interested)return;
    struct mh_inventory_state*s=state(h);if(!s)return;
    refresh(h,s,plat_now_ms());
    if(memcmp(&s->last_sent,&s->current,sizeof s->current)){
        bbm_inventory_change change={s->last_sent,s->current};s->last_sent=s->current;
        bbm_event ev={BBM_EV_INVENTORY_CHANGED,0,change.current.revision,change.current.item_count,&change,sizeof change};
        mh_dispatch_event(h,&ev);
    }
}
void mh_inventory_stop(modhost*h){free(h->inventory);h->inventory=NULL;}
void mh_inventory_fill(bbm_host*api){api->inventory_page=page;}
