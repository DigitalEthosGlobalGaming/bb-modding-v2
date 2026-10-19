/* Load the actual Rust DLL; deliver menu events through the real host. */
#include "native_menu.h"
#include "tcompat.h"
#include <assert.h>
typedef struct test_adapter {mh_menu_snapshot snapshot;bbm_menu_event event;int pending;} test_adapter;
static int publish(void*u,const mh_menu_snapshot*s){((test_adapter*)u)->snapshot=*s;return 1;}
static size_t drain(void*u,bbm_menu_event*out,size_t cap,uint64_t*drops){test_adapter*t=u;assert(cap);*drops=0;if(!t->pending)return 0;*out=t->event;t->pending=0;return 1;}
static mh_menu_item *find(test_adapter*t,const char*key){for(size_t i=0;i<t->snapshot.count;i++)if(!strcmp(t->snapshot.items[i].node.key,key))return &t->snapshot.items[i];return NULL;}
static void activate(test_adapter*t,const char*key){mh_menu_item*i=find(t,key);assert(i);t->event=(bbm_menu_event){.size=sizeof t->event,.kind=BBM_MENU_ACTIVATED,.node=i->id,.revision=i->version};t->pending=1;}
int main(int argc,char**argv){
    if(argc!=2)return 2;char root[300],mods[400],cache[400];tc_tmpdir(root,sizeof root,"bbmod-native-ui");
    snprintf(mods,sizeof mods,"%s/mods",root);snprintf(cache,sizeof cache,"%s/cache",root);
    tc_make_mod(mods,"example",argv[1],"[mod]\nid = \"example.native-ui\"\nabi = \"1.8\"\ndll = \"m.dll\"\n");
    modhost_config cfg={0};cfg.mods_dir=mods;cfg.cache_dir=cache;modhost*h=modhost_create(&cfg);assert(h);
    test_adapter*t=calloc(1,sizeof*t);mh_menu_sink sink={t,publish,drain,NULL,BBM_MENU_CAP_LISTS|BBM_MENU_CAP_SYSTEM};mh_menu_bind(h,&sink);
    assert(modhost_scan(h)==1&&h->mods[0].state==MODHOST_ACTIVE);modhost_frame(h);assert(t->snapshot.count==6);
    assert(find(t,"settings")&&find(t,"shortcut")&&find(t,"details"));
    activate(t,"dynamic");modhost_frame(h);modhost_frame(h);assert(find(t,"added")&&t->snapshot.count==7);
    activate(t,"dynamic");modhost_frame(h);modhost_frame(h);assert(!find(t,"added")&&t->snapshot.count==6);
    activate(t,"logging");modhost_frame(h);assert(!find(t,"logging")->node.value);
    bbm_menu_event stale=t->event;
    for(int i=0;i<20;i++){
        assert(!modhost_reload_now(h,"example.native-ui"));modhost_frame(h);assert(t->snapshot.count==6&&find(t,"logging")->node.value);
    }
    t->event=stale;t->pending=1;modhost_frame(h);assert(find(t,"logging")->node.value);
    assert(!modhost_unload_now(h,"example.native-ui"));modhost_frame(h);assert(t->snapshot.count==0);
    modhost_destroy(h);tc_rmtree(root);free(t);puts("actual native UI DLL: custom/nested screens, System link, dynamic add/remove, toggle, 20 reloads, stale selection and unload: OK");return 0;
}
