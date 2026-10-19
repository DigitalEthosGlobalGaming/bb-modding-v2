#include "inventory.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BASE ((uintptr_t)0x800000000ULL)
#define ACTOR ((uintptr_t)0x20000)
#define CHAR ((uintptr_t)0x60000)
typedef struct{uintptr_t a;size_t n;unsigned char b[2048];}Region;
typedef struct{Region r[32];size_t n;int unstable,reads;}Memory;
static void put(Memory*m,uintptr_t a,const void*b,size_t n){assert(m->n<32&&n<=2048);m->r[m->n].a=a;m->r[m->n].n=n;memcpy(m->r[m->n++].b,b,n);}
static int read_mem(void*u,uintptr_t a,void*out,size_t n){Memory*m=u;for(size_t i=0;i<m->n;i++){Region*r=&m->r[i];if(a>=r->a&&a-r->a<=r->n&&n<=r->n-(a-r->a)){memcpy(out,r->b+a-r->a,n);if(a==0x70000&&++m->reads==2&&m->unstable)((unsigned char*)out)[0]^=1;return 0;}}return -1;}
static void p64(Memory*m,uintptr_t a,uintptr_t v){put(m,a,&v,8);}
static void w32(unsigned char*p,size_t o,uint32_t v){memcpy(p+o,&v,4);}
static void w64(unsigned char*p,size_t o,uintptr_t v){memcpy(p+o,&v,8);}
static void fixture(Memory*m){
 p64(m,BASE+0x553e878,0x10000);p64(m,0x10060,ACTOR);p64(m,ACTOR+0x3c0,CHAR);p64(m,BASE+0x553b130,0x30000);p64(m,0x30008,CHAR);
 unsigned char header[0x90]={0};w32(header,0x24,2);w32(header,0x88,3);w64(header,0x58,0x70000);w64(header,0x48,0x71000);put(m,CHAR+0x328,header,sizeof header);
 uint32_t first[]={0x800003e8,0x12345678,1,99,0xb00003e8,0,20,0};put(m,0x70000,first,sizeof first);
 uint32_t second[]={0,0,0,0,0x80800002,0,1,0};put(m,0x71000,second,sizeof second);
 p64(m,BASE+0x553e990,0x80000);p64(m,0x80018,0x90000);uint32_t obj[]={0x80800002,1001000};put(m,0x90008,obj,sizeof obj);
}
int main(void){
 Memory m={0};fixture(&m);mh_inventory_snapshot*out=malloc(sizeof*out);assert(out);
 assert(!mh_inventory_read(BASE,ACTOR,read_mem,&m,out));assert(out->slot_count==4&&out->count==3);
 assert(out->entries[0].encoded_item_id==1000&&out->entries[0].metadata_04==0x12345678&&out->entries[0].flags==MH_INVENTORY_ID_RESOLVED);
 assert(out->entries[1].encoded_item_id==0x400003e8&&out->entries[1].quantity==20);
 assert(out->entries[2].slot==3&&out->entries[2].encoded_item_id==1001000);
 /* A stale dynamic object must never be mislabeled as the new item occupying it. */
 w32(m.r[10].b,0,0x80800003);m.reads=0;assert(!mh_inventory_read(BASE,ACTOR,read_mem,&m,out));assert(!out->entries[2].flags&&out->entries[2].encoded_item_id==UINT32_MAX);
 m.unstable=1;m.reads=0;assert(mh_inventory_read(BASE,ACTOR,read_mem,&m,out));assert(!out->count);m.unstable=0;
 w32(m.r[5].b,0x88,4096);assert(mh_inventory_read(BASE,ACTOR,read_mem,&m,out));assert(!out->count);
 w32(m.r[5].b,0x88,UINT32_MAX);assert(!mh_inventory_read(BASE,ACTOR,read_mem,&m,out));assert(!out->count&&!out->slot_count);
 w32(m.r[5].b,0x88,3);w64(m.r[4].b,0,0x50000);assert(mh_inventory_read(BASE,ACTOR,read_mem,&m,out));assert(!out->count);
 assert(mh_inventory_profile(BASE,read_mem,&m));
 free(out);puts("inventory: split arrays, empty slots, inline/dynamic item IDs, stale handle, unstable data, bounded counts, owner mismatch: OK");return 0;
}
