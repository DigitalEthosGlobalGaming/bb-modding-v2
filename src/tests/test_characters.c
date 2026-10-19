/* Offline real host-service tests with a synthetic guest image. */
#include "internal.h"
#include "characters.h"
#include "character_profile.h"
#include <assert.h>
#define BASE ((uintptr_t)0x10000000)
#define CHAR_IMAGE_SIZE 0x6000000
#define WORLD (BASE+0x5600000)
#define SETS (BASE+0x5610000)
#define ENTRIES (BASE+0x5620000)
#define INFO (BASE+0x5630000)
#define ACTOR(i) (BASE+0x5700000+(uintptr_t)(i)*0x1000)
typedef struct{unsigned char*b;unsigned reads;uintptr_t fail;}Memory;
static void put(Memory*m,uintptr_t at,const void*p,size_t n){assert(at>=BASE&&at-BASE+n<=CHAR_IMAGE_SIZE);memcpy(m->b+at-BASE,p,n);}
static void p64(Memory*m,uintptr_t at,uintptr_t value){put(m,at,&value,8);}
static void p32(Memory*m,uintptr_t at,uint32_t value){put(m,at,&value,4);}
static int read_mem(void*u,uintptr_t at,void*p,size_t n){Memory*m=u;m->reads++;if(at<BASE||at-BASE>CHAR_IMAGE_SIZE||n>CHAR_IMAGE_SIZE-(at-BASE)||(m->fail&&at<=m->fail&&m->fail-at<n))return -1;memcpy(p,m->b+at-BASE,n);return 0;}
static int read_host(void*u,const void*at,void*p,size_t n){return read_mem(u,(uintptr_t)at,p,n);}
static void fixture(Memory*m){
 m->b=calloc(1,CHAR_IMAGE_SIZE);assert(m->b);
 for(size_t i=0;i<sizeof character_checks/sizeof character_checks[0];i++)put(m,BASE+character_checks[i].rva,character_checks[i].bytes,character_checks[i].n);
 p64(m,BASE+0x553e878,WORLD);p64(m,WORLD+0x60,ACTOR(0));p64(m,WORLD+8,INFO);p32(m,INFO+0x18,0);
 for(unsigned s=0;s<2;s++){p64(m,WORLD+0x850+s*8,SETS+s*0x20);p32(m,SETS+s*0x20,s?35:1);p64(m,SETS+s*0x20+8,ENTRIES+s*0x1000);}
 for(unsigned i=0;i<36;i++){
  uintptr_t a=ACTOR(i),slot=i?ENTRIES+0x1000+(i-1)*0x38:ENTRIES;
  p64(m,slot,a);p64(m,a,BASE+(i?0x538ebc0:0x100));p32(m,a+8,i?(1<<14)|(i-1):0);p64(m,a+0x18,slot);
  p64(m,a+0x3b0,a+0x400);p64(m,a+0x420,a+0x800);p64(m,a+0x808,a);
  p32(m,a+0x8f8,1000);p32(m,a+0x8fc,1000);p32(m,a+0x88,6);
  p64(m,a+0x440,a+0xc00);p64(m,a+0xc00,BASE+0x53357e0);p64(m,a+0xc08,a);
  p64(m,a+0x458,a+0xc20);p64(m,a+0xc20,BASE+0x53358b0);p64(m,a+0xc28,a);p32(m,a+0xc40,7);
  p64(m,a+0x3c8,a+0xc80);p32(m,a+0xc80,50000000+i);p64(m,a+0xc88,a+0xc90);p32(m,a+0xc98,500000);
 }
}
typedef struct{unsigned count;bbm_character_summary summary;}Observer;
static void event(bbm_ctx*c,const bbm_event*e){Observer*o=(Observer*)c;assert(e->type==BBM_EV_CHARACTERS_CHANGED&&e->data_len==sizeof o->summary);memcpy(&o->summary,e->data,sizeof o->summary);o->count++;}
int main(void){
 _Static_assert(sizeof(bbm_character)==56,"ABI character");_Static_assert(sizeof(bbm_character_page)==1832,"ABI page");
 Memory m={0};fixture(&m);mh_character_sample*sample=calloc(1,sizeof *sample);assert(sample);
 assert(mh_characters_profile(BASE,read_mem,&m));assert(mh_characters_read(BASE,read_mem,&m,sample)==BBM_OK&&sample->count==36);
 assert(sample->entries[0].value.kind==1&&sample->entries[1].value.kind==3&&sample->entries[1].value.name_id==500000);
 assert(sample->entries[1].value.flags==31&&sample->entries[1].value.script_value==7);
 p32(&m,SETS+0x20,4097);assert(mh_characters_read(BASE,read_mem,&m,sample)==BBM_ERR_BUDGET&&sample->count==0);p32(&m,SETS+0x20,35);
 p32(&m,ACTOR(1)+8,0);assert(mh_characters_read(BASE,read_mem,&m,sample)!=BBM_OK);p32(&m,ACTOR(1)+8,1<<14);
 p64(&m,ACTOR(1)+0x808,ACTOR(2));assert(mh_characters_read(BASE,read_mem,&m,sample)!=BBM_OK);p64(&m,ACTOR(1)+0x808,ACTOR(1));
 p32(&m,ACTOR(1)+0x8fc,0);assert(!mh_characters_read(BASE,read_mem,&m,sample)&&!(sample->entries[1].value.flags&BBM_CHARACTER_HEALTH));p32(&m,ACTOR(1)+0x8fc,1000);
 m.fail=ACTOR(1)+8;assert(mh_characters_read(BASE,read_mem,&m,sample)!=BBM_OK);m.fail=0;free(sample);
 modhost_config cfg={0};cfg.mods_dir="characters-test-no-mods";cfg.cache_dir="characters-test-cache";cfg.eboot_base=BASE;cfg.eboot_size=CHAR_IMAGE_SIZE;cfg.platform.user=&m;cfg.platform.read_safe=read_host;
 modhost*h=modhost_create(&cfg);assert(h);unsigned reads=m.reads;mh_characters_tick(h);assert(!h->characters&&reads==m.reads);
 h->mods=calloc(2,sizeof*h->mods);h->nmods=h->capmods=2;Observer oa={0},ob={0};
 struct bbm_mod a={.host=h,.entry=&h->mods[0],.ctx=(bbm_ctx*)&oa},b={.host=h,.entry=&h->mods[1],.ctx=(bbm_ctx*)&ob};
 h->mods[0].inst=&a;h->mods[1].inst=&b;h->mods[0].state=h->mods[1].state=MODHOST_ACTIVE;a.events=b.events=BBM_EV_CHARACTERS_CHANGED;a.api.on_event=b.api.on_event=event;
 mh_characters_tick(h);assert(oa.count==1&&ob.count==1&&oa.summary.count==36&&!memcmp(&oa.summary,&ob.summary,sizeof oa.summary));
 bbm_character_page p;assert(!h->api.characters_page(&a,0,0,32,&p)&&p.count==32);uint64_t rev=p.snapshot.revision,token=p.entries[1].token;
 reads=m.reads;assert(!h->api.characters_page(&b,rev,32,32,&p)&&p.count==4&&reads==m.reads);
 assert(h->api.characters_page(&a,rev,37,32,&p)==BBM_ERR_ARG);assert(h->api.characters_page(&a,rev,0,33,&p)==BBM_ERR_ARG);
 p32(&m,ACTOR(1)+0x8f8,0);plat_sleep_ms(260);mh_characters_tick(h);assert(oa.count==2&&ob.count==2);
 assert(h->api.characters_page(&a,rev,0,32,&p)==BBM_ERR_STATE);assert(!h->api.characters_page(&a,0,0,32,&p)&&p.entries[1].token==token&&p.entries[1].health==0);
 uint64_t epoch=p.snapshot.epoch;p64(&m,BASE+0x553e878,0);plat_sleep_ms(260);mh_characters_tick(h);assert(!oa.summary.flags&&oa.summary.count==0);
 assert(h->api.characters_page(&a,0,0,32,&p)==BBM_ERR_NOTFOUND);
 p64(&m,BASE+0x553e878,WORLD);plat_sleep_ms(260);mh_characters_tick(h);assert(!h->api.characters_page(&a,0,0,32,&p)&&p.entries[1].token!=token&&p.snapshot.epoch!=epoch);
 m.b[character_checks[0].rva]^=1;plat_sleep_ms(260);mh_characters_tick(h);assert(h->api.characters_page(&a,0,0,32,&p)==BBM_ERR_UNSUPPORTED);
 a.events=b.events=0;reads=m.reads;plat_sleep_ms(260);mh_characters_tick(h);assert(reads==m.reads);
 h->mods[0].inst=h->mods[1].inst=NULL;modhost_destroy(h);free(m.b);
 puts("characters: bounds/identity, optional fields, two subscribers, cached pages, HP zero, unload/recovery epochs, profile rejection, idle: OK");return 0;
}
