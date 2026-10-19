/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#define GUEST __attribute__((sysv_abi))
typedef uintptr_t (GUEST *getter_fn)(uintptr_t);
typedef void (GUEST *construct_fn)(uint32_t*,uintptr_t,uint32_t*);
typedef void (GUEST *award_fn)(uintptr_t,void*);
typedef void (GUEST *release_fn)(uint32_t*);
typedef struct request {
    uint32_t state; /* release/acquire publishes all remaining fields */
    struct bbm_mod *owner; /* host-only; never dereferenced by native code */
    uint64_t id,generation,expires;
    uintptr_t actor,character;
    uint32_t encoded,quantity;
} request;
static struct {
    struct bbm_mod owner;mh_entry entry;mh_hook *hook;void *original;
    uintptr_t base,caller,actor;uint64_t generation,next_id;
    uint32_t enabled,used,executing;
    request q[32];
} actions;
static uint32_t state(request*r){return __atomic_load_n(&r->state,__ATOMIC_ACQUIRE);}
static int read_at(uintptr_t a,void*out,size_t n){return a<0x10000||a>UINTPTR_MAX-n||plat_read_safe((void*)a,out,n);}
static int ptr(uintptr_t a,uintptr_t*out){return read_at(a,out,8)||*out<0x10000||*out>UINTPTR_MAX-0x10000||(*out&7);}
/* Read the first (active) cap of each bounded repository slot. No scan of
 * address space, no retained table pointers, no native PARAM lookup calls. */
