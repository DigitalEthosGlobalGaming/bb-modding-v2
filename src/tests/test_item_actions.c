/* Actual guest getter detour + bounded native item request execution. */
#include "internal.h"
#include <assert.h>
#include <windows.h>
#define GUEST __attribute__((sysv_abi))
static unsigned made,awarded,released;static int fail_construct;
static uint32_t observed[5];
static void GUEST construct(uint32_t*out,uintptr_t manager,uint32_t*id){assert(manager);made++;*out=fail_construct==2?0x80ffffff:fail_construct?0:0xb0000000|(*id&0xffffff);}
static void GUEST award(uintptr_t manager,void*batch){assert(manager);awarded++;memcpy(observed,batch,sizeof observed);}
static void GUEST release(uint32_t*p){assert(*p);released++;*p=0;}
static void hex(unsigned char*p,const char*s){for(size_t i=0;i<strlen(s)/2;i++){unsigned n;sscanf(s+i*2,"%2x",&n);p[i]=(unsigned char)n;}}
static void jump(unsigned char*p,void*fn){p[0]=0x48;p[1]=0xb8;memcpy(p+2,&fn,8);p[10]=0xff;p[11]=0xe0;}
static void pointer(void*p,uintptr_t n){memcpy(p,&n,8);}
int main(void){
 unsigned char*image=VirtualAlloc(NULL,0x5550000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(image);
 const struct{uintptr_t rva;const char*bytes;}fps[]={
 {0x1f1e3ec,"4c8d3d4d1f6203498b07"},{0x1f1e42e,"488b8050010000"},{0x1f1e440,"488b40704c8b4070"},
 {0x18fd960,"488b87c0030000c30f1f840000000000"},{0x18f7891,"498b06ff90c8010000"},
 {0x17d89f0,"554889e54157415641554154534881ec"},{0x17d8a7c,"418b4708488d8dc4feffff894108498b07488901"},
 {0x1a86ff0,"554889e54157415641554154534883ec"},{0x1a8714b,"89c181e1ffffff0081c9000000b03dffff7f0041bcffffffb0440f42e1"},
 {0x1a8bc00,"554889e54156534889fb8b3385f60f84"},
 {0x18c2d00,"554889e54156534889fb4c8d3567bbc703"},{0x18c2d3d,"483958600f94c0"},{0x18f78b1,"8b88f8000000"},
 {0x18f78db,"8b8034010000"},{0x1900278,"498b96c00300008b8294000000"},{0x203a4a6,"8b4740c38b4748c38b4750c38b4758c38b4760c38b4768c38b4770c3"},{0x1a571a7,"41f6860002000080"}};
 for(size_t i=0;i<sizeof fps/sizeof fps[0];i++)hex(image+fps[i].rva,fps[i].bytes);
 unsigned char actor[0x400]={0},character[0x400]={0},world[0x80]={0},manager[0x20]={0},modules[0x40]={0},component[0x240]={0},table[0x200]={0};
 unsigned char repo[0x1300]={0},cap[0x80]={0},holder[0x80]={0},param[0x200]={0};
 pointer(image+0x5540340,(uintptr_t)repo);*(uint32_t*)(repo+0x148)=1;pointer(repo+0x150,(uintptr_t)cap);pointer(cap+0x70,(uintptr_t)holder);pointer(holder+0x70,(uintptr_t)param);
 *(uint16_t*)(param+0xa)=1;memcpy(param+0xc,"EQUIP_PARAM_GOODS_ST",20);param[0x2d]=4;param[0x2e]=2;*(uint32_t*)(param+0x40)=1000;pointer(param+0x48,0x80);
 pointer(actor,(uintptr_t)table);pointer(table+0x1c8,(uintptr_t)(image+0x18fd960));pointer(actor+0x3c0,(uintptr_t)character);
 pointer(actor+0x3b0,(uintptr_t)modules);pointer(modules+0x20,(uintptr_t)component);pointer(component+8,(uintptr_t)actor);
 int32_t values[17]={573,573};values[15]=values[16]=91;memcpy(component+0xf8,values,sizeof values);
 pointer(image+0x553e878,(uintptr_t)world);pointer(world+0x60,(uintptr_t)actor);pointer(image+0x553b130,(uintptr_t)manager);pointer(manager+8,(uintptr_t)character);
 pointer(image+0x553d6e0,(uintptr_t)manager);pointer(image+0x553e990,(uintptr_t)manager);
 /* Caller fixture loads the actor vtable, then enters verified virtual call. */
 hex(image+0x18f7800,"4883ec08488b07e988000000");hex(image+0x18f789a,"4883c408c3");
 FlushInstructionCache(GetCurrentProcess(),image,0x5550000);
 modhost_config cfg={0};cfg.eboot_base=(uintptr_t)image;cfg.eboot_size=0x5550000;cfg.native_observation=1;cfg.mods_dir="item-actions-no-mods";cfg.cache_dir="item-actions-cache";
 modhost*h=modhost_create(&cfg);assert(h&&h->hooks);
 /* Replace only fixture action bodies after startup fingerprints: no real game
    routines execute here; getter and host hook/trampoline remain actual code. */
 jump(image+0x1a86ff0,(void*)construct);jump(image+0x17d89f0,(void*)award);jump(image+0x1a8bc00,(void*)release);FlushInstructionCache(GetCurrentProcess(),image,0x5550000);
 mh_entry entry={0},other_entry={0};struct bbm_mod mod={.host=h,.entry=&entry},other={.host=h,.entry=&other_entry};bbm_player_snapshot player;assert(!mh_gameplay_read(h,&player));mh_item_actions_tick(h);
 typedef uintptr_t(GUEST*Fn)(uintptr_t);Fn fn=(Fn)(image+0x18f7800);Fn raw=(Fn)(image+0x18fd960);
 uint64_t id;bbm_item_give_result result;
 assert(h->api.item_give(&mod,0x40600000,2,player.generation+1,&id)==BBM_ERR_STATE);
 assert(h->api.item_give(&mod,50000000,2,player.generation,&id)==BBM_ERR_ARG);
 assert(!h->api.item_give(&mod,0x400003e8,3,player.generation,&id));assert(!made);
 assert(h->api.item_give_status(&other,id,&result)==BBM_ERR_NOTFOUND);
 assert(!h->api.item_give_status(&mod,id,&result)&&result.state==BBM_ITEM_PENDING);
 assert(raw((uintptr_t)actor)==(uintptr_t)character&&!made);
 assert(fn((uintptr_t)actor)==(uintptr_t)character);assert(made==1&&awarded==1&&released==1);
 assert(observed[0]==1&&observed[1]==0xb00003e8&&observed[2]==0x400003e8&&observed[3]==3&&observed[4]==UINT32_MAX);
 assert(!h->api.item_give_status(&mod,id,&result)&&result.state==BBM_ITEM_SUBMITTED);assert(h->api.item_give_status(&mod,id,&result)==BBM_ERR_NOTFOUND);
 assert(!h->api.item_give(&mod,0x400003e9,1,player.generation,&id));fn((uintptr_t)actor);assert(!h->api.item_give_status(&mod,id,&result)&&result.state==BBM_ITEM_FAILED);assert(made==1);
 pointer(image+0x5540340,0);assert(!h->api.item_give(&mod,0x400003e8,1,player.generation,&id));fn((uintptr_t)actor);assert(!h->api.item_give_status(&mod,id,&result)&&result.state==BBM_ITEM_FAILED);assert(made==1);pointer(image+0x5540340,(uintptr_t)repo);
 param[0x2d]=3;assert(!h->api.item_give(&mod,0x400003e8,1,player.generation,&id));fn((uintptr_t)actor);assert(!h->api.item_give_status(&mod,id,&result)&&result.state==BBM_ITEM_FAILED);assert(made==1);param[0x2d]=4;
 param[0x2e]=0;assert(!h->api.item_give(&mod,0x400003e8,1,player.generation,&id));fn((uintptr_t)actor);assert(!h->api.item_give_status(&mod,id,&result)&&result.state==BBM_ITEM_FAILED);assert(made==1);param[0x2e]=2;
 fail_construct=1;assert(!h->api.item_give(&mod,0x400003e8,1,player.generation,&id));fn((uintptr_t)actor);assert(!h->api.item_give_status(&mod,id,&result)&&result.state==BBM_ITEM_FAILED);assert(awarded==1&&released==1);fail_construct=0;
 fail_construct=2;assert(!h->api.item_give(&mod,0x400003e8,1,player.generation,&id));fn((uintptr_t)actor);assert(!h->api.item_give_status(&mod,id,&result)&&result.state==BBM_ITEM_FAILED);assert(awarded==1&&released==1);fail_construct=0;
 uint64_t pending[8];for(int i=0;i<8;i++)assert(!h->api.item_give(&mod,0x400003e8,1,player.generation,&pending[i]));assert(h->api.item_give(&mod,0x400003e8,1,player.generation,&id)==BBM_ERR_BUDGET);
 mh_item_actions_cancel(&mod);mh_item_actions_tick(h);fn((uintptr_t)actor);assert(awarded==1);
 assert(!h->api.item_give(&mod,0x400003e8,1,player.generation,&id));h->session.in_session=1;mh_item_actions_tick(h);fn((uintptr_t)actor);assert(!h->api.item_give_status(&mod,id,&result)&&result.state==BBM_ITEM_CANCELLED);assert(awarded==1);h->session.in_session=0;mh_item_actions_tick(h);
 assert(!h->api.item_give(&mod,0x400003e8,1,player.generation,&id));h->player_generation++;mh_item_actions_tick(h);fn((uintptr_t)actor);assert(!h->api.item_give_status(&mod,id,&result)&&result.state==BBM_ITEM_CANCELLED);
 modhost_destroy(h);assert(!memcmp(image+0x18fd960,"\x48\x8b\x87\xc0\x03\0\0\xc3",8));assert(fn((uintptr_t)actor)==(uintptr_t)character);
 modhost*again=modhost_create(&cfg);assert(again&&!again->hooks);modhost_destroy(again);VirtualFree(image,0,MEM_RELEASE);
 puts("item actions: real getter detour, guest-only ordered calls, ownership, budgets, failed constructor, generation/network cancellation, shutdown passed");
}
