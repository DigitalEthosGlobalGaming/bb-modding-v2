/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"

/* Process-lifetime host bridge. Never dereferences host/mod/actor pointers from
 * the detour. Its one code allocation is retained on shutdown for cached calls. */
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#define GUEST __attribute__((sysv_abi))
typedef uintptr_t (GUEST *item_fn)(uintptr_t,uint32_t,uint32_t,uint32_t,uint32_t);
static struct {
    struct bbm_mod owner;
    mh_entry entry;
    mh_hook *hook;
    void *original;
    uintptr_t caller;
    uintptr_t actor;
    uint64_t generation;
    uint32_t enabled, busy, used;
    uint64_t dropped;
    bbm_item_use queue[64];
    size_t count;
} bridge;

static void capture(uintptr_t actor,uint32_t id,uint32_t success,uintptr_t caller) {
    if(!__atomic_load_n(&bridge.enabled,__ATOMIC_ACQUIRE) || caller!=bridge.caller || success>1) return;
    uint64_t generation=__atomic_load_n(&bridge.generation,__ATOMIC_ACQUIRE);
    if(!generation || actor!=__atomic_load_n(&bridge.actor,__ATOMIC_ACQUIRE)) return;
    uint32_t expected=0;
    if(!__atomic_compare_exchange_n(&bridge.busy,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) {
        __atomic_fetch_add(&bridge.dropped,1,__ATOMIC_RELAXED); return;
    }
    if(bridge.count<64 && generation==__atomic_load_n(&bridge.generation,__ATOMIC_ACQUIRE))
        bridge.queue[bridge.count++]=(bbm_item_use){sizeof(bbm_item_use),success,generation,actor,(int32_t)id,0};
    else __atomic_fetch_add(&bridge.dropped,1,__ATOMIC_RELAXED);
    __atomic_store_n(&bridge.busy,0,__ATOMIC_RELEASE);
}
static uintptr_t GUEST item_detour(uintptr_t actor,uint32_t id,uint32_t opaque2,uint32_t opaque3,uint32_t success) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    /* Snapshot the observation before the original's potentially lengthy work;
     * the action result has already been produced at the verified caller. */
    capture(actor,id,success,caller);
    return ((item_fn)bridge.original)(actor,id,opaque2,opaque3,success);
}

void mh_native_attack_start(modhost *h);
void mh_native_attack_tick(modhost *h);
void mh_native_attack_stop(modhost *h);

void mh_native_observation_start(modhost *h) {
    mh_native_attack_start(h);
    if(!h->cfg.native_observation || h->cfg.safe_mode) {
        mh_log(h,"bbmod.native",BBM_LOG_INFO,"native item observation disabled (opt-in or safe mode)");return;
    }
    if(bridge.used) {mh_log(h,"bbmod.native",BBM_LOG_WARN,"native item observation unavailable: bridge already used in this process");return;}
    if(
       h->cfg.eboot_size<0x553e880 || !h->cfg.eboot_base ||
       h->cfg.eboot_base>UINTPTR_MAX-0x553e880) {
        mh_log(h,"bbmod.native",BBM_LOG_WARN,"native item observation unavailable: image range invalid");return;
    }
    static const unsigned char prologue[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x18,0x01,0x00,0x00};
    static const unsigned char caller[]={0x45,0x0f,0xb6,0xc4,0x4c,0x89,0xf7,0x8b,0xb5,0x28,0xff,0xff,0xff,0x89,0xda,0x44,0x89,0xe9,0xe8,0x48,0x7c,0x72,0x00};
    unsigned char actual[32];
    if(mh_read_safe(h,(void*)(h->cfg.eboot_base+0x1ff0fa0),actual,sizeof prologue) || memcmp(actual,prologue,sizeof prologue) ||
       mh_read_safe(h,(void*)(h->cfg.eboot_base+0x18c9341),actual,sizeof caller) || memcmp(actual,caller,sizeof caller)) {
        mh_log(h,"bbmod.native",BBM_LOG_WARN,"native item observation unavailable: image signatures differ");return;
    }
    bridge.owner.host=h;bridge.owner.entry=&bridge.entry;
    mh_strlcpy(bridge.entry.m.id,"bbmod.native",sizeof bridge.entry.m.id);
    bridge.caller=h->cfg.eboot_base+0x18c9358;
    bbm_status rc=mh_hook_inline(&bridge.owner,h->cfg.eboot_base+0x1ff0fa0,(void*)item_detour,&bridge.original,&bridge.hook);
    if(rc!=BBM_OK) {mh_log(h,"bbmod.native",BBM_LOG_WARN,"native item observation installation failed (%d)",rc);return;}
    bridge.used=1;
    __atomic_store_n(&bridge.enabled,1,__ATOMIC_RELEASE);
    mh_log(h,"bbmod.native",BBM_LOG_INFO,"native item observation installed; game logging unchanged");
}
void mh_native_observation_tick(modhost *h) {
    mh_native_attack_tick(h);
    if(bridge.owner.host!=h || !bridge.hook) return;
    /* Disable capture while publishing the new identity pair. */
    __atomic_store_n(&bridge.generation,0,__ATOMIC_RELEASE);
    __atomic_store_n(&bridge.actor,h->player_available?h->player_actor:0,__ATOMIC_RELEASE);
    uint64_t generation=h->player_available&&!h->session.in_session?h->player_generation:0;
    __atomic_store_n(&bridge.generation,generation,__ATOMIC_RELEASE);
    uint32_t expected=0;
    if(!__atomic_compare_exchange_n(&bridge.busy,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) return;
    bbm_item_use local[64];size_t count=bridge.count;
    memcpy(local,bridge.queue,count*sizeof local[0]);bridge.count=0;
    __atomic_store_n(&bridge.busy,0,__ATOMIC_RELEASE);
    uint64_t dropped=__atomic_exchange_n(&bridge.dropped,0,__ATOMIC_RELAXED);
    if(dropped) mh_log(h,"bbmod.native",BBM_LOG_WARN,"native item observation dropped %llu records",(unsigned long long)dropped);
    for(size_t i=0;i<count;i++) if(generation && local[i].generation==generation && local[i].actor==h->player_actor) {
        bbm_event ev={BBM_EV_ITEM_USE_OBSERVED,0,generation,0,&local[i],sizeof local[i]};mh_dispatch_event(h,&ev);
    }
}
void mh_native_observation_stop(modhost *h) {
    mh_native_attack_stop(h);
    if(bridge.owner.host!=h || !bridge.hook) return;
    __atomic_store_n(&bridge.enabled,0,__ATOMIC_RELEASE);
    __atomic_store_n(&bridge.generation,0,__ATOMIC_RELEASE);
    void *retained=bridge.hook->block;
    mh_hook_remove(h,bridge.hook);bridge.hook=NULL;
    /* Remove only our block from deferred frees. A cached detour may still call
     * its trampoline after shutdown; host executable + bridge outlive that call.
     * The OS reclaims this bounded allocation on process exit. */
    for(size_t i=0;i<h->ndeferred;) {
        if(h->deferred[i].p==retained) h->deferred[i]=h->deferred[--h->ndeferred];else ++i;
    }
    bridge.owner.host=NULL;
}
#else
void mh_native_observation_start(modhost *h) {(void)h;}
void mh_native_observation_tick(modhost *h) {(void)h;}
void mh_native_observation_stop(modhost *h) {(void)h;}
#endif
