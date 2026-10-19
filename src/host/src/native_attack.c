/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"

/* Process-lifetime host bridge. Never dereferences host/mod/actor pointers from
 * the detour. Its one code allocation is retained on shutdown for cached calls. */
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#define GUEST __attribute__((sysv_abi))
typedef uintptr_t (GUEST *attack_fn)(uintptr_t,uint32_t);
static struct {
    struct bbm_mod owner;
    mh_entry entry;
    mh_hook *hook;
    void *original;
    uintptr_t caller[2];
    uintptr_t actor;
    uint64_t generation;
    uint32_t enabled, busy, used;
    uint64_t dropped;
    bbm_attack_observed queue[64];
    size_t count;
} bridge;

static void capture(uintptr_t actor,uint32_t id,uint32_t source,uintptr_t caller) {
    if(!__atomic_load_n(&bridge.enabled,__ATOMIC_ACQUIRE) || (caller!=bridge.caller[0] && caller!=bridge.caller[1]) || source<1 || source>2) return;
    uint64_t generation=__atomic_load_n(&bridge.generation,__ATOMIC_ACQUIRE);
    if(!generation || actor!=__atomic_load_n(&bridge.actor,__ATOMIC_ACQUIRE)) return;
    uint32_t expected=0;
    if(!__atomic_compare_exchange_n(&bridge.busy,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) {
        __atomic_fetch_add(&bridge.dropped,1,__ATOMIC_RELAXED); return;
    }
    if(bridge.count<64 && generation==__atomic_load_n(&bridge.generation,__ATOMIC_ACQUIRE))
        bridge.queue[bridge.count++]=(bbm_attack_observed){sizeof(bbm_attack_observed),source,generation,actor,(int32_t)id,0};
    else __atomic_fetch_add(&bridge.dropped,1,__ATOMIC_RELAXED);
    __atomic_store_n(&bridge.busy,0,__ATOMIC_RELEASE);
}
static uintptr_t GUEST attack_detour(uintptr_t actor,uint32_t id) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    uint32_t source=caller==bridge.caller[0]?1:2;
    capture(actor,id,source,caller);
    return ((attack_fn)bridge.original)(actor,id);
}

void mh_native_attack_start(modhost *h) {
    if(!h->cfg.native_observation || h->cfg.safe_mode) {
        mh_log(h,"bbmod.native.attack",BBM_LOG_INFO,"native attack observation disabled (opt-in or safe mode)");return;
    }
    if(bridge.used) {mh_log(h,"bbmod.native.attack",BBM_LOG_WARN,"native attack observation unavailable: bridge already used in this process");return;}
    if(
       h->cfg.eboot_size<0x553e880 || !h->cfg.eboot_base ||
       h->cfg.eboot_base>UINTPTR_MAX-0x553e880) {
        mh_log(h,"bbmod.native.attack",BBM_LOG_WARN,"native attack observation unavailable: image range invalid");return;
    }
    static const unsigned char prologue[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x08,0x01,0x00,0x00};
    static const unsigned char caller1[]={0xe8,0xf7,0x0e,0xea,0xff,0x84,0xc0,0x74,0x1b,0x80,0xbd,0x60,0xfe,0xff,0xff,0x00,0x74,0x12,0x49,0x8b,0x47,0x10,0x48,0x89,0xc7,0x8b,0xb5,0x50,0xfe,0xff,0xff,0xe8,0x58,0x29,0x5c,0x00};
    static const unsigned char caller2[]={0x41,0x89,0x87,0x34,0x01,0x00,0x00,0x49,0x8b,0x7d,0x08,0x44,0x89,0xf6,0xe8,0x58,0xf7,0x5b,0x00};
    unsigned char actual[40];
    if(mh_read_safe(h,(void*)(h->cfg.eboot_base+0x1fe8fb0),actual,sizeof prologue) || memcmp(actual,prologue,sizeof prologue) ||
       mh_read_safe(h,(void*)(h->cfg.eboot_base+0x1a26634),actual,sizeof caller1) || memcmp(actual,caller1,sizeof caller1) ||
       mh_read_safe(h,(void*)(h->cfg.eboot_base+0x1a29845),actual,sizeof caller2) || memcmp(actual,caller2,sizeof caller2)) {
        mh_log(h,"bbmod.native.attack",BBM_LOG_WARN,"native attack observation unavailable: image signatures differ");return;
    }
    bridge.owner.host=h;bridge.owner.entry=&bridge.entry;
    mh_strlcpy(bridge.entry.m.id,"bbmod.native.attack",sizeof bridge.entry.m.id);
    bridge.caller[0]=h->cfg.eboot_base+0x1a26658;
    bridge.caller[1]=h->cfg.eboot_base+0x1a29858;
    bbm_status rc=mh_hook_inline(&bridge.owner,h->cfg.eboot_base+0x1fe8fb0,(void*)attack_detour,&bridge.original,&bridge.hook);
    if(rc!=BBM_OK) {mh_log(h,"bbmod.native.attack",BBM_LOG_WARN,"native attack observation installation failed (%d)",rc);return;}
    bridge.used=1;
    __atomic_store_n(&bridge.enabled,1,__ATOMIC_RELEASE);
    mh_log(h,"bbmod.native.attack",BBM_LOG_INFO,"native attack observation installed; game logging unchanged");
}
void mh_native_attack_tick(modhost *h) {
    if(bridge.owner.host!=h || !bridge.hook) return;
    /* Disable capture while publishing the new identity pair. */
    __atomic_store_n(&bridge.generation,0,__ATOMIC_RELEASE);
    __atomic_store_n(&bridge.actor,h->player_available?h->player_actor:0,__ATOMIC_RELEASE);
    uint64_t generation=h->player_available&&!h->session.in_session?h->player_generation:0;
    __atomic_store_n(&bridge.generation,generation,__ATOMIC_RELEASE);
    uint32_t expected=0;
    if(!__atomic_compare_exchange_n(&bridge.busy,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) return;
    bbm_attack_observed local[64];size_t count=bridge.count;
    memcpy(local,bridge.queue,count*sizeof local[0]);bridge.count=0;
    __atomic_store_n(&bridge.busy,0,__ATOMIC_RELEASE);
    uint64_t dropped=__atomic_exchange_n(&bridge.dropped,0,__ATOMIC_RELAXED);
    if(dropped) mh_log(h,"bbmod.native.attack",BBM_LOG_WARN,"native attack observation dropped %llu records",(unsigned long long)dropped);
    for(size_t i=0;i<count;i++) if(generation && local[i].generation==generation && local[i].actor==h->player_actor) {
        bbm_event ev={BBM_EV_ATTACK_OBSERVED,0,generation,0,&local[i],sizeof local[i]};mh_dispatch_event(h,&ev);
    }
}
void mh_native_attack_stop(modhost *h) {
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
void mh_native_attack_start(modhost *h) {(void)h;}
void mh_native_attack_tick(modhost *h) {(void)h;}
void mh_native_attack_stop(modhost *h) {(void)h;}
#endif
