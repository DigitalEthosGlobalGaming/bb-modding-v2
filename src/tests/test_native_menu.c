/* Registry tests use the real host and an explicit synthetic adapter. */
#include "native_menu.h"
#include <assert.h>
_Static_assert(sizeof(bbm_menu_node)==852,"node ABI");
_Static_assert(sizeof(bbm_menu_state)==216,"state ABI");
_Static_assert(sizeof(bbm_menu_event)==440,"event ABI");
typedef struct fixture {mh_menu_snapshot copy;bbm_menu_event pending[8];size_t count;unsigned events[8];bbm_menu_event last;int busy;} fixture;
static int publish(void *u,const mh_menu_snapshot *s){fixture*f=u;if(f->busy)return 0;f->copy=*s;return 1;}
static size_t drain(void *u,bbm_menu_event*out,size_t cap,uint64_t*drops){fixture*f=u;*drops=0;assert(f->count<=cap);size_t n=f->count;memcpy(out,f->pending,n*sizeof*out);f->count=0;return n;}
static void event(bbm_ctx *ctx,const bbm_event *e){fixture*f=(void*)ctx;assert(e->type==BBM_EV_NATIVE_MENU&&e->data_len==sizeof(bbm_menu_event));f->last=*(const bbm_menu_event*)e->data;assert(f->last.kind<8);f->events[f->last.kind]++;}
static bbm_menu_node node(const char *key,uint32_t kind){bbm_menu_node n={.size=sizeof n,.kind=kind,.flags=BBM_MENU_ENABLED};strcpy(n.key,key);strcpy(n.parent,"game.system");strcpy(n.label,"Same label");return n;}
static void activate(fixture*f,mh_menu_item x){f->pending[f->count++]=(bbm_menu_event){.size=sizeof(bbm_menu_event),.kind=BBM_MENU_ACTIVATED,.node=x.id,.revision=x.version};}
int main(void){
    fixture *f=calloc(1,sizeof*f);static fixture aevents,bevents;assert(f);
    modhost_config cfg={0};cfg.cache_dir="native-menu-test-cache";cfg.mods_dir="native-menu-no-mods";
    modhost*h=modhost_create(&cfg);assert(h);h->mods=calloc(2,sizeof*h->mods);h->nmods=h->capmods=2;
    strcpy(h->mods[0].m.id,"z.mod");strcpy(h->mods[1].m.id,"a.mod");
    struct bbm_mod a={.host=h,.entry=&h->mods[0],.ctx=(void*)&aevents},b={.host=h,.entry=&h->mods[1],.ctx=(void*)&bevents};
    a.api.on_event=b.api.on_event=event;a.events=b.events=BBM_EV_NATIVE_MENU;
    h->mods[0].inst=&a;h->mods[1].inst=&b;h->mods[0].state=h->mods[1].state=MODHOST_ACTIVE;
    bbm_menu_state state;assert(!h->api.menu_state(&a,&state)&&state.capabilities==BBM_MENU_CAP_REGISTRY);
    mh_menu_sink sink={f,publish,drain,NULL,BBM_MENU_CAP_LISTS|BBM_MENU_CAP_SYSTEM};mh_menu_bind(h,&sink);
    bbm_menu_node n=node("same",BBM_MENU_ACTION);bbm_id aid,bid,tmp;
    assert(!h->api.menu_register(&a,&n,&aid));assert(!h->api.menu_register(&b,&n,&bid));
    assert(h->api.menu_register(&a,&n,&tmp)==BBM_ERR_CONFLICT);
    strcpy(n.key,"bad/key");assert(h->api.menu_register(&a,&n,&tmp)==BBM_ERR_ARG);
    strcpy(n.key,"bad");strcpy(n.parent,"game.inventory");assert(h->api.menu_register(&a,&n,&tmp)==BBM_ERR_ARG);
    strcpy(n.parent,"game.system");n.label[0]=(char)0xc0;n.label[1]=(char)0x80;n.label[2]=0;assert(h->api.menu_register(&a,&n,&tmp)==BBM_ERR_ARG);
    n=node("same",BBM_MENU_ACTION);f->busy=1;mh_menu_tick(h);assert(f->copy.count==0);f->busy=0;mh_menu_tick(h);
    assert(f->copy.count==2&&!strcmp(f->copy.items[0].owner,"a.mod"));
    uint64_t unchanged=f->copy.revision;
    n.label[sizeof n.label-1]='x'; /* semantically unused storage */
    assert(!h->api.menu_update(&a,aid,&n));mh_menu_tick(h);assert(f->copy.revision==unchanged);
    mh_menu_item old=f->copy.items[1];activate(f,old);mh_menu_tick(h);
    assert(aevents.events[BBM_MENU_ACTIVATED]==1&&bevents.events[BBM_MENU_ACTIVATED]==1&&!strcmp(aevents.last.owner,"z.mod"));
    n.flags=0;assert(!h->api.menu_update(&a,aid,&n));activate(f,old);mh_menu_tick(h);assert(aevents.events[BBM_MENU_ACTIVATED]==1);
    assert(h->api.menu_update(&b,aid,&n)==BBM_ERR_NOTFOUND);
    n=node("toggle",BBM_MENU_TOGGLE);assert(!h->api.menu_register(&b,&n,&tmp));mh_menu_tick(h);
    mh_menu_item toggle={0};for(size_t i=0;i<f->copy.count;i++)if(f->copy.items[i].id==tmp)toggle=f->copy.items[i];assert(toggle.id);
    activate(f,toggle);activate(f,toggle);mh_menu_tick(h);assert(aevents.events[BBM_MENU_VALUE_CHANGED]==1&&bevents.events[BBM_MENU_VALUE_CHANGED]==1);
    for(size_t i=0;i<f->copy.count;i++)if(f->copy.items[i].id==tmp)assert(f->copy.items[i].node.value==1);
    f->pending[f->count++]=(bbm_menu_event){.kind=BBM_MENU_OPENED,.generation=4};strcpy(f->pending[0].screen,"Inventory");
    mh_menu_tick(h);assert(!h->api.menu_state(&a,&state)&&state.generation==4&&!strcmp(state.screen,"Inventory"));
    mh_res_release_all(&a);activate(f,old);mh_menu_tick(h);assert(aevents.events[BBM_MENU_ACTIVATED]==1);
    /* Only a manager may replace the pause menu's Mods entry. */
    n=node("mods-root",BBM_MENU_ACTION);strcpy(n.parent,"game.system");
    assert(h->api.menu_register(&b,&n,&tmp)==BBM_ERR_STATE);
    h->mods[1].m.manage=1;assert(!h->api.menu_register(&b,&n,&tmp));
    n.kind=BBM_MENU_TOGGLE;assert(h->api.menu_register(&b,&n,&tmp)==BBM_ERR_STATE||h->api.menu_register(&b,&n,&tmp)==BBM_ERR_CONFLICT);
    mh_res_release_all(&b);h->mods[1].m.manage=0;mh_menu_tick(h);
    n=node("information",BBM_MENU_INFO);assert(!h->api.menu_register(&b,&n,&tmp));mh_menu_tick(h);
    mh_menu_item info={0};for(size_t i=0;i<f->copy.count;i++)if(f->copy.items[i].id==tmp)info=f->copy.items[i];assert(info.id);
    unsigned before=aevents.events[BBM_MENU_ACTIVATED];activate(f,info);mh_menu_tick(h);assert(aevents.events[BBM_MENU_ACTIVATED]==before);
    for(int i=0;i<20;i++){mh_res_release_all(&b);n=node("same",BBM_MENU_ACTION);assert(!h->api.menu_register(&b,&n,&bid));mh_menu_tick(h);assert(f->copy.count==1&&b.nres==1);}
    mh_res_release_all(&b);mh_menu_tick(h);assert(f->copy.count==0);
    /* All converted examples must fit together. Verify full publication, a
     * bounded failure at capacity, and independent owner recovery after unload. */
    for(size_t i=0;i<MH_MENU_LIMIT;i++){
        char label[64];snprintf(label,sizeof label,"capacity-%zu",i);
        n=node(label,BBM_MENU_INFO);assert(!h->api.menu_register(i<MH_MENU_LIMIT/2?&a:&b,&n,&tmp));
    }
    mh_menu_tick(h);assert(f->copy.count==MH_MENU_LIMIT);
    n=node("over-capacity",BBM_MENU_INFO);assert(h->api.menu_register(&a,&n,&tmp)==BBM_ERR_BUDGET);
    mh_res_release_all(&a);assert(!h->api.menu_register(&a,&n,&tmp));mh_menu_tick(h);
    assert(f->copy.count==MH_MENU_LIMIT/2+1&&b.nres==MH_MENU_LIMIT/2);
    mh_res_release_all(&a);mh_res_release_all(&b);h->mods[0].inst=h->mods[1].inst=NULL;
    modhost_destroy(h);free(f);puts("native menu registry: namespace, order, ownership, stale events, toggle fanout, busy retry, 20 reload cycles: OK");return 0;
}
