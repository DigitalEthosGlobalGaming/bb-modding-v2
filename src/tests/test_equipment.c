/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "equipment.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char actor[0x400], character[0x400], world[0x100], manager[0x20];
static int change, reads;
static int reader(void *unused, uintptr_t at, void *out, size_t n) {
    (void)unused;
    uintptr_t value;
    if(at==0x10000000+0x553e878) { value=(uintptr_t)world; memcpy(out,&value,n); return 0; }
    if(at==0x10000000+0x553b130) { value=(uintptr_t)manager; memcpy(out,&value,n); return 0; }
    if(at==(uintptr_t)character+0x248 && change && ++reads==2) character[0x264]^=1;
    const void *regions[]={actor,character,world,manager};
    const size_t sizes[]={sizeof actor,sizeof character,sizeof world,sizeof manager};
    for(unsigned i=0;i<4;i++) if(at>=(uintptr_t)regions[i] && at-(uintptr_t)regions[i]<=sizes[i] && n<=sizes[i]-(at-(uintptr_t)regions[i])) {
        memcpy(out,(void*)at,n); return 0;
    }
    return -1;
}
static void putptr(void *at, void *value) { memcpy(at,&value,sizeof value); }
int main(void) {
    putptr(world+0x60,actor); putptr(actor+0x3c0,character); putptr(manager+8,character);
    uint32_t selectors[6]={1,0,0,0,0,0}; memcpy(character+0x24c,selectors,sizeof selectors);
    uint32_t refs[16], ids[16]; for(unsigned i=0;i<16;i++){refs[i]=0xb0000000+i;ids[i]=1000+i;}
    memcpy(character+0x264,refs,sizeof refs);memcpy(character+0x2a4,ids,sizeof ids);
    mh_equipment_snapshot out;
    assert(mh_equipment_read(0x10000000,(uintptr_t)actor,reader,0,&out)==0);
    assert(out.selected_even==2 && out.selected_odd==1 && out.runtime_references[15]==refs[15] && out.saved_item_ids[15]==1015);
    change=1; reads=0; assert(mh_equipment_read(0x10000000,(uintptr_t)actor,reader,0,&out)==-1);
    assert(out.actor==0); change=0;
    putptr(manager+8,actor);assert(mh_equipment_read(0x10000000,(uintptr_t)actor,reader,0,&out)==-1);
    putptr(manager+8,character); uint32_t invalid=0xffffffff; memcpy(character+0x24c,&invalid,4);
    assert(mh_equipment_read(0x10000000,(uintptr_t)actor,reader,0,&out)==0 && out.selected_even==UINT32_MAX);
    assert(mh_equipment_profile(0x10000000,reader,0)==-1);
    puts("equipment: stable layout, identity mismatch, unstable sample, bounded selectors passed");
}
