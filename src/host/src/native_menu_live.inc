/* Shared native list refresh. Included after row construction and snapshots.
 * All engine allocation/control mutation occurs on the widget update thread.
 * Publication still only copies data; it never touches a live engine widget. */
static void *G factory_invoke(callable *self,void *root,void *params){
    _Alignas(16) uintptr_t stock[5]={bridge.base+0x5343790,0,0,0,0};stock[4]=(uintptr_t)stock;
    void *widget=ops.factory(stock,root,params);
    if(widget&&__atomic_load_n(&bridge.enabled,__ATOMIC_ACQUIRE)&&enter()){
        size_t at=bridge.next_view++%64;
        /* Reused widget addresses replace their previous tracking entry. */
        for(size_t i=0;i<64;i++)if(bridge.views[i].widget==(uintptr_t)widget){at=i;break;}
        live_view *v=&bridge.views[at];memset(v,0,sizeof *v);
        v->widget=(uintptr_t)widget;memcpy(&v->name,(char*)params+8,8);
        v->context=(void*)self->data->context;
        mh_strlcpy(v->path,self->data->item.path,sizeof v->path);
        /* revision=0 requests a consistent rebuild at the first native update;
         * the original factory's builder can be older than today's snapshot. */
        leave();
    }
    return widget;
}
static void *factory_vtable[]={ (void*)clone,(void*)clone,(void*)factory_invoke,(void*)unused,(void*)destroy,(void*)destruct };

/* The stock template exposes six command rows. Larger logical lists use four
 * data rows and up to two host-owned navigation rows, refreshed in the same
 * widget. No additional engine Step is created by changing a page. */
#define NATIVE_PAGE_ROWS 4u
static size_t page_items(mh_menu_item *items,size_t total,size_t *page){
    if(total<=MH_MENU_VISIBLE_ROWS){*page=0;return total;}
    size_t pages=(total+NATIVE_PAGE_ROWS-1)/NATIVE_PAGE_ROWS;
    if(*page>=pages)*page=pages-1;
    size_t first=*page*NATIVE_PAGE_ROWS,count=total-first;
    if(count>NATIVE_PAGE_ROWS)count=NATIVE_PAGE_ROWS;
    memmove(items,items+first,count*sizeof *items);
    for(int direction=-1;direction<=1;direction+=2){
        if((direction<0&&!*page)||(direction>0&&*page+1==pages))continue;
        mh_menu_item *nav=&items[count++];memset(nav,0,sizeof *nav);
        nav->node.kind=BBM_MENU_ACTION;nav->node.flags=BBM_MENU_ENABLED;
        size_t destination=direction<0?*page-1:*page+1;
        snprintf(nav->node.label,sizeof nav->node.label,"%s (%zu/%zu)",direction<0?"Previous page":"Next page",destination+1,pages);
        snprintf(nav->node.help,sizeof nav->node.help,"Page %zu of %zu. Open page %zu; Back returns to the previous menu.",*page+1,pages,destination+1);
        mh_strlcpy(nav->node.target,direction<0?"__bbmod.page/previous":"__bbmod.page/next",sizeof nav->node.target);
    }
    return count;
}
static int request_page(void *widget,int direction){
    if(!__atomic_load_n(&bridge.enabled,__ATOMIC_ACQUIRE)||!bridge.live||!enter())return 0;
    int found=0;
    for(size_t i=0;i<64;i++)if(bridge.views[i].widget==(uintptr_t)widget){
        live_view *v=&bridge.views[i];
        if(direction>0){if(v->page<MH_MENU_LIMIT/NATIVE_PAGE_ROWS)++v->page;}
        else if(v->page)--v->page;
        v->revision=0;v->content_hash=0;v->reset_focus=1;found=1;break;
    }
    leave();return found;
}

