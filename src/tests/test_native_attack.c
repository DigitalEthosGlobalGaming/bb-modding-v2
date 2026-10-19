/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"
#include <assert.h>
#include <windows.h>
typedef uintptr_t (__attribute__((sysv_abi)) *call_fn)(uintptr_t,uint32_t);
typedef uintptr_t (__attribute__((sysv_abi)) *raw_fn)(uintptr_t,uint32_t);
static int counts[2]; static bbm_attack_observed last[2];
static void event(bbm_ctx *ctx,const bbm_event *ev) {
    size_t i=(size_t)ctx;
    assert(ev->type==BBM_EV_ATTACK_OBSERVED && ev->data_len==sizeof(bbm_attack_observed));
    last[i]=*(const bbm_attack_observed*)ev->data;counts[i]++;
}
int main(void) {
    unsigned char *image=VirtualAlloc(NULL,0x5540000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    assert(image);
    const unsigned char prologue[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x08,0x01,0x00,0x00};
    const unsigned char finish[]={0xb8,0x34,0x12,0,0,0x48,0x81,0xc4,0x08,0x01,0,0,0x5b,0x41,0x5c,0x41,0x5d,0x41,0x5e,0x41,0x5f,0x5d,0xc3};
    memcpy(image+0x1fe8fb0,prologue,sizeof prologue);memcpy(image+0x1fe8fb0+sizeof prologue,finish,sizeof finish);
    const unsigned char signature1[]={0xe8,0xf7,0x0e,0xea,0xff,0x84,0xc0,0x74,0x1b,0x80,0xbd,0x60,0xfe,0xff,0xff,0x00,0x74,0x12,0x49,0x8b,0x47,0x10,0x48,0x89,0xc7,0x8b,0xb5,0x50,0xfe,0xff,0xff,0xe8,0x58,0x29,0x5c,0x00};
    const unsigned char signature2[]={0x41,0x89,0x87,0x34,0x01,0x00,0x00,0x49,0x8b,0x7d,0x08,0x44,0x89,0xf6,0xe8,0x58,0xf7,0x5b,0x00};
    memcpy(image+0x1a26634,signature1,sizeof signature1);
    memcpy(image+0x1a29845,signature2,sizeof signature2);
    /* Fixture entries jump directly to each verified call. Signatures are left
     * intact; this tests bridge ABI/callsite filtering, not game behavior. */
    unsigned char *caller=image+0x1a26500,*caller2=image+0x1a29700;
    const unsigned char setup[]={0x48,0x83,0xec,0x08,0xe9,0x4a,0x01,0,0};
    const unsigned char exitcode[]={0x48,0x83,0xc4,0x08,0xc3};
    memcpy(caller,setup,sizeof setup);memcpy(caller2,setup,sizeof setup);
    memcpy(image+0x1a26658,exitcode,sizeof exitcode);memcpy(image+0x1a29858,exitcode,sizeof exitcode);
    FlushInstructionCache(GetCurrentProcess(),image,0x5540000);
    modhost_config cfg={0};cfg.eboot_base=(uintptr_t)image;cfg.eboot_size=0x5540000;cfg.native_observation=1;
    cfg.mods_dir="native-attack-test-no-mods";cfg.cache_dir="native-attack-test-cache";
    modhost *h=modhost_create(&cfg);assert(h && h->hooks);
    mh_entry entries[2]={0};struct bbm_mod mods[2]={0};
    for(size_t i=0;i<2;i++){mods[i].host=h;mods[i].entry=&entries[i];mods[i].ctx=(bbm_ctx*)i;mods[i].api.on_event=event;mods[i].events=BBM_EV_ATTACK_OBSERVED;entries[i].inst=&mods[i];entries[i].state=MODHOST_ACTIVE;}
    h->mods=entries;h->nmods=2;h->player_available=1;h->player_actor=0x123000;h->player_generation=7;
    mh_native_observation_tick(h);
    call_fn fn=(call_fn)caller;
    assert(fn(0x123000,1000)==0x1234);assert(!counts[0]);
    mh_native_observation_tick(h);assert(counts[0]==1 && counts[1]==1);
    assert(!memcmp(&last[0],&last[1],sizeof last[0]) && last[0].behavior_id==1000 && last[0].source==1);
    fn(0x123001,1000);((raw_fn)(image+0x1fe8fb0))(0x123000,1000);
    mh_native_observation_tick(h);assert(counts[0]==1);
    ((call_fn)caller2)(0x123000,1100);mh_native_observation_tick(h);assert(counts[0]==2 && last[0].source==2);
    for(int i=0;i<70;i++) fn(0x123000,1200);
    mh_native_observation_tick(h);assert(counts[0]==66 && counts[1]==66);
    fn(0x123000,1300);h->player_generation++;mh_native_observation_tick(h);assert(counts[0]==66);
    h->session.in_session=1;mh_native_observation_tick(h);
    fn(0x123000,1400);mh_native_observation_tick(h);assert(counts[0]==66);
    h->session.in_session=0;mh_native_observation_tick(h);
    h->mods=NULL;h->nmods=0;modhost_destroy(h);
    assert(!memcmp(image+0x1fe8fb0,prologue,sizeof prologue));
    assert(fn(0x123000,1000)==0x1234);
    modhost *again=modhost_create(&cfg);assert(again && !again->hooks);modhost_destroy(again);
    VirtualFree(image,0,MEM_RELEASE);
    puts("native attack observer: real SysV detour, original return, caller/actor filter, two subscribers, overflow, stale epoch, shutdown and reinstall rejection passed");
}
