#include "internal.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#define BASE ((uintptr_t)0x800000000ULL)
#define WORLD ((uintptr_t)0x10000)
#define ACTOR ((uintptr_t)0x20000)
#define MODULES ((uintptr_t)0x30000)
#define COMPONENT ((uintptr_t)0x40000)
typedef struct { uintptr_t at; size_t len; uint8_t data[96]; } Region;
typedef struct {Region r[24];size_t n;int writes;int fail_write;} Memory;
static void add(Memory*m,uintptr_t at,const void*p,size_t n){assert(m->n<24&&n<=96);Region*r=&m->r[m->n++];r->at=at;r->len=n;memcpy(r->data,p,n);}
static int read_mem(void*u,const void*a,void*out,size_t n){Memory*m=u;uintptr_t at=(uintptr_t)a;for(size_t i=0;i<m->n;++i){Region*r=&m->r[i];if(at>=r->at&&at-r->at<=r->len&&n<=r->len-(at-r->at)){memcpy(out,r->data+(at-r->at),n);return 0;}}return -1;}
static int write_mem(void*u,void*a,const void*in,size_t n){Memory*m=u;if(m->fail_write)return -1;uintptr_t at=(uintptr_t)a;for(size_t i=0;i<m->n;++i){Region*r=&m->r[i];if(at>=r->at&&at-r->at<=r->len&&n<=r->len-(at-r->at)){memcpy(r->data+(at-r->at),in,n);m->writes++;return 0;}}return -1;}
static void num(Memory*m,uintptr_t a,int32_t v){assert(!write_mem(m,(void*)a,&v,4));}
static void fixture(Memory*m){
 const struct{size_t o;const char*h;}sig[]={
 {0x18c2d00,"554889e54156534889fb4c8d3567bbc703"},{0x18c2d3d,"483958600f94c0"},
 {0x18f78b1,"8b88f8000000"},{0x18f78db,"8b8034010000"},{0x1900278,"498b96c00300008b8294000000"},
 {0x203a4a6,"8b4740c38b4748c38b4750c38b4758c38b4760c38b4768c38b4770c3"},{0x1a571a7,"41f6860002000080"}};
 for(size_t i=0;i<7;++i){uint8_t b[32];size_t n=strlen(sig[i].h)/2;for(size_t j=0;j<n;++j){unsigned v;sscanf(sig[i].h+j*2,"%2x",&v);b[j]=(uint8_t)v;}add(m,BASE+sig[i].o,b,n);}
 uintptr_t p=WORLD;add(m,BASE+0x553e878,&p,8);p=ACTOR;add(m,WORLD+0x60,&p,8);p=MODULES;add(m,ACTOR+0x3b0,&p,8);p=COMPONENT;add(m,MODULES+0x20,&p,8);p=ACTOR;add(m,COMPONENT+8,&p,8);
 int32_t values[17]={573,573};values[15]=values[16]=91;add(m,COMPONENT+0xf8,values,sizeof values);
}
typedef struct{int count;bbm_player_change change;} Observer;
static void event(bbm_ctx*u,const bbm_event*e){Observer*o=(Observer*)u;assert(e->type==BBM_EV_PLAYER_CHANGED);assert(e->data_len==sizeof o->change);memcpy(&o->change,e->data,sizeof o->change);o->count++;}
typedef struct{struct bbm_mod*m;bbm_player_snapshot expected;uint32_t field;int delta,rc,actual;} Request;
static void request(void*u){Request*r=u;r->rc=r->m->host->api.player_adjust(r->m,r->field,r->expected.generation,r->field==1?r->expected.health:r->expected.stamina,r->delta,&r->actual);}
static int adjust(struct bbm_mod*m,uint32_t field,int delta,bbm_player_snapshot*p){Request r={m,*p,field,delta,0,0};mh_call c={.kind=MH_CALL_VOID1,.f=(void(*)(void))request,.a=&r};m->host->gameplay_in_frame=1;assert(!mh_call_mod(m,&c));m->host->gameplay_in_frame=0;assert(r.rc==BBM_OK);return r.actual;}
int main(void){
 Memory memory={0};fixture(&memory);modhost_config cfg={0};cfg.cache_dir="gameplay-test-cache";cfg.mods_dir="gameplay-test-no-mods";cfg.eboot_base=BASE;cfg.eboot_size=0x6000000;cfg.platform.user=&memory;cfg.platform.read_safe=read_mem;cfg.platform.write_code=write_mem;
 modhost*h=modhost_create(&cfg);assert(h);h->mods=calloc(2,sizeof *h->mods);h->nmods=h->capmods=2;
 struct bbm_mod a={.host=h,.entry=&h->mods[0]},b={.host=h,.entry=&h->mods[1]};Observer oa={0},ob={0};a.ctx=(bbm_ctx*)&oa;b.ctx=(bbm_ctx*)&ob;a.api.on_event=b.api.on_event=event;a.events=b.events=BBM_EV_PLAYER_CHANGED;
 h->mods[0].inst=&a;h->mods[1].inst=&b;h->mods[0].state=h->mods[1].state=MODHOST_ACTIVE;strcpy(a.entry->m.id,"a.test");strcpy(b.entry->m.id,"b.test");
 bbm_player_snapshot p;assert(!h->api.player_snapshot(&a,&p));assert(p.health==573&&p.stamina==91&&p.generation==1);int out;assert(h->api.player_adjust(&a,1,p.generation,573,-1,&out)==BBM_ERR_STATE);assert(memory.writes==0);
 modhost_frame(h);assert(oa.count==1&&ob.count==1);modhost_frame(h);assert(oa.count==1&&ob.count==1);
 bbm_id half,twice;assert(!h->api.player_modifier(&a,1,500,20,&half));assert(!h->api.player_modifier(&b,1,2000,10,&twice));assert(adjust(&a,1,-3,&p)==-3); /* twice then half, not half then twice */
 modhost_frame(h);assert(oa.count==2&&ob.count==2&&oa.change.current.health==570&&ob.change.previous.health==573);
 Request stale={&a,p,1,-1,0,0};h->gameplay_in_frame=1;mh_call c={.kind=MH_CALL_VOID1,.f=(void(*)(void))request,.a=&stale};mh_call_mod(&a,&c);h->gameplay_in_frame=0;assert(stale.rc==BBM_ERR_STATE);
 assert(!h->api.player_snapshot(&a,&p));assert(h->api.release(&a,twice)==BBM_ERR_NOTFOUND);assert(!h->api.release(&b,twice));assert(adjust(&a,1,-3,&p)==-1);
 mh_res_release_all(&a);assert(!h->api.player_snapshot(&a,&p));assert(adjust(&a,1,-3,&p)==-3);
 assert(!h->api.player_snapshot(&a,&p));assert(adjust(&a,1,INT32_MAX,&p)==573-p.health);assert(!h->api.player_snapshot(&a,&p));assert(adjust(&a,1,INT32_MIN,&p)==-572);
 assert(!h->api.player_snapshot(&a,&p));assert(adjust(&a,2,INT32_MIN,&p)==-91);assert(!h->api.player_snapshot(&a,&p));assert(adjust(&a,2,INT32_MAX,&p)==91);
 num(&memory,COMPONENT+0xf8,0);assert(!h->api.player_snapshot(&a,&p));Request dead={&a,p,1,5,0,0};c.a=&dead;h->gameplay_in_frame=1;mh_call_mod(&a,&c);h->gameplay_in_frame=0;assert(dead.rc==BBM_ERR_STATE);num(&memory,COMPONENT+0xf8,573);
 assert(h->api.player_modifier(&a,1,4001,0,&half)==BBM_ERR_ARG);for(int i=0;i<64;++i)assert(!h->api.player_modifier(&a,1,1000,0,&half));assert(h->api.player_modifier(&b,1,1000,0,&half)==BBM_ERR_BUDGET);mh_res_release_all(&a);
 assert(!h->api.player_modifier(&b,1,0,0,&twice));b.faulted=1;assert(!h->api.player_snapshot(&a,&p));assert(adjust(&a,1,-1,&p)==-1);b.faulted=0;mh_res_release_all(&b);
 /* Each reload must remove just its old claims, with no accumulated scales. */
 for(int i=0;i<20;++i){assert(!h->api.player_modifier(&b,1,500,0,&twice));mh_res_release_all(&b);assert(!h->api.player_snapshot(&a,&p));assert(adjust(&a,1,-1,&p)==-1);}
 uint64_t generation=p.generation;memory.r[0].data[0]^=1;assert(h->api.player_snapshot(&a,&p)==BBM_ERR_UNSUPPORTED);assert(!p.flags);modhost_frame(h);assert(!oa.change.current.flags);memory.r[0].data[0]^=1;assert(!h->api.player_snapshot(&a,&p));assert(p.generation>generation);
 h->session.in_session=1;Request online={&a,p,1,-1,0,0};c.a=&online;h->gameplay_in_frame=1;mh_call_mod(&a,&c);h->gameplay_in_frame=0;assert(online.rc==BBM_ERR_STATE);h->session.in_session=0;
 memory.fail_write=1;Request fail={&a,p,1,-1,0,0};c.a=&fail;h->gameplay_in_frame=1;mh_call_mod(&a,&c);h->gameplay_in_frame=0;assert(fail.rc==BBM_ERR);memory.fail_write=0;
 mh_res_release_all(&a);mh_res_release_all(&b);h->mods[0].inst=h->mods[1].inst=NULL;modhost_destroy(h);
 puts("shared gameplay: fingerprints, samples, two observers/modifiers, ordered composition, stale/dead/offline guards, limits, failure, unload/reload: OK");return 0;
}
