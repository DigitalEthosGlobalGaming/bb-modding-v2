/* Actual converted DLLs, real host, copied native snapshots. No ImGui backend. */
#include "native_menu.h"
#include "tcompat.h"
#include <assert.h>
typedef struct fixture {mh_menu_snapshot snapshot;uint32_t buttons;} fixture;
static int publish(void*u,const mh_menu_snapshot*s){((fixture*)u)->snapshot=*s;return 1;}
static uint32_t buttons(void*u){return ((fixture*)u)->buttons;}
static mh_menu_item *find(fixture*f,const char*owner,const char*key){
    for(size_t i=0;i<f->snapshot.count;i++)if(!strcmp(f->snapshot.items[i].owner,owner)&&!strcmp(f->snapshot.items[i].node.key,key))return &f->snapshot.items[i];
    return NULL;
}
static void check(fixture*f){
    assert(f->snapshot.count==6);
    mh_menu_item *count=find(f,"example.input-counter","count"),*monitor=find(f,"example.input-monitor","buttons");
    assert(count&&monitor&&count->node.kind==BBM_MENU_INFO&&monitor->node.kind==BBM_MENU_INFO);
    assert(!strcmp(count->node.label,"Button presses: 2"));
    assert(!strcmp(monitor->node.label,"Observed buttons: 0x00000030"));
}
int main(int argc,char**argv){
    if(argc!=4)return 2;
    char root[300],mods[400],cache[400];tc_tmpdir(root,sizeof root,"bbmod-native-input");
    snprintf(mods,sizeof mods,"%s/mods",root);snprintf(cache,sizeof cache,"%s/cache",root);
    tc_make_mod(mods,"counter",argv[1],"[mod]\nid=\"example.input-counter\"\nabi=\"1.9\"\ndll=\"m.dll\"\n");
    tc_make_mod(mods,"monitor",argv[2],"[mod]\nid=\"example.input-monitor\"\nabi=\"1.9\"\ndll=\"m.dll\"\n");
    tc_make_mod(mods,"hello",argv[3],"[mod]\nid=\"example.hello-rust\"\nabi=\"1.9\"\ndll=\"m.dll\"\n");
    fixture*f=calloc(1,sizeof*f);assert(f);
    modhost_config cfg={0};cfg.mods_dir=mods;cfg.cache_dir=cache;cfg.input.user=f;cfg.input.pad_buttons=buttons;
    modhost*h=modhost_create(&cfg);assert(h);
    mh_menu_sink sink={f,publish,NULL,NULL,BBM_MENU_CAP_LISTS|BBM_MENU_CAP_LIVE};mh_menu_bind(h,&sink);
    assert(modhost_scan(h)==3);modhost_frame(h);
    for(size_t i=0;i<h->nmods;i++){
        assert(h->mods[i].state==MODHOST_ACTIVE);
        struct bbm_mod*m=h->mods[i].inst;
        for(size_t j=0;j<m->nres;j++)assert(m->res[j].kind!=MH_RES_PANEL&&m->res[j].kind!=MH_RES_UI_CONTRIBUTION);
    }
    f->buttons=0x30;modhost_frame(h);modhost_frame(h);check(f);
    bbm_id monitor=find(f,"example.input-monitor","buttons")->id;
    uint64_t revision=f->snapshot.revision;modhost_frame(h);assert(f->snapshot.revision==revision);
    for(int i=0;i<20;i++){
        assert(!modhost_reload_now(h,"example.input-counter"));modhost_frame(h);check(f);
        assert(find(f,"example.input-monitor","buttons")->id==monitor);
    }
    for(int i=0;i<20;i++){
        const char *prefix="Sync points seen: ";
        mh_menu_item *hello=find(f,"example.hello-rust","frames");assert(hello);
        unsigned long long before=strtoull(hello->node.label+strlen(prefix),NULL,10);
        assert(!modhost_reload_now(h,"example.hello-rust"));modhost_frame(h);
        hello=find(f,"example.hello-rust","frames");assert(hello);
        assert(strtoull(hello->node.label+strlen(prefix),NULL,10)>before);check(f);
    }
    f->buttons=0;modhost_frame(h);modhost_frame(h);
    assert(!strcmp(find(f,"example.input-counter","count")->node.label,"Button presses: 2"));
    assert(!strcmp(find(f,"example.input-monitor","buttons")->node.label,"Observed buttons: 0x00000000"));
    assert(!modhost_unload_now(h,"example.input-counter"));modhost_frame(h);assert(f->snapshot.count==4);
    assert(find(f,"example.input-monitor","buttons")->id==monitor);
    assert(!modhost_unload_now(h,"example.input-monitor"));modhost_frame(h);assert(f->snapshot.count==2);
    assert(!modhost_unload_now(h,"example.hello-rust"));modhost_frame(h);assert(!f->snapshot.count);
    modhost_destroy(h);tc_rmtree(root);free(f);
    puts("native input DLLs: native-only registration, shared press/release values, 20 state-preserving reloads, owner isolation and cleanup: OK");return 0;
}
