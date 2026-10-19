/* SPDX-License-Identifier: GPL-2.0-or-later
 * CUSA03173 1.09 layout independently checked against local eboot instructions.
 * Leads: bbhost boss_rush.cpp goods_count; see modding-docs/inventory-research.md.
 * All addresses are image RVAs (bbhost BN addresses need -0x400000). */
#include "inventory.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
static int pointer(mh_inventory_reader read,void*u,uintptr_t base,size_t off,uintptr_t*out){
    return base>UINTPTR_MAX-off||read(u,base+off,out,8)||*out<0x10000||(*out&7)?-1:0;
}
int mh_inventory_profile(uintptr_t base,mh_inventory_reader read,void*u){
    static const struct{uintptr_t rva;size_t n;unsigned char b[24];} checks[]={
      {0x14da77e,13,{0x41,0x8b,0x86,0x88,0,0,0,0x8d,0x70,1,0x44,0x39,0xfe}},
      {0x14d9fff,16,{0x2b,0x47,0x24,0x73,0x13,0x48,0x89,0xc8,0x48,0xc1,0xe0,4,0x48,3,0x47,0x58}},
      {0x14da879,5,{0x8b,0x48,8,0x39,0xd1}},
      {0x1a8911c,24,{0x48,0x8b,0x4c,0xd1,8,0x48,0x85,0xc9,0x74,0x14,0x39,0x41,8,0x75,0x0f,0x48,0x89,0x4b,8,0x8b,0x41,0x0c,0x89,0x43}},
      {0x1a891c4,20,{0xd6,0xff,0xff,0xff,0xde,0xff,0xff,0xff,0xec,0xff,0xff,0xff,0xc8,0xff,0xff,0xff,0xc8,0xff,0xff,0xff}}
    };
    if(!base||!read)return -1;
    for(size_t i=0;i<sizeof checks/sizeof checks[0];++i){unsigned char b[24];if(base>UINTPTR_MAX-checks[i].rva||read(u,base+checks[i].rva,b,checks[i].n)||memcmp(b,checks[i].b,checks[i].n))return -1;}
    return 0;
}
static int decode(uintptr_t base,uint32_t ref,mh_inventory_reader read,void*u,uint32_t*out){
    uint32_t id=ref&0xffffff;
    if(!ref||id==0xffffff)return -1;
    if(id&0x800000){
        uintptr_t manager,object;uint32_t fields[2],again[2];uint32_t index=ref&0xffff;
        if(index==0xffff||pointer(read,u,base,0x553e990,&manager)||pointer(read,u,manager,8+(size_t)index*8,&object)||object>UINTPTR_MAX-16||read(u,object+8,fields,8)||fields[0]!=ref||read(u,object+8,again,8)||memcmp(fields,again,8))return -1;
        *out=fields[1];return *out==UINT32_MAX?-1:0;
    }
    unsigned kind=(ref>>28)&7;
    if(!(ref&0x80000000)||kind>4||id>=0x7fffff)return -1;
    static const uint32_t category[]={0,0x10000000,0x20000000,0x40000000,0x40000000};
    *out=id|category[kind];return 0;
}
static uint32_t u32(const unsigned char*p){uint32_t n;memcpy(&n,p,4);return n;}
static uintptr_t uptr(const unsigned char*p){uintptr_t n;memcpy(&n,p,8);return n;}
static int arrays(const unsigned char*header,uint32_t total,mh_inventory_reader read,void*u,unsigned char*out){
    uint32_t split=u32(header+0x24),first=total<split?total:split;
    uintptr_t a=uptr(header+0x58),b=uptr(header+0x48);
    if(first&&(a<0x10000||a>UINTPTR_MAX-first*16||read(u,a,out,first*16)))return -1;
    uint32_t second=total-first;
    if(second&&(b<0x10000||b>UINTPTR_MAX-second*16||read(u,b,out+first*16,second*16)))return -1;
    return 0;
}
int mh_inventory_read(uintptr_t base,uintptr_t actor,mh_inventory_reader read,void*u,mh_inventory_snapshot*out){
    if(!out)return -1;
    memset(out,0,sizeof *out);
    if(!read||!base||base>UINTPTR_MAX-0x553e990||actor<0x10000||actor>UINTPTR_MAX-0x3c8)return -1;
    uintptr_t world,current,manager,character,owner,again;
    if(pointer(read,u,base,0x553e878,&world)||pointer(read,u,world,0x60,&current)||current!=actor||pointer(read,u,actor,0x3c0,&character)||character>UINTPTR_MAX-0x3b8||pointer(read,u,base,0x553b130,&manager)||pointer(read,u,manager,8,&owner)||owner!=character)return -1;
    unsigned char header[0x90],check[0x90];
    if(read(u,character+0x328,header,sizeof header))return -1;
    uint32_t last=u32(header+0x88),total=last==UINT32_MAX?0:last+1,split=u32(header+0x24);
    if(total>MH_INVENTORY_LIMIT||split>MH_INVENTORY_LIMIT)return -1;
    size_t bytes=(size_t)total*16;
    unsigned char *raw=malloc(bytes?bytes*2:1);if(!raw)return -1;
    int rc=-1;
    if(arrays(header,total,read,u,raw)||arrays(header,total,read,u,raw+bytes)||memcmp(raw,raw+bytes,bytes)||read(u,character+0x328,check,sizeof check)||memcmp(header,check,sizeof header))goto done;
    for(uint32_t i=0;i<total;++i){
        const unsigned char*p=raw+i*16;uint32_t ref=u32(p);if(!ref)continue;
        mh_inventory_entry *e=&out->entries[out->count++];e->slot=i;e->reference=ref;e->quantity=u32(p+8);e->metadata_04=u32(p+4);e->metadata_0c=u32(p+12);e->encoded_item_id=UINT32_MAX;
        if(e->quantity>INT32_MAX)goto done;
        if(!decode(base,ref,read,u,&e->encoded_item_id))e->flags=MH_INVENTORY_ID_RESOLVED;
    }
    if(pointer(read,u,base,0x553e878,&again)||again!=world||pointer(read,u,base,0x553b130,&again)||again!=manager||pointer(read,u,world,0x60,&again)||again!=actor||pointer(read,u,actor,0x3c0,&again)||again!=character||pointer(read,u,manager,8,&again)||again!=character)goto done;
    out->actor=actor;out->character=character;out->slot_count=total;rc=0;
done:
    free(raw);if(rc)memset(out,0,sizeof *out);return rc;
}