static int catalog_present(uint32_t encoded){
    uintptr_t repo;if(ptr(actions.base+0x5540340,&repo))return 0;
    const char*type=(encoded>>28)==0?"EQUIP_PARAM_WEAPON_ST":(encoded>>28)==1?"EQUIP_PARAM_PROTECTOR_ST":(encoded>>28)==2?"EQUIP_PARAM_ACCESSORY_ST":"EQUIP_PARAM_GOODS_ST";
    uint32_t wanted=encoded&0x0fffffff;
    for(size_t slot=0;slot<63;slot++){
        uintptr_t at=repo+0x70+slot*0x48,cap,holder,file;uint32_t count;
        if(read_at(at,&count,4)||!count||count>8||ptr(at+8,&cap)||ptr(cap+0x70,&holder)||ptr(holder+0x70,&file))continue;
        unsigned char header[0x40];if(read_at(file,header,sizeof header)||memcmp(header+0xc,type,strlen(type)+1))continue;
        uint16_t rows;memcpy(&rows,header+0xa,2);if(!rows)return 0;
        unsigned format=header[0x2d],flags=header[0x2e];
        size_t start=format==2?0x34:0x40,stride=format>3&&(flags&2)?0x18:0xc;
        /* Current local tables use the wide (format 4, flag bit 1) layout. */
        if(format!=4||!(flags&2))return 0;
        uint32_t lo=0,hi=rows;
        while(lo<hi){uint32_t mid=lo+(hi-lo)/2,id;uintptr_t record=file+start+(size_t)mid*stride;
            if(read_at(record,&id,4))return 0;
            if(id<wanted){lo=mid+1;continue;}if(id>wanted){hi=mid;continue;}
            uint64_t offset=0;unsigned char byte;
            if(read_at(record+(stride==0x18?8:4),&offset,stride==0x18?8:4)||offset<start+(size_t)rows*stride||offset>64*1024*1024||file>UINTPTR_MAX-offset||read_at(file+(uintptr_t)offset,&byte,1))return 0;
            uintptr_t check;return !ptr(actions.base+0x5540340,&check)&&check==repo&&!ptr(at+8,&check)&&check==cap&&!ptr(cap+0x70,&check)&&check==holder&&!ptr(holder+0x70,&check)&&check==file;
        }
        return 0;
    }
    return 0;
}
static void cancel(request*r){uint32_t want=BBM_ITEM_PENDING;__atomic_compare_exchange_n(&r->state,&want,BBM_ITEM_CANCELLED,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED);}
static uintptr_t GUEST detour(uintptr_t actor){
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    uintptr_t character=((getter_fn)actions.original)(actor);
    if(caller!=actions.caller||!__atomic_load_n(&actions.enabled,__ATOMIC_ACQUIRE))return character;
    uint64_t gen=__atomic_load_n(&actions.generation,__ATOMIC_ACQUIRE);
    if(!gen||actor!=__atomic_load_n(&actions.actor,__ATOMIC_ACQUIRE))return character;
    uint32_t expected=0;
    /* Recursion/concurrent guest callbacks must not execute another award. */
    if(!__atomic_compare_exchange_n(&actions.executing,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return character;
    uintptr_t world,current,data,modules,component,owner,map,items;int32_t hp;
    if(ptr(actions.base+0x553e878,&world)||ptr(world+0x60,&current)||current!=actor||
       ptr(actor+0x3c0,&data)||data!=character||ptr(actions.base+0x553b130,&data)||ptr(data+8,&current)||current!=character||
       ptr(actor+0x3b0,&modules)||ptr(modules+0x20,&component)||ptr(component+8,&owner)||owner!=actor||read_at(component+0xf8,&hp,4)||hp<=0||
       ptr(actions.base+0x553d6e0,&map)||ptr(actions.base+0x553e990,&items))goto done;
    for(size_t i=0;i<32;i++){
        request*r=&actions.q[i];expected=BBM_ITEM_PENDING;
        if(!__atomic_compare_exchange_n(&r->state,&expected,BBM_ITEM_RUNNING,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED))continue;
        /* Claim before reading fields: cancellation may recycle pending slots. */
        if(r->generation!=gen||r->actor!=actor||r->character!=character||plat_now_ms()>r->expires||
           !__atomic_load_n(&actions.enabled,__ATOMIC_ACQUIRE)||gen!=__atomic_load_n(&actions.generation,__ATOMIC_ACQUIRE)){
            __atomic_store_n(&r->state,BBM_ITEM_CANCELLED,__ATOMIC_RELEASE);break;
        }
        /* Native owns any inventory references retained by award. This local
         * temporary is constructed and released on this same guest callback. */
        if(!catalog_present(r->encoded)){
            __atomic_store_n(&r->state,BBM_ITEM_FAILED,__ATOMIC_RELEASE);break;
        }
        uint32_t batch[5]={1,0,r->encoded,r->quantity,UINT32_MAX};
        ((construct_fn)(actions.base+0x1a86ff0))(&batch[1],items,&batch[2]);
        uint32_t result=BBM_ITEM_FAILED;
        if(batch[1]&&(batch[1]&0xffffff)!=0xffffff){
            ((award_fn)(actions.base+0x17d89f0))(map,batch);
            result=BBM_ITEM_SUBMITTED;
            ((release_fn)(actions.base+0x1a8bc00))(&batch[1]);
        }
        __atomic_store_n(&r->state,result,__ATOMIC_RELEASE);break;
    }
done:
    __atomic_store_n(&actions.executing,0,__ATOMIC_RELEASE);return character;
}
void mh_item_actions_start(modhost*h){
    if(!h->cfg.native_observation||h->cfg.safe_mode||actions.used)return;
    if(!h->cfg.eboot_base||h->cfg.eboot_size<0x5540348||h->cfg.eboot_base>UINTPTR_MAX-0x5540348)return;
    static const struct{uintptr_t rva;size_t n;unsigned char b[28];}checks[]={
        {0x1f1e3ec,10,{0x4c,0x8d,0x3d,0x4d,0x1f,0x62,0x03,0x49,0x8b,0x07}},
        {0x1f1e42e,7,{0x48,0x8b,0x80,0x50,0x01,0x00,0x00}},
        {0x1f1e440,8,{0x48,0x8b,0x40,0x70,0x4c,0x8b,0x40,0x70}},
        {0x18fd960,16,{0x48,0x8b,0x87,0xc0,3,0,0,0xc3,0x0f,0x1f,0x84,0,0,0,0,0}},
        {0x18f7891,9,{0x49,0x8b,6,0xff,0x90,0xc8,1,0,0}},
        {0x17d89f0,15,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81}},
        {0x17d8a7c,18,{0x41,0x8b,0x47,8,0x48,0x8d,0x8d,0xc4,0xfe,0xff,0xff,0x89,0x41,8,0x49,0x8b,7,0x48}},
        {0x1a86ff0,15,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83}},
        {0x1a8714b,28,{0x89,0xc1,0x81,0xe1,0xff,0xff,0xff,0,0x81,0xc9,0,0,0,0xb0,0x3d,0xff,0xff,0x7f,0,0x41,0xbc,0xff,0xff,0xff,0xb0,0x44,0x0f,0x42}},
        {0x1a8bc00,15,{0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x89,0xfb,0x8b,0x33,0x85,0xf6,0x0f}}
    };
    for(size_t i=0;i<sizeof checks/sizeof checks[0];i++){unsigned char b[28];if(mh_read_safe(h,(void*)(h->cfg.eboot_base+checks[i].rva),b,checks[i].n)||memcmp(b,checks[i].b,checks[i].n)){
        mh_log(h,"bbmod.items",BBM_LOG_WARN,"native item queue unavailable: signature at %llx differs",(unsigned long long)checks[i].rva);return;}}
    actions.owner.host=h;actions.owner.entry=&actions.entry;mh_strlcpy(actions.entry.m.id,"bbmod.items",sizeof actions.entry.m.id);
    actions.base=h->cfg.eboot_base;actions.caller=actions.base+0x18f789a;
    bbm_status rc=mh_hook_inline(&actions.owner,actions.base+0x18fd960,(void*)detour,&actions.original,&actions.hook);
    if(rc!=BBM_OK){mh_log(h,"bbmod.items",BBM_LOG_WARN,"native item queue hook failed (%d)",rc);return;}
    actions.used=1;__atomic_store_n(&actions.enabled,1,__ATOMIC_RELEASE);
    mh_log(h,"bbmod.items",BBM_LOG_INFO,"native item queue installed (verified player update only)");
}
void mh_item_actions_tick(modhost*h){
    if(actions.owner.host!=h||!actions.hook)return;
    __atomic_store_n(&actions.generation,0,__ATOMIC_RELEASE);
    __atomic_store_n(&actions.actor,h->player_actor,__ATOMIC_RELEASE);
    uint64_t gen=h->player_available&&!h->session.in_session?h->player_generation:0;
    __atomic_store_n(&actions.generation,gen,__ATOMIC_RELEASE);
    for(size_t i=0;i<32;i++){request*r=&actions.q[i];uint32_t s=state(r);
        if(s==BBM_ITEM_PENDING&&(r->generation!=gen||plat_now_ms()>r->expires))cancel(r);
        if(!r->owner&&state(r)>=BBM_ITEM_SUBMITTED)__atomic_store_n(&r->state,0,__ATOMIC_RELEASE);
    }
}
void mh_item_actions_cancel(struct bbm_mod*m){
    if(actions.owner.host!=m->host)return;
    for(size_t i=0;i<32;i++){request*r=&actions.q[i];if(r->owner==m){cancel(r);r->owner=NULL;}}
}
void mh_item_actions_stop(modhost*h){
    if(actions.owner.host!=h||!actions.hook)return;
    __atomic_store_n(&actions.enabled,0,__ATOMIC_RELEASE);__atomic_store_n(&actions.generation,0,__ATOMIC_RELEASE);
    for(size_t i=0;i<32;i++)cancel(&actions.q[i]);
    void*retained=actions.hook->block;mh_hook_remove(h,actions.hook);actions.hook=NULL;
    for(size_t i=0;i<h->ndeferred;){if(h->deferred[i].p==retained)h->deferred[i]=h->deferred[--h->ndeferred];else i++;}
    actions.owner.host=NULL; /* Cached native calls retain only static state. */
}
static bbm_status give(bbm_handle self,uint32_t encoded,uint32_t quantity,uint64_t generation,uint64_t*out){
    if(!self||!out)return BBM_ERR_ARG;*out=0;
    uint32_t kind=encoded&0xf0000000,id=encoded&0x0fffffff;
    if(!id||id==0x0fffffff||(kind!=0&&kind!=0x10000000&&kind!=0x20000000&&kind!=0x40000000)||
       !quantity||quantity>99||(kind!=0x40000000&&quantity!=1)||((kind==0x40000000||kind==0x20000000)&&id>=0x7fffff))return BBM_ERR_ARG;
    modhost*h=self->host;plat_mutex_lock(h->run_lock);bbm_status rc=BBM_ERR_UNSUPPORTED;
    if(actions.owner.host!=h||!actions.hook||!__atomic_load_n(&actions.enabled,__ATOMIC_ACQUIRE))goto done;
    rc=BBM_ERR_STATE;if(self->faulted||self->bypassed||h->session.in_session||h->cfg.safe_mode)goto done;
    bbm_player_snapshot player;if(mh_gameplay_read(h,&player)!=BBM_OK||player.health<=0||generation!=player.generation)goto done;
    uintptr_t character;if(ptr(player.actor+0x3c0,&character))goto done;
    size_t mine=0;request*slot=NULL;
    for(size_t i=0;i<32;i++){if(actions.q[i].owner==self)mine++;if(!state(&actions.q[i])&&!slot)slot=&actions.q[i];}
    rc=BBM_ERR_BUDGET;if(mine>=8||!slot)goto done;
    uint64_t id_request=++actions.next_id;if(!id_request)id_request=++actions.next_id;
    slot->owner=self;slot->id=id_request;slot->generation=generation;slot->expires=plat_now_ms()+5000;
    slot->actor=player.actor;slot->character=character;slot->encoded=encoded;slot->quantity=quantity;
    __atomic_store_n(&slot->state,BBM_ITEM_PENDING,__ATOMIC_RELEASE);*out=id_request;rc=BBM_OK;
done:plat_mutex_unlock(h->run_lock);return rc;
}
static bbm_status status(bbm_handle self,uint64_t id,bbm_item_give_result*out){
    if(!self||!out||!id)return BBM_ERR_ARG;memset(out,0,sizeof *out);
    modhost*h=self->host;plat_mutex_lock(h->run_lock);bbm_status rc=BBM_ERR_NOTFOUND;
    if(actions.owner.host==h)for(size_t i=0;i<32;i++){request*r=&actions.q[i];if(r->owner!=self||r->id!=id)continue;
        uint32_t s=state(r);if(s==BBM_ITEM_PENDING&&plat_now_ms()>r->expires){cancel(r);s=state(r);}
        *out=(bbm_item_give_result){sizeof *out,s,id,r->generation,r->encoded,r->quantity};rc=BBM_OK;
        if(s>=BBM_ITEM_SUBMITTED){r->owner=NULL;__atomic_store_n(&r->state,0,__ATOMIC_RELEASE);}break;
    }
    plat_mutex_unlock(h->run_lock);return rc;
}
#else
void mh_item_actions_start(modhost*h){(void)h;}
void mh_item_actions_tick(modhost*h){(void)h;}
void mh_item_actions_stop(modhost*h){(void)h;}
void mh_item_actions_cancel(struct bbm_mod*m){(void)m;}
static bbm_status give(bbm_handle s,uint32_t i,uint32_t q,uint64_t g,uint64_t*o){(void)s;(void)i;(void)q;(void)g;(void)o;return BBM_ERR_UNSUPPORTED;}
static bbm_status status(bbm_handle s,uint64_t i,bbm_item_give_result*o){(void)s;(void)i;(void)o;return BBM_ERR_UNSUPPORTED;}
#endif
void mh_item_actions_fill(bbm_host*a){a->item_give=give;a->item_give_status=status;}
