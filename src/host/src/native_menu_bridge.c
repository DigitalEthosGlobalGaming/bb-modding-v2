/* SPDX-License-Identifier: GPL-2.0-or-later
 * Experimental CUSA03173 1.09 adapter. Only host-owned code and copied payloads
 * enter the native UI. A native screen is a frozen view; no DLL pointers live
 * in the engine. Opt in with BB_MODS_NATIVE_UI=1 after building the asset layer.
 */
#include "native_menu.h"
#include <ctype.h>
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#define G __attribute__((sysv_abi))
typedef void *(G *fn3)(void*,void*,void*);
typedef struct row_data {
    uint32_t refs;
    uintptr_t context;
    mh_menu_item item;
    uint16_t label[192],help[256];
} row_data;
typedef struct callable { void **vt; row_data *data; } callable;
typedef struct live_view {
    uintptr_t widget;
    const uint16_t *name;
    void *context;
    char path[192];
    uint64_t revision,content_hash,ids[MH_MENU_VISIBLE_ROWS];
    size_t count,page;
    int reset_focus;
} live_view;
static int request_page(void*,int);
static struct {
    uintptr_t base;
    char asset[MH_PATH],list_asset[MH_PATH];
    uint32_t busy, enabled, used, live;
    uint64_t drops,generation;
    mh_menu_snapshot snapshot;
    bbm_menu_event events[MH_MENU_QUEUE];size_t count;
    struct { uintptr_t step; char name[192]; } names[128];size_t next_name;
    uintptr_t active;
    struct {uintptr_t address;uint64_t token,generation;} controls[64];
    size_t next_control;uint64_t control_seq;
    struct bbm_mod owner;mh_entry entry;
    mh_hook *hooks[5];void *originals[5];
    live_view views[64];size_t next_view;
    /* Interned engine-visible names/titles have process lifetime. Deduplicated
     * and bounded, including across opens and mod reloads; no mod allocation. */
    uint16_t words[512][192];size_t nwords;
} bridge;
static int enter(void){uint32_t zero=0;return __atomic_compare_exchange_n(&bridge.busy,&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);}
static void leave(void){__atomic_store_n(&bridge.busy,0,__ATOMIC_RELEASE);}
static void wide(const char *s,uint16_t *out,size_t cap){
    size_t n=0;while(*s&&n+1<cap){unsigned c=(unsigned char)*s++;int k=0;
        if(c>=0xf0){c&=7;k=3;}else if(c>=0xe0){c&=15;k=2;}else if(c>=0xc0){c&=31;k=1;}
        while(k--&&*s)c=(c<<6)|((unsigned char)*s++&63);
        if(c>0xffff){if(n+2>=cap)break;c-=0x10000;out[n++]=(uint16_t)(0xd800+(c>>10));out[n++]=(uint16_t)(0xdc00+(c&1023));}
        else out[n++]=(uint16_t)c;
    }out[n]=0;
}
static const uint16_t *intern(const char *s){
    uint16_t w[192];wide(s,w,192);size_t len=0;while(w[len])len++;
    if(!enter())return NULL;
    for(size_t i=0;i<bridge.nwords;i++)if(!memcmp(bridge.words[i],w,(len+1)*2)){leave();return bridge.words[i];}
    if(bridge.nwords==512){leave();return NULL;}
    uint16_t *out=bridge.words[bridge.nwords++];memcpy(out,w,(len+1)*2);leave();return out;
}
static void enqueue(bbm_menu_event ev){
    if(!__atomic_load_n(&bridge.enabled,__ATOMIC_ACQUIRE))return;
    if(!enter()){__atomic_fetch_add(&bridge.drops,1,__ATOMIC_RELAXED);return;}
    if(bridge.count<MH_MENU_QUEUE){ev.size=sizeof ev;ev.generation=bridge.generation;bridge.events[bridge.count++]=ev;}
    else __atomic_fetch_add(&bridge.drops,1,__ATOMIC_RELAXED);
    leave();
}
static int publish(void *u,const mh_menu_snapshot *s){(void)u;if(!enter())return 0;bridge.snapshot=*s;leave();return 1;}
static size_t drain(void *u,bbm_menu_event *out,size_t cap,uint64_t *drops){
    (void)u;*drops=__atomic_exchange_n(&bridge.drops,0,__ATOMIC_RELAXED);
    if(!enter())return 0;
    size_t n=bridge.count<cap?bridge.count:cap;memcpy(out,bridge.events,n*sizeof *out);
    memmove(bridge.events,bridge.events+n,(bridge.count-n)*sizeof *out);bridge.count-=n;leave();return n;
}
static int current(const mh_menu_item *item){
    if(!__atomic_load_n(&bridge.enabled,__ATOMIC_ACQUIRE)||!enter())return 0;
    int found=item->id==0;
    for(size_t i=0;i<bridge.snapshot.count&&!found;i++){
        const mh_menu_item *p=&bridge.snapshot.items[i];
        found=p->id==item->id&&p->version==item->version&&(p->node.flags&BBM_MENU_ENABLED);
    }leave();return found;
}
static void *G open_screen(void *out,void *ctx,const char *path);
static void *G clone(callable *self,callable *to){
    if(!to)to=calloc(1,sizeof *to);if(!to)return NULL;
    *to=*self;__atomic_add_fetch(&to->data->refs,1,__ATOMIC_RELAXED);return to;
}
static void drop_data(callable *self){if(self->data&&__atomic_sub_fetch(&self->data->refs,1,__ATOMIC_ACQ_REL)==0)free(self->data);self->data=NULL;}
static void G destroy(callable *self,int heap){drop_data(self);if(heap)free(self);}
static void G destruct(callable *self){drop_data(self);}
static uintptr_t G unused(void){return 0;}
static void *G invoke(void *out,callable *self,void *dialog){
    *(void**)out=NULL;
    if(!current(&self->data->item))return out;
    const mh_menu_item *item=&self->data->item;
    if(!item->id&&item->node.kind==BBM_MENU_ACTION){
        if(!strcmp(item->node.target,"__bbmod.page/next")){request_page(dialog,1);return out;}
        if(!strcmp(item->node.target,"__bbmod.page/previous")){request_page(dialog,-1);return out;}
    }
    if(item->node.kind==BBM_MENU_INFO)return out;
    if(item->node.kind==BBM_MENU_SCREEN||item->node.kind==BBM_MENU_LINK)
        return open_screen(out,(unsigned char*)dialog+0x48,item->node.kind==BBM_MENU_SCREEN?item->path:item->node.target);
    bbm_menu_event ev={.kind=BBM_MENU_ACTIVATED,.node=item->id,.revision=item->version};
    mh_strlcpy(ev.screen,item->node.parent,sizeof ev.screen);enqueue(ev);
    return out;
}
static void *callable_vtable[]={ (void*)clone,(void*)clone,(void*)invoke,(void*)unused,(void*)destroy,(void*)destruct };

