/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"
#include "characters.h"
struct mh_characters_state {
    mh_character_sample cached,scratch;
    bbm_character_summary current,last_sent;
    uint64_t next_refresh,next_token,last_time;
    bbm_status status;
    int sampled;
};
static int adapter(void*u,uintptr_t at,void*out,size_t n){return mh_read_safe(u,(void*)at,out,n);}
static struct mh_characters_state*state(modhost*h){
    if(!h->characters)h->characters=calloc(1,sizeof *h->characters);
    return h->characters;
}
static void refresh(modhost*h,struct mh_characters_state*s,uint64_t now){
    if(s->sampled&&now<s->next_refresh)return;
    int gap=s->sampled&&now-s->last_time>2000;
    s->sampled=1;s->last_time=now;s->next_refresh=now>UINT64_MAX-250?UINT64_MAX:now+250;
    bbm_status rc=BBM_ERR_UNSUPPORTED;
    if(h->cfg.eboot_size>=0x553e880&&mh_characters_profile(h->cfg.eboot_base,adapter,h))
        rc=mh_characters_read(h->cfg.eboot_base,adapter,h,&s->scratch);
    bbm_character_summary next={sizeof next,0,s->current.revision,s->current.epoch,0,0};
    if(rc==BBM_OK){
        next.flags=1;next.count=s->scratch.count;
        int same=s->current.flags&&!gap&&s->cached.world==s->scratch.world&&s->cached.player==s->scratch.player;
        if(!same)++next.epoch;
        for(uint32_t i=0;i<next.count;i++){
            mh_character*n=&s->scratch.entries[i];
            if(same)for(uint32_t j=0;j<s->cached.count;j++){
                mh_character*p=&s->cached.entries[j];
                if(n->actor==p->actor&&n->data==p->data&&n->value.handle==p->value.handle){n->value.token=p->value.token;break;}
            }
            if(!n->value.token){if(++s->next_token==0)++s->next_token;n->value.token=s->next_token;}
        }
    }else if(s->current.flags)++next.epoch;
    int changed=memcmp(&next,&s->current,sizeof next)!=0;
    if(!changed&&rc==BBM_OK)changed=memcmp(s->scratch.entries,s->cached.entries,next.count*sizeof(mh_character))!=0;
    if(changed){
        if(++next.revision==0)++next.revision;
        s->current=next;
        if(rc==BBM_OK)memcpy(&s->cached,&s->scratch,sizeof s->cached);
        else memset(&s->cached,0,sizeof s->cached);
    }
    s->status=rc;
}
static bbm_status page(bbm_handle self,uint64_t revision,uint32_t offset,uint32_t limit,bbm_character_page*out){
    if(!self||!out)return BBM_ERR_ARG;
    memset(out,0,sizeof *out);
    if(!limit||limit>BBM_CHARACTER_PAGE_SIZE||offset>BBM_CHARACTER_MAX)return BBM_ERR_ARG;
    modhost*h=self->host;plat_mutex_lock(h->run_lock);bbm_status rc=BBM_ERR_NOMEM;
    struct mh_characters_state*s=state(h);if(!s)goto done;
    refresh(h,s,plat_now_ms());out->snapshot=s->current;out->offset=offset;
    if(revision&&revision!=s->current.revision){rc=BBM_ERR_STATE;goto done;}
    rc=s->status;if(rc)goto done;
    if(offset>s->current.count){rc=BBM_ERR_ARG;goto done;}
    out->count=s->current.count-offset;if(out->count>limit)out->count=limit;
    for(uint32_t i=0;i<out->count;i++)out->entries[i]=s->cached.entries[offset+i].value;
done:plat_mutex_unlock(h->run_lock);return rc;
}
void mh_characters_tick(modhost*h){
    int interested=0;
    for(size_t i=0;i<h->nmods&&!interested;i++){struct bbm_mod*m=h->mods[i].inst;
        interested=m&&!m->bypassed&&!m->faulted&&h->mods[i].state==MODHOST_ACTIVE&&(m->events&BBM_EV_CHARACTERS_CHANGED);}
    if(!interested)return;
    struct mh_characters_state*s=state(h);if(!s)return;refresh(h,s,plat_now_ms());
    if(memcmp(&s->last_sent,&s->current,sizeof s->current)){
        bbm_character_summary copy=s->current;s->last_sent=copy;
        bbm_event ev={BBM_EV_CHARACTERS_CHANGED,0,copy.revision,copy.count,&copy,sizeof copy};mh_dispatch_event(h,&ev);
    }
}
void mh_characters_fill(bbm_host*a){a->characters_page=page;}
void mh_characters_stop(modhost*h){free(h->characters);h->characters=NULL;}
