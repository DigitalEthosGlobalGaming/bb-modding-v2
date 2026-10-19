/* Real native-only example DLLs; no ImGui backend. Optional full converted
 * bundle checks coexistence beyond the old 128-node registry capacity. */
#include "native_menu.h"
#include "tcompat.h"
#include <assert.h>
#define OWNER "example.ui-examples"
typedef struct fixture {mh_menu_snapshot snapshot;bbm_menu_event event;int pending,cancelled;uint64_t active,sequence;bbm_text_request request;bbm_text_result result;} fixture;
static int publish(void*u,const mh_menu_snapshot*s){((fixture*)u)->snapshot=*s;return 1;}
static size_t drain(void*u,bbm_menu_event*out,size_t cap,uint64_t*drops){fixture*f=u;assert(cap);*drops=0;if(!f->pending)return 0;*out=f->event;f->pending=0;return 1;}
static bbm_status begin(void*u,const bbm_text_request*r,uint64_t*t){fixture*f=u;if(f->active)return BBM_ERR_CONFLICT;f->active=++f->sequence;*t=f->active;f->request=*r;memset(&f->result,0,sizeof f->result);f->result.size=sizeof f->result;return BBM_OK;}
static bbm_status poll(void*u,uint64_t t,bbm_text_result*r){fixture*f=u;if(f->active!=t)return BBM_ERR_NOTFOUND;*r=f->result;if(r->state)f->active=0;return BBM_OK;}
static void cancel(void*u,uint64_t t){fixture*f=u;if(f->active==t){f->active=0;f->cancelled++;}}
static bbm_status (*original_register)(bbm_handle,const bbm_menu_node*,bbm_id*);
static bbm_status denied_register(bbm_handle m,const bbm_menu_node*n,bbm_id*out){
    if(!strcmp(m->entry->m.id,OWNER)&&!strcmp(n->key,"style-1"))return BBM_ERR_BUDGET;
    return original_register(m,n,out);
}
static mh_menu_item *find(fixture*f,const char*owner,const char*key){for(size_t i=0;i<f->snapshot.count;i++)if(!strcmp(f->snapshot.items[i].owner,owner)&&!strcmp(f->snapshot.items[i].node.key,key))return &f->snapshot.items[i];return NULL;}
static mh_menu_item *row(fixture*f,const char*key){mh_menu_item*r=find(f,OWNER,key);assert(r);return r;}
static size_t count(fixture*f){size_t n=0;for(size_t i=0;i<f->snapshot.count;i++)n+=!strcmp(f->snapshot.items[i].owner,OWNER);return n;}
static void tick(modhost*h){modhost_frame(h);modhost_frame(h);}
static void click(modhost*h,fixture*f,const char*key){mh_menu_item*r=row(f,key);f->event=(bbm_menu_event){.size=sizeof f->event,.kind=BBM_MENU_ACTIVATED,.node=r->id,.revision=r->version};f->pending=1;tick(h);}
static void accepted(fixture*f){assert(!strcmp(row(f,"level")->node.label,"Level: 61"));assert(!strcmp(row(f,"style")->node.label,"Style: Orange"));assert(!row(f,"enabled")->node.value);assert(!strcmp(row(f,"overview")->node.label,"Unicode \xe2\x9c\x93"));}
static void make(const char*mods,const char*folder,const char*id,const char*dll,const char*abi){char manifest[300];snprintf(manifest,sizeof manifest,"[mod]\nid=\"%s\"\nabi=\"%s\"\ndll=\"m.dll\"\n",id,abi);tc_make_mod(mods,folder,dll,manifest);}
int main(int argc,char**argv){
    if(argc!=3&&argc!=9)return 2;
    char root[300],mods[400],cache[400];tc_tmpdir(root,sizeof root,"bbmod-native-ui-examples");
    snprintf(mods,sizeof mods,"%s/mods",root);snprintf(cache,sizeof cache,"%s/cache",root);
    make(mods,"ui",OWNER,argv[1],"1.10");make(mods,"counter","example.input-counter",argv[2],"1.9");
    if(argc==9){
        const char*ids[]={"example.input-monitor","example.hello-rust","example.live-patches","example.native-ui","example.hooks-observer","example.hooks-modifier"};
        for(int i=0;i<6;i++){char folder[32];snprintf(folder,sizeof folder,"extra-%d",i);make(mods,folder,ids[i],argv[i+3],i==3?"1.10":"1.9");}
    }
    fixture*f=calloc(1,sizeof*f);assert(f);modhost_config cfg={0};cfg.mods_dir=mods;cfg.cache_dir=cache;cfg.text=(modhost_text_backend){f,begin,poll,cancel};
    modhost*h=modhost_create(&cfg);assert(h);mh_menu_sink sink={f,publish,drain,NULL,BBM_MENU_CAP_LISTS|BBM_MENU_CAP_LIVE};mh_menu_bind(h,&sink);
    assert(modhost_scan(h)==(argc==9?8:2));tick(h);assert(count(f)==22);
    if(argc==9)assert(f->snapshot.count>128);
    for(size_t i=0;i<h->nmods;i++){assert(h->mods[i].state==MODHOST_ACTIVE);for(size_t j=0;j<h->mods[i].inst->nres;j++){mh_res_kind k=h->mods[i].inst->res[j].kind;assert(k!=MH_RES_PANEL&&k!=MH_RES_UI_CONTRIBUTION);}}
    bbm_id other=find(f,"example.input-counter","count")->id;
    assert(!strcmp(row(f,"dashboard-increment")->node.label,row(f,"settings-increment")->node.label));
    assert(row(f,"dashboard-increment")->id!=row(f,"settings-increment")->id);
    click(h,f,"dashboard-increment");click(h,f,"dashboard-increment");click(h,f,"settings-increment");
    assert(!strcmp(row(f,"dashboard-count")->node.label,"Dashboard counter: 2"));assert(!strcmp(row(f,"settings-count")->node.label,"Settings counter: 1"));
    click(h,f,"enabled");click(h,f,"style-2");click(h,f,"level-adjust-3");click(h,f,"level-adjust-2");
    click(h,f,"label");assert(f->active&&!strcmp(f->request.initial,"Shared UI example"));
    strcpy(f->result.text,"Unicode \xe2\x9c\x93");f->result.state=BBM_TEXT_ACCEPTED;tick(h);accepted(f);
    /* Remove settings while its editor is open, retain accepted values, and
     * reject a queued old settings action without touching another owner. */
    click(h,f,"label");assert(f->active);int cancelled=f->cancelled;
    mh_menu_item stale=*row(f,"settings-increment");click(h,f,"settings-visible");
    assert(count(f)==7&&!f->active&&f->cancelled==cancelled+1&&!find(f,OWNER,"settings"));
    f->event=(bbm_menu_event){.size=sizeof f->event,.kind=BBM_MENU_ACTIVATED,.node=stale.id,.revision=stale.version};f->pending=1;tick(h);
    assert(find(f,"example.input-counter","count")->id==other);
    click(h,f,"settings-visible");assert(count(f)==22);accepted(f);
    assert(!strcmp(row(f,"settings-count")->node.label,"Settings counter: 1"));
    for(int i=0;i<20;i++){
        stale=*row(f,"settings-increment");assert(!modhost_reload_now(h,OWNER));tick(h);accepted(f);assert(count(f)==22);
        f->event=(bbm_menu_event){.size=sizeof f->event,.kind=BBM_MENU_ACTIVATED,.node=stale.id,.revision=stale.version};f->pending=1;tick(h);
        assert(!strcmp(row(f,"settings-count")->node.label,"Settings counter: 1"));assert(find(f,"example.input-counter","count")->id==other);
    }
    click(h,f,"settings-visible");for(int i=0;i<20;i++){assert(!modhost_reload_now(h,OWNER));tick(h);assert(count(f)==7&&!row(f,"settings-visible")->node.value);}
    /* Failed partial registration must roll back every newly created row and
     * leave accepted values available for a later successful show. */
    original_register=h->api.menu_register;h->api.menu_register=denied_register;
    click(h,f,"settings-visible");assert(count(f)==7&&!row(f,"settings-visible")->node.value&&!find(f,OWNER,"level"));
    assert(strstr(row(f,"notice")->node.label,"Cannot register settings"));
    h->api.menu_register=original_register;
    click(h,f,"settings-visible");accepted(f);assert(count(f)==22);
    click(h,f,"reset");assert(!strcmp(row(f,"dashboard-count")->node.label,"Dashboard counter: 0")&&!strcmp(row(f,"settings-count")->node.label,"Settings counter: 0"));accepted(f);
    assert(!modhost_unload_now(h,OWNER));tick(h);assert(!count(f)&&find(f,"example.input-counter","count")->id==other);
    modhost_destroy(h);tc_rmtree(root);free(f);
    puts("native UI examples: all controls, independent counters, hide/show cancellation, value preservation, 40 reloads and bundle coexistence: OK");return 0;
}
