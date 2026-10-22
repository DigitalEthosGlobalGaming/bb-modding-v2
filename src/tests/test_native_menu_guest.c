/* Execute the actual SysV callable adapters against explicit fake engine ops.
 * This verifies C ABI/lifetime mechanics, not Bloodborne's native semantics. */
#include "../host/src/native_menu_bridge.c"
#include <assert.h>
static callable *rows[140];static size_t nrows;static unsigned finalized,cleaned;
static unsigned char live_widget[0x2020];static unsigned refreshes,selections,frames;
static void *G fake_init(void *b,const int32_t*t){assert(*t==3);memset(b,0,0x1190);return b;}
static void G fake_caption(void*out,int32_t group,int32_t id){assert((group==200||group==201)&&id==101001);memset(out,0,0x40);uint64_t cap=7;memcpy((char*)out+0x20,&cap,8);}
static void *G fake_append(void *builder,void *captions,void *f,void *flag){
    assert(*(uint64_t*)flag==0&&nrows<140);(void)builder;
    const uint16_t *title=*(uint16_t**)captions;assert(title&&title[0]);
    callable *object=*(callable**)((char*)f+0x20);assert(object==(callable*)f);
    rows[nrows++]=((void*(G*)(callable*,callable*))object->vt[0])(object,NULL);return builder;
}
static void *G fake_factory(void*f,void*root,void*b){
    (void)root;assert(*(uintptr_t*)f==bridge.base+0x5343790);
    memset(live_widget,0,sizeof live_widget);memcpy(live_widget+0xd98,(char*)b+8,8);return live_widget;
}
static void *G fake_finalize(void*out,void*b,void*f){
    assert(*(void**)((char*)b+0x1180)==(void*)0x5000);
    if(bridge.live){callable *object=*(callable**)((char*)f+0x20);assert(object->vt==factory_vtable);
        callable *copy=clone(object,NULL);assert(factory_invoke(copy,NULL,b)==live_widget);destroy(copy,1);}
    else assert(*(uintptr_t*)f==bridge.base+0x5343790);
    *(void**)out=(void*)0xabc;finalized++;return out;
}
static void G fake_cleanup(void*b){(void)b;while(nrows)destroy(rows[--nrows],1);cleaned++;}
static void G fake_copy(void*to,const void*from){
    void *context=*(void**)((char*)from+0x1180);assert(!context||context==(void*)0x5000);memcpy(to,from,0x1180);
}
static unsigned list_phase;
static void G fake_count(void*l,int32_t count){
    assert(l==live_widget+0xa70&&count>=1&&count<=5&&list_phase==0);
    memcpy(live_widget+0xae0,&count,4);list_phase=1;
}
static int G fake_focus(void*l,int32_t selected){
    int32_t count;memcpy(&count,live_widget+0xae0,4);
    assert(l==live_widget+0xa70&&list_phase==1&&selected>=0&&selected<count);
    memcpy(live_widget+0xae4,&selected,4);list_phase=2;return 1;
}
static void G fake_layout(void*l){assert(l==live_widget+0xa70&&list_phase==2);list_phase=3;}
static void G fake_redraw(void*l){
    int32_t pending;memcpy(&pending,live_widget+0xae8,4);
    assert(l==live_widget+0xa70&&list_phase==3&&pending==-1);
    list_phase=0;refreshes++;
}
static void G fake_selection(void*w){assert(w==live_widget);selections++;}
static int rows_ready=1;
static int G fake_ready(void *list,size_t count){assert(list==live_widget+0xa70&&count>=1&&count<=5&&list_phase==0);return rows_ready;}
static unsigned movie_reads,movie_releases,row_reads;
static double movie_alpha=100;static int movie_visible=1,movie_valid=1;
static void **test_movie_vt;static void *expected_links;
static int G fake_display(void *iface,void *object,void *display){
    assert(iface==&test_movie_vt&&object==(void*)0x1234);
    memset(display,0,0xd8);double fields[]={0,0,0,100,100,movie_alpha};memcpy(display,fields,sizeof fields);
    ((unsigned char*)display)[0xd6]=(unsigned char)movie_visible;movie_reads++;return movie_valid;
}
static void G fake_movie_release(void *iface,void *links,void *object){
    assert(iface==&test_movie_vt&&links==expected_links&&object==(void*)0x1234);movie_releases++;
}
static void movie_fixture(native_movie_value *value){
    memset(value,0,sizeof *value);value->interface=&test_movie_vt;value->type=0x4a;value->object=(void*)0x1234;
    expected_links=value->links;
}
static void *G fake_row_read(void *out,void *list,int32_t slot){
    assert(list==live_widget+0xa70&&slot>=0&&slot<5);row_reads++;
    native_movie_row *row=out;memset(row,0,sizeof *row);
    row->vt=(void**)0x1111;row->links[2]=0x2222;row->owner=0x3333;
    movie_fixture(&row->value);return row;
}
static void G fake_frame(void*w,void*input,float elapsed){assert(w==live_widget&&input==(void*)0x123&&elapsed==0.25f);frames++;}
static uintptr_t G __attribute__((noinline)) fake_ctor(uintptr_t step,uintptr_t ctx,uintptr_t type,uintptr_t name,uintptr_t factory,uintptr_t arg,uintptr_t last){
    assert(step==1&&ctx==2&&type==3&&name&&factory==5&&arg==6&&last==7);return 123;
}
static int32_t next_cursor=2;
static void G fake_list(unsigned char *c,unsigned char *input){(void)input;memcpy(c+0x74,&next_cursor,4);}
int main(void){
    char longest[192];memset(longest,'x',191);longest[191]=0;uint16_t converted[192];wide(longest,converted,192);
    assert(converted[190]=='x'&&converted[191]==0);
    ops=(native_ops){.init=fake_init,.append=fake_append,.finalize=fake_finalize,.cleanup=fake_cleanup,.caption=fake_caption,
        .factory=fake_factory,.copy=fake_copy,.list_count=fake_count,.list_focus=fake_focus,
        .list_layout=fake_layout,.list_redraw=fake_redraw,.list_ready=fake_ready,.selection=fake_selection};
    bridge.enabled=1;bridge.base=0x10000000;
    bridge.originals[2]=(void*)fake_ctor;uint16_t native_name[]={'I','n','v','e','n','t','o','r','y',0};
    assert(ctor_detour(1,2,3,(uintptr_t)native_name,5,6,7)==123);
    bbm_menu_event observed[128];uint64_t lost;
    assert(drain(NULL,observed,128,&lost)==1&&observed[0].kind==BBM_MENU_CREATED&&!strcmp(observed[0].screen,"Inventory"));
    unsigned char component[0x300]={0},focus=1;int32_t fields[]={10,1,-1};memcpy(component+0x70,fields,sizeof fields);
    bridge.originals[3]=(void*)fake_list;list_detour(component,&focus);
    assert(drain(NULL,observed,128,&lost)==1&&observed[0].kind==BBM_MENU_SELECTION_CHANGED&&observed[0].previous_value==1&&observed[0].value==2&&observed[0].control);
    focus=0;next_cursor=3;list_detour(component,&focus);assert(drain(NULL,observed,128,&lost)==0);
    mh_menu_snapshot *s=calloc(1,sizeof*s);s->count=1;s->revision=1;s->mod_count=2;
    strcpy(s->mods[0].id,"a.mod");strcpy(s->mods[0].name,"First mod");s->mods[0].state=MODHOST_ACTIVE;
    strcpy(s->mods[1].id,"b.mod");strcpy(s->mods[1].name,"Second mod");s->mods[1].state=MODHOST_DISABLED;
    mh_menu_item *item=&s->items[0];item->id=42;item->version=7;item->node.kind=BBM_MENU_ACTION;item->node.flags=BBM_MENU_ENABLED;
    strcpy(item->node.parent,"a.mod/settings");strcpy(item->node.label,"An action");assert(publish(NULL,s));
    mh_menu_item listed[3];assert(items_for("game.mods",listed,3)==2);
    assert(strstr(listed[0].node.label,"Loaded")&&strstr(listed[1].node.label,"Disabled"));
    /* The pause menu's Mods entry is the built-in link until a manager registers "mods-root". */
    mh_menu_item root;root_item(&root);
    assert(root.node.kind==BBM_MENU_LINK&&!strcmp(root.node.label,"Mods")&&!strcmp(root.node.target,"game.mods")&&root.id==0);
    s->count=2;mh_menu_item *over=&s->items[1];over->id=77;over->version=3;over->node.kind=BBM_MENU_ACTION;over->node.flags=BBM_MENU_ENABLED;
    strcpy(over->node.parent,"game.system");strcpy(over->node.key,"mods-root");strcpy(over->node.label,"Mods");assert(publish(NULL,s));
    root_item(&root);assert(root.id==77&&root.node.kind==BBM_MENU_ACTION);
    mh_menu_item system_items[3];assert(items_for("game.system",system_items,3)==0); /* not listed twice */
    over->node.flags=0;assert(publish(NULL,s));root_item(&root);assert(root.id==0&&root.node.kind==BBM_MENU_LINK); /* disabled: built-in again */
    s->count=1;assert(publish(NULL,s));
    assert(append(NULL,item)&&nrows==1);callable *held=rows[--nrows];assert(held->data->refs==1);
    struct{callable object;uint64_t canary[2];} inline_copy={.canary={123,456}};
    assert(clone(held,&inline_copy.object)==&inline_copy.object&&inline_copy.canary[0]==123&&inline_copy.canary[1]==456);
    assert(held->data->refs==2);destroy(&inline_copy.object,0);assert(held->data->refs==1);
    void *out=(void*)1;assert(invoke(&out,held,(void*)0x6000)==&out&&!out);
    bbm_menu_event events[128];uint64_t drops;assert(drain(NULL,events,128,&drops)==1&&events[0].node==42&&events[0].revision==7);
    s->items[0].version++;assert(publish(NULL,s));invoke(&out,held,(void*)0x6000);assert(drain(NULL,events,128,&drops)==0);destroy(held,1);
    assert(open_screen(&out,(void*)0x5000,"a.mod/settings")==&out&&out==(void*)0xabc&&finalized==1&&cleaned==1&&nrows==0);
    assert(bridge.nwords==1);for(int i=0;i<20;i++)open_screen(&out,(void*)0x5000,"a.mod/settings");assert(bridge.nwords==1);
    /* Real engine logs showed custom step IDs being requested as .gfx movies.
     * The alias must supply System's actual stock hierarchy, without replacing
     * stock movies or allowing arbitrary paths from the caller. */
    modhost resolver_host={0},other_host={0};bridge.owner.host=&resolver_host;
    strcpy(bridge.list_asset,"native-assets/optionsetting.gfx");
    char resolved[MH_PATH];
    assert(mh_menu_bridge_resolve(&resolver_host,"dvdroot_ps4/menu/a.mod/settings.gfx",resolved,sizeof resolved));
    assert(!strcmp(resolved,"native-assets/optionsetting.gfx"));
    assert(!mh_menu_bridge_resolve(&other_host,"dvdroot_ps4/menu/a.mod/settings.gfx",resolved,sizeof resolved));
    assert(!mh_menu_bridge_resolve(&resolver_host,"dvdroot_ps4/menu/a.mod/settings.gfx",resolved,2));
    assert(!mh_menu_bridge_resolve(&resolver_host,"dvdroot_ps4/menu/optionsetting.gfx",resolved,sizeof resolved));
    assert(!mh_menu_bridge_resolve(&resolver_host,"dvdroot_ps4/menu/other.mod/settings.gfx",resolved,sizeof resolved));
    assert(!mh_menu_bridge_resolve(&resolver_host,"dvdroot_ps4/menu/../a.mod/settings.gfx",resolved,sizeof resolved));
    assert(intern("Case.Mod/Settings"));
    assert(mh_menu_bridge_resolve(&resolver_host,"dvdroot_ps4/menu/case.mod/settings.gfx",resolved,sizeof resolved));
    /* The new factory owns only copied host data and delegates native widget
     * construction. Refresh must preserve the selected ID across reorder and
     * choose a neighboring valid row after removal, without a stack of screens. */
    bridge.live=1;open_screen(&out,(void*)0x5000,"a.mod/settings");
    refresh_view(live_widget);assert(refreshes==1&&selections==1);
    refresh_view(live_widget);assert(refreshes==1); /* unchanged snapshot */
    s->revision=10;assert(publish(NULL,s));refresh_view(live_widget);assert(refreshes==1); /* other screen changed */
    s->items[1]=s->items[0];s->items[0].id=43;s->items[0].version=8;s->count=2;s->revision=2;
    assert(publish(NULL,s));refresh_view(live_widget);
    int32_t focused=-1;memcpy(&focused,live_widget+0xae4,4);assert(focused==1&&refreshes==2);
    s->count=1;s->revision=3;assert(publish(NULL,s));rows_ready=0;refresh_view(live_widget);
    assert(refreshes==2&&list_phase==0); /* resize waits; pending revision survives */
    int32_t held_count;memcpy(&held_count,live_widget+0xae0,4);assert(held_count==2);
    rows_ready=1;refresh_view(live_widget);
    memcpy(&focused,live_widget+0xae4,4);assert(focused==0&&refreshes==3);
    s->count=0;s->revision=4;assert(publish(NULL,s));refresh_view(live_widget);assert(refreshes==4);
    /* Native update arguments, including the float register, pass through. */
    bridge.originals[4]=(void*)fake_frame;frame_detour(live_widget,(void*)0x123,0.25f);assert(frames==1&&refreshes==4);
    const uint16_t *stale=NULL;memcpy(live_widget+0xd98,&stale,8);
    s->revision=5;assert(publish(NULL,s));refresh_view(live_widget);assert(refreshes==4);
    /* Recover registration lost to publication contention / bounded tracking
     * eviction, but never identify a stock widget by a guessed string. */
    memset(bridge.views,0,sizeof bridge.views);
    refresh_view(live_widget);assert(refreshes==4); /* null/untrusted name */
    const uint16_t *recover=intern("a.mod/settings");memcpy(live_widget+0xd98,&recover,8);
    refresh_view(live_widget);assert(refreshes==5);
    /* Long lists share one native Step. Exercise the actual host navigation
     * callable, deferred refresh, all logical rows, focus reset and shrinking. */
    s->count=13;s->revision=20;
    for(size_t i=0;i<s->count;i++){
        s->items[i]=s->items[0];s->items[i].id=100+i;s->items[i].version=1;
        snprintf(s->items[i].node.label,sizeof s->items[i].node.label,"Row %zu",i+1);
    }
    assert(publish(NULL,s));refresh_view(live_widget);
    size_t view=64;for(size_t i=0;i<64;i++)if(bridge.views[i].widget==(uintptr_t)live_widget){view=i;break;}
    assert(view<64&&bridge.views[view].page==0&&bridge.views[view].count==4);
    unsigned steps=finalized;unsigned seen=0;
    for(size_t page=0;page<5;page++){
        mh_menu_item page_rows[13];size_t requested=page;
        size_t visible=items_for("a.mod/settings",page_rows,13);
        visible=page_items(page_rows,visible,&requested);assert(requested==page&&visible<=5);
        for(size_t i=0;i<visible;i++)if(page_rows[i].id){
            assert(page_rows[i].id>=100&&page_rows[i].id<113);seen|=1u<<(page_rows[i].id-100);
        }
        if(page==4)break;
        assert(!strcmp(page_rows[visible-1].node.target,"__bbmod.page/next"));
        assert(append(NULL,&page_rows[visible-1])&&nrows==1);callable *navigation=rows[--nrows];
        unsigned before=refreshes;invoke(&out,navigation,live_widget);destroy(navigation,1);
        assert(refreshes==before&&finalized==steps); /* no engine mutation in action */
        refresh_view(live_widget);assert(refreshes==before+1&&bridge.views[view].page==page+1);
        memcpy(&focused,live_widget+0xae4,4);assert(focused==0);
    }
    assert(seen==0x1fff&&finalized==steps);
    mh_menu_item page_rows[13];size_t page=4;
    size_t visible=items_for("a.mod/settings",page_rows,13);
    visible=page_items(page_rows,visible,&page);assert(visible==2);
    assert(!strcmp(page_rows[visible-1].node.target,"__bbmod.page/previous"));
    assert(append(NULL,&page_rows[visible-1]));held=rows[--nrows];
    invoke(&out,held,live_widget);destroy(held,1);refresh_view(live_widget);
    assert(bridge.views[view].page==3&&finalized==steps);
    /* A stable selected row follows a reorder onto a different page. */
    int32_t first=0;memcpy(live_widget+0xae4,&first,4);
    mh_menu_item swap=s->items[9];s->items[9]=s->items[1];s->items[1]=swap;
    ++s->revision;assert(publish(NULL,s));refresh_view(live_widget);
    memcpy(&focused,live_widget+0xae4,4);
    assert(bridge.views[view].page==0&&focused==1&&bridge.views[view].ids[1]==109);
    s->count=2;++s->revision;assert(publish(NULL,s));refresh_view(live_widget);
    assert(bridge.views[view].page==0&&bridge.views[view].count==2&&finalized==steps);
    bridge.live=0;
    for(int i=0;i<140;i++)enqueue((bbm_menu_event){.kind=BBM_MENU_CREATED});assert(drain(NULL,events,128,&drops)==128&&drops==12);
    bridge.enabled=0;enqueue((bbm_menu_event){.kind=BBM_MENU_CREATED});assert(drain(NULL,events,128,&drops)==0);
    /* Execute a real relocated trampoline through the seven-argument hook.
     * The synthetic function deliberately consumes the stack argument. */
    modhost_config cfg={0};cfg.safe_mode=1;cfg.cache_dir="native-menu-guest-test-cache";
    modhost *h=modhost_create(&cfg);assert(h);struct bbm_mod owner={.host=h};mh_hook *hook=NULL;
    /* Regress the real layout distinction that crashed the first tracer. Row
     * header/owner fields deliberately cannot be used as a movie interface. */
    bridge.owner.host=h;void *methods[29]={0};methods[0xe0/8]=(void*)fake_display;
    /* Composition observation never invokes a factory or assumes its layout.
     * Unreadable descriptors and addresses outside the image remain harmless. */
    bridge.component_trace_budget=2;
    trace_component_step("inventory-fixture",1,3,1);
    trace_component_step("inventory-fixture",2,3,0);
    assert(bridge.component_trace_budget==0&&!component_trace_take());
    assert(image_rva(1)==0);
    methods[0x18/8]=(void*)fake_movie_release;test_movie_vt=methods;
    native_movie_value movie={0};movie_fixture(&movie);trace_movie_value(&movie,"test","root",-1);
    assert(movie_reads==1&&movie_releases==1&&!movie.interface&&!movie.type);
    native_movie_row row={0};row.vt=(void**)0x1111;row.links[2]=0x2222;row.owner=0x3333;
    movie_fixture(&row.value);trace_row_value(&row,"test","row",0);
    assert(movie_reads==2&&movie_releases==2&&row.links[2]==0x2222&&row.owner==0x3333);
    bridge.trace=1;ops.list_row=fake_row_read;trace_list(live_widget,&bridge.views[view],"test");bridge.trace=0;
    assert(row_reads==5&&movie_reads==7&&movie_releases==7);
    movie_alpha=0;assert(!list_movie_ready(live_widget+0xa70,4));
    movie_alpha=50;assert(!list_movie_ready(live_widget+0xa70,4));
    movie_alpha=100;assert(list_movie_ready(live_widget+0xa70,4));
    movie_visible=0;assert(!list_movie_ready(live_widget+0xa70,4));movie_visible=1;
    movie_valid=0;assert(!list_movie_ready(live_widget+0xa70,4));movie_valid=1;
    assert(!list_movie_ready(live_widget+0xa70,6)&&movie_reads==movie_releases);
    assert(mh_hook_inline(&owner,(uintptr_t)fake_ctor,(void*)ctor_detour,&bridge.originals[2],&hook)==BBM_OK);
    bridge.enabled=1;ctor_fn volatile entry=fake_ctor;
    assert(entry(1,2,3,(uintptr_t)native_name,5,6,7)==123);
    assert(drain(NULL,observed,128,&lost)==1&&observed[0].kind==BBM_MENU_CREATED);
    bridge.enabled=0;mh_hook_remove(h,hook);modhost_destroy(h);
    free(s);puts("native menu SysV adapter: functor ownership, native list calls, cursor observation, real seven-argument detour/trampoline, stale actions and bounded queue: OK");return 0;
}
