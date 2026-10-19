/* Exercise actual host cache/ABI dispatch against a synthetic player inventory. */
#include "internal.h"
#define main inventory_resolver_test_main
#include "test_inventory.c"
#undef main
static void hex(Memory*m,uintptr_t rva,const char*s){unsigned char b[64];size_t n=strlen(s)/2;assert(n<64);for(size_t i=0;i<n;i++){unsigned v;sscanf(s+i*2,"%2x",&v);b[i]=(unsigned char)v;}put(m,BASE+rva,b,n);}
static void service_fixture(Memory*m){
 fixture(m);
 const struct{uintptr_t rva;const char*hex;}fp[]={
 {0x14da77e,"418b86880000008d70014439fe"},{0x14d9fff,"2b472473134889c848c1e00448034758"},{0x14da879,"8b480839d1"},
 {0x1a8911c,"488b4cd1084885c97414394108750f48894b088b410c8943"},{0x1a891c4,"d6ffffffdeffffffecffffffc8ffffffc8ffffff"},
 {0x18c2d00,"554889e54156534889fb4c8d3567bbc703"},{0x18c2d3d,"483958600f94c0"},{0x18f78b1,"8b88f8000000"},
 {0x18f78db,"8b8034010000"},{0x1900278,"498b96c00300008b8294000000"},{0x203a4a6,"8b4740c38b4748c38b4750c38b4758c38b4760c38b4768c38b4770c3"},{0x1a571a7,"41f6860002000080"}};
 for(size_t i=0;i<sizeof fp/sizeof fp[0];i++)hex(m,fp[i].rva,fp[i].hex);
 p64(m,ACTOR+0x3b0,0x40000);p64(m,0x40020,0x50000);p64(m,0x50008,ACTOR);
 int32_t values[17]={573,573};values[15]=values[16]=91;put(m,0x500f8,values,sizeof values);
 w32(m->r[5].b,0x88,69);m->r[7].n=68*16;
 for(unsigned i=0;i<68;i++){w32(m->r[7].b,i*16,0xb0000000|(1000+i));w32(m->r[7].b,i*16+8,20);}
}
static int read_host(void*u,const void*at,void*out,size_t n){return read_mem(u,(uintptr_t)at,out,n);}
typedef struct{unsigned count;bbm_inventory_change change;}Observer;
static void event(bbm_ctx*ctx,const bbm_event*ev){Observer*o=(Observer*)ctx;assert(ev->type==BBM_EV_INVENTORY_CHANGED);assert(ev->data_len==sizeof o->change);memcpy(&o->change,ev->data,sizeof o->change);o->count++;}
int main(void){
 Memory m={0};service_fixture(&m);modhost_config cfg={0};cfg.cache_dir="inventory-service-test-cache";cfg.mods_dir="inventory-service-test-no-mods";cfg.eboot_base=BASE;cfg.eboot_size=0x6000000;cfg.platform.user=&m;cfg.platform.read_safe=read_host;
 modhost*h=modhost_create(&cfg);assert(h);mh_inventory_tick(h);assert(h->inventory==NULL&&m.reads==0);
 h->mods=calloc(2,sizeof*h->mods);h->nmods=h->capmods=2;
 Observer oa={0},ob={0};struct bbm_mod a={.host=h,.entry=&h->mods[0],.ctx=(bbm_ctx*)&oa},b={.host=h,.entry=&h->mods[1],.ctx=(bbm_ctx*)&ob};
 h->mods[0].inst=&a;h->mods[1].inst=&b;h->mods[0].state=h->mods[1].state=MODHOST_ACTIVE;a.events=b.events=BBM_EV_INVENTORY_CHANGED;a.api.on_event=b.api.on_event=event;
 modhost_frame(h);assert(oa.count==1&&ob.count==1);assert(oa.change.current.flags==BBM_INVENTORY_AVAILABLE&&oa.change.current.item_count==70);assert(!memcmp(&oa.change,&ob.change,sizeof oa.change));
 bbm_inventory_page page;assert(!h->api.inventory_page(&a,0,0,64,&page));uint64_t revision=page.snapshot.revision;assert(page.count==64&&page.entries[0].quantity==1);int reads=m.reads;
 assert(!h->api.inventory_page(&b,revision,64,64,&page));assert(page.count==6&&page.entries[0].slot==64&&m.reads==reads);
 assert(h->api.inventory_page(&a,revision,71,64,&page)==BBM_ERR_ARG);assert(h->api.inventory_page(&a,revision,0,65,&page)==BBM_ERR_ARG);assert(!h->api.inventory_page(&a,revision,70,64,&page)&&page.count==0);
 /* A changed live value does not corrupt a page already pinned to this cache. */
 w32(m.r[6].b,8,2);assert(!h->api.inventory_page(&a,revision,0,64,&page));assert(page.entries[0].quantity==1&&m.reads==reads);
 plat_sleep_ms(260);modhost_frame(h);assert(oa.count==2&&ob.count==2);assert(h->api.inventory_page(&a,revision,64,64,&page)==BBM_ERR_STATE&&page.count==0);assert(!h->api.inventory_page(&a,0,0,64,&page));assert(page.entries[0].quantity==2&&page.snapshot.revision>revision);revision=page.snapshot.revision;
 /* No-change refresh produces no duplicate event. */
 plat_sleep_ms(260);modhost_frame(h);assert(oa.count==2&&ob.count==2);
 /* Profile change invalidates existing pages and emits one unavailable sample. */
 m.r[11].b[0]^=1;plat_sleep_ms(260);modhost_frame(h);assert(oa.count==3&&!oa.change.current.flags&&ob.count==3);assert(h->api.inventory_page(&a,revision,0,64,&page)==BBM_ERR_STATE);assert(h->api.inventory_page(&a,0,0,64,&page)==BBM_ERR_UNSUPPORTED);
 m.r[11].b[0]^=1;plat_sleep_ms(260);modhost_frame(h);assert(oa.count==4&&oa.change.current.flags);
 a.events=b.events=0;reads=m.reads;plat_sleep_ms(260);mh_inventory_tick(h);assert(m.reads==reads); /* idle has zero scan cost */
 assert(!h->api.inventory_page(&a,0,0,1,&page));assert(page.count==1&&m.reads>reads); /* demand resumes bounded refresh */
 h->mods[0].inst=h->mods[1].inst=NULL;modhost_destroy(h);
 puts("inventory service: two observers, cached pagination, consistent revisions, 250ms throttle, unavailable/recovery, idle/demand: OK");return 0;
}
