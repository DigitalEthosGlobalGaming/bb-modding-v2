/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"
#include <assert.h>
#include <windows.h>
typedef uintptr_t (__attribute__((sysv_abi)) *call_fn)(uintptr_t,uint32_t,uint32_t);
typedef uintptr_t (__attribute__((sysv_abi)) *raw_fn)(uintptr_t,uint32_t,uint32_t,uint32_t,uint32_t);
static int counts[2]; static bbm_item_use last[2];
static void event(bbm_ctx *ctx,const bbm_event *ev) {
    size_t i=(size_t)ctx;
    assert(ev->type==BBM_EV_ITEM_USE_OBSERVED && ev->data_len==sizeof(bbm_item_use));
    last[i]=*(const bbm_item_use*)ev->data;counts[i]++;
}
int main(void) {
    unsigned char *image=VirtualAlloc(NULL,0x5540000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    assert(image);
    const unsigned char prologue[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x18,0x01,0x00,0x00};
    const unsigned char finish[]={0xb8,0x34,0x12,0,0,0x48,0x81,0xc4,0x18,0x01,0,0,0x5b,0x41,0x5c,0x41,0x5d,0x41,0x5e,0x41,0x5f,0x5d,0xc3};
    memcpy(image+0x1ff0fa0,prologue,sizeof prologue);memcpy(image+0x1ff0fa0+sizeof prologue,finish,sizeof finish);
    const unsigned char setup[]={0x55,0x48,0x89,0xe5,0x53,0x41,0x54,0x41,0x55,0x41,0x56,0x48,0x81,0xec,0,1,0,0,0x49,0x89,0xfe,0x41,0x89,0xd4,0xbb,0,0,0,0,0x41,0xbd,0,0,0,0,0x89,0xb5,0x28,0xff,0xff,0xff};
    unsigned char *caller=image+0x18c9300;
    memset(caller,0x90,0x41);memcpy(caller,setup,sizeof setup);
    const unsigned char call[]={0x45,0x0f,0xb6,0xc4,0x4c,0x89,0xf7,0x8b,0xb5,0x28,0xff,0xff,0xff,0x89,0xda,0x44,0x89,0xe9,0xe8,0x48,0x7c,0x72,0x00};
    memcpy(caller+0x41,call,sizeof call);
    const unsigned char exitcode[]={0x48,0x81,0xc4,0,1,0,0,0x41,0x5e,0x41,0x5d,0x41,0x5c,0x5b,0x5d,0xc3};
    memcpy(caller+0x58,exitcode,sizeof exitcode);
    FlushInstructionCache(GetCurrentProcess(),image,0x5540000);
    modhost_config cfg={0};cfg.eboot_base=(uintptr_t)image;cfg.eboot_size=0x5540000;cfg.native_observation=1;
    cfg.mods_dir="native-observation-test-no-mods";cfg.cache_dir="native-observation-test-cache";
    modhost *h=modhost_create(&cfg);assert(h && h->hooks);
    mh_entry entries[2]={0};struct bbm_mod mods[2]={0};
    for(size_t i=0;i<2;i++){mods[i].host=h;mods[i].entry=&entries[i];mods[i].ctx=(bbm_ctx*)i;mods[i].api.on_event=event;mods[i].events=BBM_EV_ITEM_USE_OBSERVED;entries[i].inst=&mods[i];entries[i].state=MODHOST_ACTIVE;}
    h->mods=entries;h->nmods=2;h->player_available=1;h->player_actor=0x123000;h->player_generation=7;
    mh_native_observation_tick(h);
    call_fn fn=(call_fn)caller;
    assert(fn(0x123000,1000,1)==0x1234);assert(!counts[0]);
    mh_native_observation_tick(h);assert(counts[0]==1 && counts[1]==1);
    assert(!memcmp(&last[0],&last[1],sizeof last[0]) && last[0].item_id==1000 && last[0].success==1);
    fn(0x123001,1000,1);((raw_fn)(image+0x1ff0fa0))(0x123000,1000,0,0,1);
    mh_native_observation_tick(h);assert(counts[0]==1);
    fn(0x123000,1100,0);mh_native_observation_tick(h);assert(counts[0]==2 && !last[0].success);
    for(int i=0;i<70;i++) fn(0x123000,1200,1);
    mh_native_observation_tick(h);assert(counts[0]==66 && counts[1]==66);
    fn(0x123000,1300,1);h->player_generation++;mh_native_observation_tick(h);assert(counts[0]==66);
    h->session.in_session=1;mh_native_observation_tick(h);
    fn(0x123000,1400,1);mh_native_observation_tick(h);assert(counts[0]==66);
    h->session.in_session=0;mh_native_observation_tick(h);
    h->mods=NULL;h->nmods=0;modhost_destroy(h);
    assert(!memcmp(image+0x1ff0fa0,prologue,sizeof prologue));
    assert(fn(0x123000,1000,1)==0x1234);
    modhost *again=modhost_create(&cfg);assert(again && !again->hooks);modhost_destroy(again);
    VirtualFree(image,0,MEM_RELEASE);
    puts("native item observer: real SysV detour, original return, caller/actor filter, two subscribers, overflow, stale epoch, shutdown and reinstall rejection passed");
}