typedef struct native_ops {
    void *(G *init)(void*,const int32_t*);
    void *(G *append)(void*,void*,void*,void*);
    fn3 finalize;
    void (G *cleanup)(void*);
    void (G *caption)(void*,int32_t,int32_t);
    void *(G *factory)(void*,void*,void*);
    void (G *copy)(void*,const void*);
    void (G *refresh)(void*,void*);
    void (G *selection)(void*);
} native_ops;
static native_ops ops;
static void free_caption(unsigned char *ws){
    uint64_t cap=0,ptr=0,allocator=0;memcpy(&cap,ws+24,8);
    if(cap<8)return;memcpy(&ptr,ws,8);memcpy(&allocator,ws+32,8);
    if(ptr&&allocator){uintptr_t vt=*(uintptr_t*)allocator;
        ((void(G*)(void*,void*))(*(uintptr_t*)(vt+0x70)))((void*)allocator,(void*)ptr);}
}
static int append(void *builder,const mh_menu_item *item){
    row_data *data=calloc(1,sizeof *data);if(!data)return 0;
    data->refs=1;data->item=*item;
    char label[160];snprintf(label,sizeof label,"%s%s",item->node.label,
        item->node.kind==BBM_MENU_TOGGLE?(item->node.value?": On":": Off"):"");
    wide(label,data->label,192);wide(item->node.help,data->help,256);
    struct {callable object;uintptr_t padding[2];void *self;} f={.object={callable_vtable,data}};f.self=&f.object;
    _Alignas(16) unsigned char captions[0x80]={0};
    /* A real caption initializes the game's allocator and flags. Only the raw
     * drawing pointers are replaced; native copies own their wstring storage. */
    ops.caption(captions,200,101001);ops.caption(captions+0x40,201,101001);
    memcpy(captions,&(const uint16_t*){data->label},8);
    memcpy(captions+0x40,&(const uint16_t*){data->help},8);
    uint64_t flag=0;ops.append(builder,captions,&f,&flag);
    free_caption(captions+8);free_caption(captions+0x48);drop_data(&f.object);return 1;
}
static size_t items_at_revision(const char *path,mh_menu_item *items,size_t cap,uint64_t *revision){
    if(!enter())return 0;
    if(revision)*revision=bridge.snapshot.revision;
    size_t n=0;for(size_t i=0;i<bridge.snapshot.count&&n<cap;i++){
        mh_menu_item *p=&bridge.snapshot.items[i];
        if((p->node.flags&BBM_MENU_ENABLED)&&!strcmp(p->node.parent,path))items[n++]=*p;
    }
    if(!strcmp(path,"game.mods"))for(size_t i=0;i<bridge.snapshot.mod_count&&n<cap;i++){
        int configured=0;
        for(size_t j=0;j<n;j++)if(!strcmp(items[j].owner,bridge.snapshot.mods[i].id)){configured=1;break;}
        if(configured)continue;
        mh_menu_item *p=&items[n++];memset(p,0,sizeof *p);p->node.kind=BBM_MENU_LINK;p->node.flags=BBM_MENU_ENABLED;
        static const char *states[]={"Found","Loaded","Unloaded","Faulted","Disabled"};
        unsigned state=bridge.snapshot.mods[i].state;
        snprintf(p->node.label,sizeof p->node.label,"%.103s [%s]",bridge.snapshot.mods[i].name,state<5?states[state]:"Unknown");
        snprintf(p->node.target,sizeof p->node.target,"__bbmod.info/%s",bridge.snapshot.mods[i].id);
    }
    leave();return n;
}
static size_t items_for(const char *path,mh_menu_item *items,size_t cap){return items_at_revision(path,items,cap,NULL);}
#include "native_menu_live.inc"
static void *G open_screen(void *out,void *ctx,const char *path){
    *(void**)out=NULL;
    if(!__atomic_load_n(&bridge.enabled,__ATOMIC_ACQUIRE))return out;
    mh_menu_item *items=calloc(MH_MENU_LIMIT,sizeof *items);if(!items)return out;
    size_t count=items_for(path,items,MH_MENU_LIMIT);
    if(bridge.live){size_t page=0;count=page_items(items,count,&page);}
    const uint16_t *name=intern(path);if(!name){free(items);return out;}
    _Alignas(16) unsigned char builder[0x1190]={0};int32_t type=3;
    ops.init(builder,&type);memcpy(builder+8,&name,8);memcpy(builder+0x1180,&ctx,8);
    for(size_t i=0;i<count;i++)append(builder,&items[i]);
    if(!count){mh_menu_item empty={0};empty.node.kind=BBM_MENU_ACTION;
        mh_strlcpy(empty.node.label,"No settings available",sizeof empty.node.label);append(builder,&empty);}
    /* System's dialog factory: engine-owned clone/invoke/destruct functions.
     * This is a new list instance, not the settings screen or an ImGui panel. */
    if(!bridge.live){
        _Alignas(16) uintptr_t stock[5]={bridge.base+0x5343790,0,0,0,0};stock[4]=(uintptr_t)stock;
        ops.finalize(out,builder,stock);ops.cleanup(builder);free(items);return out;
    }
    row_data *data=calloc(1,sizeof *data);
    if(!data){ops.cleanup(builder);free(items);return out;}
    data->refs=1;data->context=(uintptr_t)ctx;mh_strlcpy(data->item.path,path,sizeof data->item.path);
    struct {callable object;uintptr_t padding[2];void *self;} factory={.object={factory_vtable,data}};
    factory.self=&factory.object;
    ops.finalize(out,builder,&factory);drop_data(&factory.object);ops.cleanup(builder);free(items);return out;
}
static void *G blank_detour(void *builder){
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    if(caller==bridge.base+0x1bde91d&&bridge.asset[0]&&__atomic_load_n(&bridge.enabled,__ATOMIC_ACQUIRE)){
        mh_menu_item root={0};root.node.kind=BBM_MENU_LINK;root.node.flags=BBM_MENU_ENABLED;
        mh_strlcpy(root.node.label,"Mods",sizeof root.node.label);
        mh_strlcpy(root.node.help,"Installed mods and their settings",sizeof root.node.help);
        mh_strlcpy(root.node.target,"game.mods",sizeof root.node.target);
        if(append(builder,&root))return builder;
    }
    return ((void*(G*)(void*))bridge.originals[0])(builder);
}
static void *G finalizer_detour(void *out,void *builder,void *factory){
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    if(caller==bridge.base+0x1bb497d&&__atomic_load_n(&bridge.enabled,__ATOMIC_ACQUIRE)){
        mh_menu_item root={0};root.node.kind=BBM_MENU_LINK;root.node.flags=BBM_MENU_ENABLED;
        mh_strlcpy(root.node.label,"Mods",sizeof root.node.label);
        mh_strlcpy(root.node.help,"Installed mods and their settings",sizeof root.node.help);
        mh_strlcpy(root.node.target,"game.mods",sizeof root.node.target);append(builder,&root);
        mh_menu_item *items=calloc(MH_MENU_LIMIT,sizeof *items);
        if(items){size_t n=items_for("game.system",items,MH_MENU_LIMIT);for(size_t i=0;i<n;i++)append(builder,&items[i]);free(items);}
    }
    return ((fn3)bridge.originals[1])(out,builder,factory);
}
typedef uintptr_t(G *ctor_fn)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
static uintptr_t G ctor_detour(uintptr_t step,uintptr_t ctx,uintptr_t type,uintptr_t name,uintptr_t factory,uintptr_t arg,uintptr_t final_arg){
    uintptr_t result=((ctor_fn)bridge.originals[2])(step,ctx,type,name,factory,arg,final_arg);
    if(__atomic_load_n(&bridge.enabled,__ATOMIC_ACQUIRE)){
        char label[192]={0};size_t i;
        for(i=0;i<191;i++){uint16_t c=0;if(plat_read_safe((void*)(name+i*2),&c,2))break;
            if(!c)break;label[i]=(char)(c<128?c:'?');}
        label[i]=0;
        if(label[0]&&enter()){
            size_t at=bridge.next_name++%128;bridge.names[at].step=step;mh_strlcpy(bridge.names[at].name,label,192);leave();
            bbm_menu_event ev={.kind=BBM_MENU_CREATED};mh_strlcpy(ev.screen,label,sizeof ev.screen);enqueue(ev);
        }
    }return result;
}
static int cursor(const unsigned char *component,int32_t *index){
    int32_t fields[3];
    if(plat_read_safe(component+0x70,fields,sizeof fields)||fields[0]<1||fields[0]>4096)return 0;
    *index=fields[2]>=0?fields[2]:fields[1];return *index>=0&&*index<fields[0];
}
static void G list_detour(unsigned char *component,unsigned char *input){
    int32_t previous=0,now=0;unsigned char focused=0;
    int observe=__atomic_load_n(&bridge.enabled,__ATOMIC_ACQUIRE)&&input&&
        !plat_read_safe(input,&focused,1)&&focused&&cursor(component,&previous);
    ((void(G*)(unsigned char*,unsigned char*))bridge.originals[3])(component,input);
    if(!observe||!cursor(component,&now)||now==previous||!enter())return;
    size_t slot=64;for(size_t i=0;i<64;i++)if(bridge.controls[i].address==(uintptr_t)component&&
        bridge.controls[i].generation==bridge.generation){slot=i;break;}
    if(slot==64){slot=bridge.next_control++%64;bridge.controls[slot].address=(uintptr_t)component;
        bridge.controls[slot].generation=bridge.generation;bridge.controls[slot].token=++bridge.control_seq;}
    bbm_menu_event ev={.kind=BBM_MENU_SELECTION_CHANGED,.control=bridge.controls[slot].token,
        .value=now,.previous_value=previous};
    for(size_t i=0;i<128;i++)if(bridge.names[i].step==bridge.active)mh_strlcpy(ev.screen,bridge.names[i].name,sizeof ev.screen);
    leave();enqueue(ev);
}
static void poll(modhost *h){
    if(!__atomic_load_n(&bridge.enabled,__ATOMIC_ACQUIRE))return;
    uintptr_t flow=0,step=0;
    if(mh_read_safe(h,(void*)(bridge.base+0x5580e38),&flow,8)||!flow||
       mh_read_safe(h,(void*)(flow+0xa88),&step,8))return;
    if(!enter())return;
    if(step==bridge.active){leave();return;}
    uintptr_t previous=bridge.active;bridge.active=step;++bridge.generation;
    char name[192]={0};for(size_t i=0;i<128;i++)if(bridge.names[i].step==step)mh_strlcpy(name,bridge.names[i].name,sizeof name);
    leave();
    if(previous){bbm_menu_event ev={.kind=BBM_MENU_CLOSED};enqueue(ev);}
    if(step){bbm_menu_event ev={.kind=BBM_MENU_OPENED};mh_strlcpy(ev.screen,name[0]?name:"unknown",sizeof ev.screen);enqueue(ev);}
}
static size_t poll_drain(void *u,bbm_menu_event *out,size_t cap,uint64_t *drops){
    /* Called only while this host is live at its sync point. */
    poll(u);return drain(NULL,out,cap,drops);
}
static void stop(void *u){
    modhost *h=u;__atomic_store_n(&bridge.enabled,0,__ATOMIC_RELEASE);
    for(size_t i=0;i<5;i++)if(bridge.hooks[i]){
        void *block=bridge.hooks[i]->block;mh_hook_remove(h,bridge.hooks[i]);bridge.hooks[i]=NULL;
        for(size_t j=0;j<h->ndeferred;)if(h->deferred[j].p==block)h->deferred[j]=h->deferred[--h->ndeferred];else ++j;
    }
    /* A worker may have cached a detour. Retain bounded trampoline blocks and
     * static state to process exit, as with the existing native observers. */
    __atomic_store_n(&bridge.owner.host,NULL,__ATOMIC_RELEASE);
}
#include "native_menu_profile.h"
int mh_menu_bridge_resolve(modhost *h,const char *key,char *out,size_t cap){
    if(!__atomic_load_n(&bridge.enabled,__ATOMIC_ACQUIRE)||__atomic_load_n(&bridge.owner.host,__ATOMIC_ACQUIRE)!=h)return 0;
    const char *source=NULL;
    if(bridge.asset[0]&&!strcmp(key,"dvdroot_ps4/menu/ingametop.gfx"))source=bridge.asset;
    /* Step names are also movie names. Keep the registry path as the unique
     * step identity, but resolve its movie to the stock hierarchy required by
     * System's list factory. Only names interned by open_screen are aliases;
     * arbitrary game paths and stock screens must keep their normal assets. */
    static const char prefix[]="dvdroot_ps4/menu/";
    size_t len=strlen(key),start=sizeof prefix-1;
    if(!source&&bridge.list_asset[0]&&len>start+4&&!strncmp(key,prefix,start)&&!strcmp(key+len-4,".gfx")){
        if(!enter())return 0;
        for(size_t i=0;i<bridge.nwords;i++){
            size_t j=0;while(j<len-start-4&&bridge.words[i][j]&&
                (unsigned char)tolower(bridge.words[i][j])==(unsigned char)key[start+j])j++;
            if(j==len-start-4&&!bridge.words[i][j]){source=bridge.list_asset;break;}
        }
        leave();
    }
    if(!source)return 0;
    size_t n=strlen(source);if(n>=cap)return 0;memcpy(out,source,n+1);return 1;
}
int mh_menu_bridge_start(modhost *h,mh_menu_sink *sink){
    const char *on=getenv("BB_MODS_NATIVE_UI");
    if(!on||strcmp(on,"1")||h->cfg.safe_mode)return 0;
    if(bridge.used||!h->cfg.eboot_base||h->cfg.eboot_size<0x5580e40){mh_log(h,"bbmod.menu",BBM_LOG_WARN,"native UI unavailable: image/lifecycle");return 0;}
    unsigned char bytes[20];
    for(size_t i=0;i<sizeof menu_signatures/sizeof menu_signatures[0];i++){
        if(mh_read_safe(h,(void*)(h->cfg.eboot_base+menu_signatures[i].rva),bytes,20)||
           memcmp(bytes,menu_signatures[i].bytes,20)){
            mh_log(h,"bbmod.menu",BBM_LOG_WARN,"native UI unavailable: fingerprint at %llx",(unsigned long long)menu_signatures[i].rva);return 0;
        }
    }
    bridge.used=1;bridge.base=h->cfg.eboot_base;bridge.owner.host=h;bridge.owner.entry=&bridge.entry;
    const char *assets=getenv("BB_NATIVE_UI_ASSETS");
    if(assets&&*assets){
        char path[MH_PATH];int n=snprintf(path,sizeof path,"%s/dvdroot_ps4/menu/ingametop.gfx",assets);
        uint64_t size=0;int dir=0;
        if(n>0&&(size_t)n<sizeof path&&!plat_stat(path,NULL,&size,&dir)&&!dir&&size>11248&&size<65536)
            mh_strlcpy(bridge.asset,path,sizeof bridge.asset);
        else mh_log(h,"bbmod.menu",BBM_LOG_WARN,"native top-menu assets unavailable; use System -> Mods");
        n=snprintf(path,sizeof path,"%s/dvdroot_ps4/menu/optionsetting.gfx",assets);
        if(n>0&&(size_t)n<sizeof path&&!plat_stat(path,NULL,&size,&dir)&&!dir&&size>0)
            mh_strlcpy(bridge.list_asset,path,sizeof bridge.list_asset);
    }
    if(!bridge.list_asset[0]){mh_log(h,"bbmod.menu",BBM_LOG_WARN,"native UI unavailable: stock list movie missing from native assets");return 0;}
    mh_strlcpy(bridge.entry.m.id,"bbmod.native-ui",sizeof bridge.entry.m.id);
    ops.init=(void*) (bridge.base+0x1be8a30);ops.append=(void*)(bridge.base+0x1b4e2a0);
    ops.cleanup=(void*)(bridge.base+0x1b4cbb0);ops.caption=(void*)(bridge.base+0x1ae8cc0);
    ops.factory=(void*)(bridge.base+0x1bdaa90);ops.copy=(void*)(bridge.base+0x1b56560);
    ops.refresh=(void*)(bridge.base+0x1beaca0);ops.selection=(void*)(bridge.base+0x1be90f0);
    const char *live=getenv("BB_MODS_NATIVE_UI_LIVE");bridge.live=live&&!strcmp(live,"1");
    uintptr_t targets[5]={0x1be29e0,0x1bea630,0x1c1cce0,0x1acff30,0x1be8cf0};void *detours[5]={(void*)blank_detour,(void*)finalizer_detour,(void*)ctor_detour,(void*)list_detour,(void*)frame_detour};
    for(size_t i=0;i<(bridge.live?5u:4u);i++)if(mh_hook_inline(&bridge.owner,bridge.base+targets[i],detours[i],&bridge.originals[i],&bridge.hooks[i])!=BBM_OK){
        stop(h);mh_log(h,"bbmod.menu",BBM_LOG_WARN,"native UI hook installation failed; changes rolled back");return 0;
    }
    ops.finalize=(fn3)bridge.originals[1];
    __atomic_store_n(&bridge.enabled,1,__ATOMIC_RELEASE);
    *sink=(mh_menu_sink){h,publish,poll_drain,stop,BBM_MENU_CAP_LISTS|BBM_MENU_CAP_SYSTEM|BBM_MENU_CAP_OBSERVE|BBM_MENU_CAP_SELECTION|(bridge.asset[0]?BBM_MENU_CAP_INGAME:0)|(bridge.live?BBM_MENU_CAP_LIVE:0)};
    mh_log(h,"bbmod.menu",BBM_LOG_INFO,"experimental native menu adapter installed; stock list controls; live navigation unverified");return 1;
}
#else
int mh_menu_bridge_start(modhost *h,mh_menu_sink *sink){(void)h;(void)sink;return 0;}
int mh_menu_bridge_resolve(modhost *h,const char *k,char *o,size_t n){(void)h;(void)k;(void)o;(void)n;return 0;}
#endif
