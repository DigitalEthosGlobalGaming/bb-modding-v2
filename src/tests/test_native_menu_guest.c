/* Execute the actual SysV callable adapters against explicit fake engine ops.
 * This verifies C ABI/lifetime mechanics, not Bloodborne's native semantics. */
#include "../host/src/native_menu_bridge.c"
#include <assert.h>
static callable *rows[140];static size_t nrows;static unsigned finalized,cleaned;
static void *G fake_init(void *b,const int32_t*t){assert(*t==3);memset(b,0,0x1190);return b;}
static void G fake_caption(void*out,int32_t group,int32_t id){assert((group==200||group==201)&&id==101001);memset(out,0,0x40);uint64_t cap=7;memcpy((char*)out+0x20,&cap,8);}
static void *G fake_append(void *builder,void *captions,void *f,void *flag){
    assert(*(uint64_t*)flag==0&&nrows<140);(void)builder;
    const uint16_t *title=*(uint16_t**)captions;assert(title&&title[0]);
    callable *object=*(callable**)((char*)f+0x20);assert(object==(callable*)f);
    rows[nrows++]=((void*(G*)(callable*,callable*))object->vt[0])(object,NULL);return builder;
}
static void *G fake_finalize(void*out,void*b,void*f){assert(*(uintptr_t*)f==bridge.base+0x5343790);assert(*(void**)((char*)b+0x1180)==(void*)0x5000);*(void**)out=(void*)0xabc;finalized++;return out;}
static void G fake_cleanup(void*b){(void)b;while(nrows)destroy(rows[--nrows],1);cleaned++;}
static uintptr_t G __attribute__((noinline)) fake_ctor(uintptr_t step,uintptr_t ctx,uintptr_t type,uintptr_t name,uintptr_t factory,uintptr_t arg,uintptr_t last){
    assert(step==1&&ctx==2&&type==3&&name&&factory==5&&arg==6&&last==7);return 123;
}
static int32_t next_cursor=2;
static void G fake_list(unsigned char *c,unsigned char *input){(void)input;memcpy(c+0x74,&next_cursor,4);}
int main(void){
    ops=(native_ops){fake_init,fake_append,fake_finalize,fake_cleanup,fake_caption};
    bridge.enabled=1;bridge.base=0x10000000;
    bridge.originals[2]=(void*)fake_ctor;uint16_t native_name[]={'I','n','v','e','n','t','o','r','y',0};
    assert(ctor_detour(1,2,3,(uintptr_t)native_name,5,6,7)==123);
    bbm_menu_event observed[128];uint64_t lost;
    assert(drain(NULL,observed,128,&lost)==1&&observed[0].kind==BBM_MENU_CREATED&&!strcmp(observed[0].screen,"Inventory"));
    unsigned char component[0x300]={0},focus=1;int32_t fields[]={10,1,-1};memcpy(component+0x70,fields,sizeof fields);
    bridge.originals[3]=(void*)fake_list;list_detour(component,&focus);
    assert(drain(NULL,observed,128,&lost)==1&&observed[0].kind==BBM_MENU_SELECTION_CHANGED&&observed[0].previous_value==1&&observed[0].value==2&&observed[0].control);
    focus=0;next_cursor=3;list_detour(component,&focus);assert(drain(NULL,observed,128,&lost)==0);
    mh_menu_snapshot *s=calloc(1,sizeof*s);s->count=1;s->revision=1;
    mh_menu_item *item=&s->items[0];item->id=42;item->version=7;item->node.kind=BBM_MENU_ACTION;item->node.flags=BBM_MENU_ENABLED;
    strcpy(item->node.parent,"a.mod/settings");strcpy(item->node.label,"An action");assert(publish(NULL,s));
    assert(append(NULL,item)&&nrows==1);callable *held=rows[--nrows];assert(held->data->refs==1);
    struct{callable object;uint64_t canary[2];} inline_copy={.canary={123,456}};
    assert(clone(held,&inline_copy.object)==&inline_copy.object&&inline_copy.canary[0]==123&&inline_copy.canary[1]==456);
    assert(held->data->refs==2);destroy(&inline_copy.object,0);assert(held->data->refs==1);
    void *out=(void*)1;assert(invoke(&out,held,(void*)0x6000)==&out&&!out);
    bbm_menu_event events[128];uint64_t drops;assert(drain(NULL,events,128,&drops)==1&&events[0].node==42&&events[0].revision==7);
    s->items[0].version++;assert(publish(NULL,s));invoke(&out,held,(void*)0x6000);assert(drain(NULL,events,128,&drops)==0);destroy(held,1);
    assert(open_screen(&out,(void*)0x5000,"a.mod/settings")==&out&&out==(void*)0xabc&&finalized==1&&cleaned==1&&nrows==0);
    assert(bridge.nwords==1);for(int i=0;i<20;i++)open_screen(&out,(void*)0x5000,"a.mod/settings");assert(bridge.nwords==1);
    for(int i=0;i<140;i++)enqueue((bbm_menu_event){.kind=BBM_MENU_CREATED});assert(drain(NULL,events,128,&drops)==128&&drops==12);
    bridge.enabled=0;enqueue((bbm_menu_event){.kind=BBM_MENU_CREATED});assert(drain(NULL,events,128,&drops)==0);
    /* Execute a real relocated trampoline through the seven-argument hook.
     * The synthetic function deliberately consumes the stack argument. */
    modhost_config cfg={0};cfg.safe_mode=1;cfg.cache_dir="native-menu-guest-test-cache";
    modhost *h=modhost_create(&cfg);assert(h);struct bbm_mod owner={.host=h};mh_hook *hook=NULL;
    assert(mh_hook_inline(&owner,(uintptr_t)fake_ctor,(void*)ctor_detour,&bridge.originals[2],&hook)==BBM_OK);
    bridge.enabled=1;ctor_fn volatile entry=fake_ctor;
    assert(entry(1,2,3,(uintptr_t)native_name,5,6,7)==123);
    assert(drain(NULL,observed,128,&lost)==1&&observed[0].kind==BBM_MENU_CREATED);
    bridge.enabled=0;mh_hook_remove(h,hook);modhost_destroy(h);
    free(s);puts("native menu SysV adapter: functor ownership, native list calls, cursor observation, real seven-argument detour/trampoline, stale actions and bounded queue: OK");return 0;
}