static uint64_t row_identity(const mh_menu_item *item){
    if(item->id)return item->id;
    uint64_t hash=1469598103934665603ull;
    const char *key=item->node.target[0]?item->node.target:item->node.label;
    while(*key)hash=(hash^(unsigned char)*key++)*1099511628211ull;
    return hash;
}
static void remember_view(size_t slot,const live_view *next){
    if(enter()){
        if(bridge.views[slot].widget==next->widget&&bridge.views[slot].name==next->name)bridge.views[slot]=*next;
        leave();
    }
}
static void refresh_view(void *widget){
    if(!__atomic_load_n(&bridge.enabled,__ATOMIC_ACQUIRE)||!enter())return;
    size_t slot=64;
    for(size_t i=0;i<64;i++)if(bridge.views[i].widget==(uintptr_t)widget){slot=i;break;}
    if(slot==64){
        /* A busy publisher may have prevented factory-time tracking, or an
         * older hidden view may have been evicted. Retry from the retained
         * builder, accepting only our process-lifetime interned name pointers.
         * No untrusted engine name is dereferenced. The refresh copy operation
         * copies 0x1180 bytes of list data; the trailing Step context is not part
         * of the widget and is unused here (unlike open_screen/finalize). */
        const uint16_t *name=NULL;memcpy(&name,(char*)widget+0xd98,8);
        size_t word=512;
        for(size_t i=0;i<bridge.nwords;i++)if(name==bridge.words[i]){word=i;break;}
        if(word==512){leave();return;}
        slot=bridge.next_view++%64;live_view *v=&bridge.views[slot];memset(v,0,sizeof *v);
        v->widget=(uintptr_t)widget;v->name=name;
        for(size_t i=0;i<sizeof v->path-1&&bridge.words[word][i];i++)v->path[i]=(char)bridge.words[word][i];
    }
    if(bridge.views[slot].revision==bridge.snapshot.revision){leave();return;}
    live_view previous=bridge.views[slot];leave();
    const uint16_t *name=NULL;memcpy(&name,(char*)widget+0xd98,8);
    if(name!=previous.name)return; /* recycled address now belongs to another screen */
    mh_menu_item *items=calloc(MH_MENU_LIMIT,sizeof *items);if(!items)return;
    uint64_t revision=0;size_t count=items_at_revision(previous.path,items,MH_MENU_LIMIT,&revision);
    if(!revision){free(items);return;} /* publication lock was busy; retry */
    int32_t selected=0,pending=-1;
    memcpy(&selected,(char*)widget+0xae4,4);memcpy(&pending,(char*)widget+0xae8,4);
    if(pending>=0)selected=pending;
    uint64_t selected_id=!previous.reset_focus&&selected>=0&&(size_t)selected<previous.count?previous.ids[selected]:0;
    live_view next=previous;next.revision=revision;next.reset_focus=0;
    if(selected_id&&count>MH_MENU_VISIBLE_ROWS)for(size_t i=0;i<count;i++)if(row_identity(&items[i])==selected_id){next.page=i/NATIVE_PAGE_ROWS;break;}
    count=page_items(items,count,&next.page);next.count=count;
    uint64_t hash=1469598103934665603ull;
    const unsigned char *bytes=(const unsigned char*)items;
    for(size_t i=0;i<count*sizeof *items;i++)hash=(hash^bytes[i])*1099511628211ull;
    next.content_hash=hash;
    if(previous.content_hash==hash){remember_view(slot,&next);free(items);return;}
    for(size_t i=0;i<count;i++)next.ids[i]=row_identity(&items[i]);
    int32_t restore=previous.reset_focus||selected<0?0:selected;
    if(count&&(size_t)restore>=count)restore=(int32_t)count-1;
    if(!count)restore=0;
    if(selected_id)for(size_t i=0;i<count;i++)if(next.ids[i]==selected_id){restore=(int32_t)i;break;}
    _Alignas(16) unsigned char builder[0x1190]={0};int32_t type=3;
    void *ctx=previous.context;
    ops.init(builder,&type);memcpy(builder+8,&name,8);memcpy(builder+0x1180,&ctx,8);
    for(size_t i=0;i<count;i++)if(!append(builder,&items[i])){
        ops.cleanup(builder);free(items);return;
    }
    if(!count){mh_menu_item empty={0};empty.node.kind=BBM_MENU_ACTION;
        mh_strlcpy(empty.node.label,"No settings available",sizeof empty.node.label);
        if(!append(builder,&empty)){ops.cleanup(builder);free(items);return;}}
    ops.cleanup((char*)widget+0xd90);
    ops.copy((char*)widget+0xd90,builder);ops.cleanup(builder);
    ops.refresh(widget,(char*)widget+0xd30);
    memcpy((char*)widget+0xae4,&restore,4);pending=-1;memcpy((char*)widget+0xae8,&pending,4);
    ops.selection(widget);
    remember_view(slot,&next);
    free(items);
}
static void G frame_detour(void *widget,void *input,float elapsed){
    refresh_view(widget);
    ((void(G*)(void*,void*,float))bridge.originals[4])(widget,input,elapsed);
}
