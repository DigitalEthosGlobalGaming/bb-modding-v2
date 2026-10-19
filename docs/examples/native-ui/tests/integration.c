/* Load the actual Rust DLL; deliver menu events through the real host. */
#include "native_menu.h"
#include "tcompat.h"
#include <assert.h>
typedef struct test_adapter {mh_menu_snapshot snapshot;bbm_menu_event event;int pending;uint64_t active,sequence;int cancelled;bbm_text_request text_request;bbm_text_result text_result;} test_adapter;
static bbm_status text_begin(void*u,const bbm_text_request*r,uint64_t*t){test_adapter*a=u;if(a->active)return BBM_ERR_CONFLICT;a->text_request=*r;a->active=++a->sequence;*t=a->active;memset(&a->text_result,0,sizeof a->text_result);a->text_result.size=sizeof a->text_result;return BBM_OK;}
static bbm_status text_poll(void*u,uint64_t t,bbm_text_result*r){test_adapter*a=u;if(t!=a->active)return BBM_ERR_NOTFOUND;*r=a->text_result;if(r->state)a->active=0;return BBM_OK;}
static void text_cancel(void*u,uint64_t t){test_adapter*a=u;if(t==a->active){a->active=0;a->cancelled++;}}
static int publish(void*u,const mh_menu_snapshot*s){((test_adapter*)u)->snapshot=*s;return 1;}
static size_t drain(void*u,bbm_menu_event*out,size_t cap,uint64_t*drops){test_adapter*t=u;assert(cap);*drops=0;if(!t->pending)return 0;*out=t->event;t->pending=0;return 1;}
static mh_menu_item *find(test_adapter*t,const char*key){for(size_t i=0;i<t->snapshot.count;i++)if(!strcmp(t->snapshot.items[i].node.key,key))return &t->snapshot.items[i];return NULL;}
static void activate(test_adapter*t,const char*key){mh_menu_item*i=find(t,key);assert(i);t->event=(bbm_menu_event){.size=sizeof t->event,.kind=BBM_MENU_ACTIVATED,.node=i->id,.revision=i->version};t->pending=1;}
int main(int argc,char**argv){
    if(argc!=2)return 2;char root[300],mods[400],cache[400];tc_tmpdir(root,sizeof root,"bbmod-native-ui");
    snprintf(mods,sizeof mods,"%s/mods",root);snprintf(cache,sizeof cache,"%s/cache",root);
    tc_make_mod(mods,"example",argv[1],"[mod]\nid = \"example.native-ui\"\nabi = \"1.10\"\ndll = \"m.dll\"\n");
    modhost_config cfg={0};cfg.mods_dir=mods;cfg.cache_dir=cache;modhost*h=modhost_create(&cfg);assert(h);
    test_adapter*t=calloc(1,sizeof*t);mh_menu_sink sink={t,publish,drain,NULL,BBM_MENU_CAP_LISTS|BBM_MENU_CAP_SYSTEM};mh_menu_bind(h,&sink);
    h->cfg.text=(modhost_text_backend){t,text_begin,text_poll,text_cancel};
    assert(modhost_scan(h)==1&&h->mods[0].state==MODHOST_ACTIVE);modhost_frame(h);assert(t->snapshot.count==17);
    assert(find(t,"settings")&&find(t,"shortcut")&&find(t,"details"));
    activate(t,"style-2");modhost_frame(h);modhost_frame(h);
    assert(!strcmp(find(t,"style")->node.label,"Example style: Orange"));
    assert(!strcmp(find(t,"style-2")->node.label,"Selected: Orange")&&!strcmp(find(t,"style-0")->node.label,"Neutral"));
    activate(t,"level-adjust-2");modhost_frame(h);modhost_frame(h);
    assert(!strcmp(find(t,"level-value")->node.label,"Example level: 51"));
    for(int i=0;i<6;i++){activate(t,"level-adjust-3");modhost_frame(h);modhost_frame(h);}
    assert(!strcmp(find(t,"level")->node.label,"Example level: 100"));
    assert(!(find(t,"level-adjust-3")->node.flags&BBM_MENU_ENABLED));
    for(int i=0;i<10;i++){activate(t,"level-adjust-0");modhost_frame(h);modhost_frame(h);}
    assert(!strcmp(find(t,"level")->node.label,"Example level: 0"));
    assert(!(find(t,"level-adjust-0")->node.flags&BBM_MENU_ENABLED));
    activate(t,"label");modhost_frame(h);modhost_frame(h);
    assert(t->active&&!strcmp(t->text_request.initial,"Native example")&&!(find(t,"label")->node.flags&BBM_MENU_ENABLED));
    strcpy(t->text_result.text,"Unicode \xe2\x9c\x93");t->text_result.state=BBM_TEXT_ACCEPTED;
    modhost_frame(h);modhost_frame(h);
    assert(!t->active&&!strcmp(find(t,"label")->node.label,"Example label: Unicode \xe2\x9c\x93")&&(find(t,"label")->node.flags&BBM_MENU_ENABLED));
    activate(t,"label");modhost_frame(h);t->text_result.state=BBM_TEXT_CANCELLED;
    modhost_frame(h);modhost_frame(h);assert(strstr(find(t,"label")->node.label,"Unicode")&&strstr(find(t,"label")->node.help,"cancelled"));
    t->active=++t->sequence;activate(t,"label");modhost_frame(h);modhost_frame(h);
    assert(strstr(find(t,"label")->node.help,"Cannot open")&&(find(t,"label")->node.flags&BBM_MENU_ENABLED));t->active=0;
    uint32_t full_size=h->api.size;h->api.size=(uint32_t)offsetof(bbm_host,text_begin);
    activate(t,"label");modhost_frame(h);modhost_frame(h);assert(!t->active&&strstr(find(t,"label")->node.help,"Cannot open"));h->api.size=full_size;
    activate(t,"dynamic");modhost_frame(h);modhost_frame(h);assert(find(t,"added")&&t->snapshot.count==18);
    activate(t,"dynamic");modhost_frame(h);modhost_frame(h);assert(!find(t,"added")&&t->snapshot.count==17);
    activate(t,"logging");modhost_frame(h);assert(!find(t,"logging")->node.value);
    bbm_menu_event stale=t->event;
    for(int i=0;i<20;i++){
        activate(t,"label");modhost_frame(h);assert(t->active);int cancelled=t->cancelled;
        assert(!modhost_reload_now(h,"example.native-ui"));modhost_frame(h);assert(!t->active&&t->cancelled==cancelled+1);
        assert(t->snapshot.count==17&&find(t,"logging")->node.value);
        assert(!strcmp(find(t,"style")->node.label,"Example style: Neutral")&&!strcmp(find(t,"level")->node.label,"Example level: 50"));
    }
    t->event=stale;t->pending=1;modhost_frame(h);assert(find(t,"logging")->node.value);
    assert(!modhost_unload_now(h,"example.native-ui"));modhost_frame(h);assert(t->snapshot.count==0);
    modhost_destroy(h);tc_rmtree(root);free(t);puts("actual native UI DLL: custom/nested screens, System link, dynamic add/remove, toggle, 20 reloads, stale selection and unload: OK");return 0;
}
