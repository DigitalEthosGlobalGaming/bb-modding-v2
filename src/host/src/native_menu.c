/* SPDX-License-Identifier: GPL-2.0-or-later
 * Declarative native UI registry. Serialized by run_lock; the native adapter
 * receives copied data and queues events, never calls into a mod DLL. */
#include "native_menu.h"
#include <ctype.h>
struct mh_menu_service {
    mh_menu_sink sink;
    uint64_t revision, published, notified,mod_hash;
    bbm_menu_state state;
};
typedef struct owned_node { bbm_menu_node node; uint64_t version; } owned_node;

static int text(const char *s,size_t cap,int empty) {
    size_t n=0;while(n<cap && s[n]) ++n;
    if(n==cap || (!n&&!empty)) return 0;
    /* Strict UTF-8, including overlongs, surrogate and scalar bounds. */
    for(size_t i=0;i<n;) {
        unsigned c=(unsigned char)s[i++],v=0,min=0;int k=0;
        if(c<0x80) {if(c<32) return 0;continue;}
        if(c>=0xc2&&c<=0xdf){k=1;v=c&31;min=0x80;}
        else if(c>=0xe0&&c<=0xef){k=2;v=c&15;min=0x800;}
        else if(c>=0xf0&&c<=0xf4){k=3;v=c&7;min=0x10000;}else return 0;
        if(i+(size_t)k>n) return 0;
        while(k--){c=(unsigned char)s[i++];if((c&0xc0)!=0x80)return 0;v=(v<<6)|(c&63);}
        if(v<min||v>0x10ffff||(v>=0xd800&&v<=0xdfff))return 0;
    }
    return 1;
}
static int key(const char *s,size_t cap) {
    if(!text(s,cap,0))return 0;
    for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||
      (*s>='0'&&*s<='9')||*s=='.'||*s=='_'||*s=='-'))return 0;
    return 1;
}
static int path(const char *s) {
    if(!text(s,192,0)) return 0;
    if(!strncmp(s,"game.",5))return !strcmp(s,"game.mods")||!strcmp(s,"game.system");
    const char *slash=strchr(s,'/');if(!slash||strchr(slash+1,'/'))return 0;
    char owner[128];size_t n=(size_t)(slash-s);if(!n||n>=sizeof owner)return 0;
    memcpy(owner,s,n);owner[n]=0;return key(owner,sizeof owner)&&key(slash+1,64);
}
static bbm_status valid(bbm_handle m,const bbm_menu_node *n) {
    if(!m||!m->host||!m->host->menu||!m->entry||!n||n->size!=sizeof *n||m->faulted||m->bypassed)return BBM_ERR_ARG;
    if(!key(n->key,sizeof n->key)||!path(n->parent)||!text(n->label,sizeof n->label,0)||
       !text(n->help,sizeof n->help,1)||n->flags&~BBM_MENU_ENABLED||n->kind<1||n->kind>4)return BBM_ERR_ARG;
    if(n->kind==BBM_MENU_LINK){if(!path(n->target))return BBM_ERR_ARG;}
    else if(n->target[0])return BBM_ERR_ARG;
    if(n->kind==BBM_MENU_TOGGLE&&(n->value<0||n->value>1))return BBM_ERR_ARG;
    char self[192];snprintf(self,sizeof self,"%s/%s",m->entry->m.id,n->key);
    if(n->kind==BBM_MENU_SCREEN&&!strcmp(self,n->parent))return BBM_ERR_CONFLICT;
    return BBM_OK;
}
static bbm_status reg(bbm_handle m,const bbm_menu_node *n,bbm_id *out) {
    if(!out)return BBM_ERR_ARG;
    bbm_status rc=valid(m,n);if(rc)return rc;
    modhost *h=m->host;plat_mutex_lock(h->run_lock);
    size_t total=0;int included=0;
    for(size_t i=0;i<h->nmods;i++)if(h->mods[i].inst){
        if(h->mods[i].inst==m)included=1;
        for(size_t j=0;j<h->mods[i].inst->nres;j++)total+=h->mods[i].inst->res[j].kind==MH_RES_NATIVE_MENU;
    }
    if(!included)for(size_t j=0;j<m->nres;j++)total+=m->res[j].kind==MH_RES_NATIVE_MENU;
    for(size_t j=0;j<m->nres;j++)if(m->res[j].kind==MH_RES_NATIVE_MENU&&
        !strcmp(((owned_node*)m->res[j].p)->node.key,n->key)){rc=BBM_ERR_CONFLICT;goto done;}
    if(total>=MH_MENU_LIMIT){rc=BBM_ERR_BUDGET;goto done;}
    owned_node *p=calloc(1,sizeof *p);if(!p){rc=BBM_ERR_NOMEM;goto done;}
    p->node=*n;p->version=++h->menu->revision;
    *out=mh_res_add(m,MH_RES_NATIVE_MENU,p);
    if(!*out){free(p);rc=BBM_ERR_NOMEM;}
done:plat_mutex_unlock(h->run_lock);return rc;
}
static bbm_status update(bbm_handle m,bbm_id id,const bbm_menu_node *n) {
    bbm_status rc=valid(m,n);if(rc)return rc;
    plat_mutex_lock(m->host->run_lock);rc=BBM_ERR_NOTFOUND;
    for(size_t i=0;i<m->nres;i++)if(m->res[i].id==id&&m->res[i].kind==MH_RES_NATIVE_MENU){
        owned_node *p=m->res[i].p;
        if(strcmp(p->node.key,n->key)||strcmp(p->node.parent,n->parent)||p->node.kind!=n->kind){rc=BBM_ERR_ARG;break;}
        p->node=*n;p->version=++m->host->menu->revision;rc=BBM_OK;break;
    }
    plat_mutex_unlock(m->host->run_lock);return rc;
}
static bbm_status state(bbm_handle m,bbm_menu_state *out) {
    if(!m||!m->host||!m->host->menu||!out)return BBM_ERR_ARG;
    plat_mutex_lock(m->host->run_lock);*out=m->host->menu->state;
    out->revision=m->host->menu->revision;plat_mutex_unlock(m->host->run_lock);return BBM_OK;
}
static int compare(const void *aa,const void *bb) {
    const mh_menu_item *a=aa,*b=bb;int c=strcmp(a->node.parent,b->node.parent);if(c)return c;
    if(a->node.order!=b->node.order)return a->node.order<b->node.order?-1:1;
    c=strcmp(a->owner,b->owner);return c?c:strcmp(a->node.key,b->node.key);
}
void mh_menu_fill(bbm_host *a){a->menu_register=reg;a->menu_update=update;a->menu_state=state;}
void mh_menu_release(struct bbm_mod *m,void *p){++m->host->menu->revision;free(p);}
void mh_menu_bind(modhost *h,const mh_menu_sink *sink){
    if(h->menu->sink.stop)h->menu->sink.stop(h->menu->sink.user);
    h->menu->sink=*sink;h->menu->published=0;
    h->menu->state.capabilities=BBM_MENU_CAP_REGISTRY|sink->capabilities;
}
void mh_menu_start(modhost *h){
    h->menu=calloc(1,sizeof *h->menu);
    if(!h->menu)return; /* create checks this allocation before exposing APIs */
    h->menu->revision=1;h->menu->state.size=sizeof(bbm_menu_state);
    h->menu->state.capabilities=BBM_MENU_CAP_REGISTRY;
    mh_menu_sink sink={0};if(mh_menu_bridge_start(h,&sink))mh_menu_bind(h,&sink);
}
void mh_menu_tick(modhost *h){
    struct mh_menu_service *s=h->menu;if(!s)return;
    uint64_t hash=1469598103934665603ull;
    for(size_t i=0;i<h->nmods;i++){
        const char *strings[]={h->mods[i].m.id,h->mods[i].m.name};
        for(size_t j=0;j<2;j++)for(const unsigned char *p=(const unsigned char*)strings[j];*p;p++)hash=(hash^*p)*1099511628211ull;
        hash=(hash^(uint32_t)h->mods[i].state)*1099511628211ull;
    }
    if(hash!=s->mod_hash){s->mod_hash=hash;++s->revision;}
    if(s->sink.drain){
        bbm_menu_event pending[MH_MENU_QUEUE];uint64_t dropped=0;
        size_t count=s->sink.drain(s->sink.user,pending,MH_MENU_QUEUE,&dropped);
        if(dropped)mh_log(h,"bbmod.menu",BBM_LOG_WARN,"native menu events dropped: %llu",(unsigned long long)dropped);
        for(size_t i=0;i<count;i++){
            bbm_menu_event ev=pending[i];
            if(ev.kind==BBM_MENU_ACTIVATED||ev.kind==BBM_MENU_VALUE_CHANGED){
                int found=0;
                for(size_t j=0;j<h->nmods&&!found;j++){
                    struct bbm_mod *m=h->mods[j].inst;if(!m||m->faulted||m->bypassed||h->mods[j].state!=MODHOST_ACTIVE)continue;
                    for(size_t k=0;k<m->nres;k++)if(m->res[k].kind==MH_RES_NATIVE_MENU&&m->res[k].id==ev.node){
                        owned_node *p=m->res[k].p;
                        if(p->version!=ev.revision||!(p->node.flags&BBM_MENU_ENABLED))break;
                        if(p->node.kind!=BBM_MENU_ACTION&&p->node.kind!=BBM_MENU_TOGGLE)break;
                        mh_strlcpy(ev.owner,m->entry->m.id,sizeof ev.owner);mh_strlcpy(ev.key,p->node.key,sizeof ev.key);
                        if(p->node.kind==BBM_MENU_TOGGLE){p->node.value=!p->node.value;p->version=++s->revision;ev.kind=BBM_MENU_VALUE_CHANGED;ev.value=p->node.value;}
                        found=1;break;
                    }
                }
                if(!found)continue;
            }else if(ev.kind==BBM_MENU_OPENED||ev.kind==BBM_MENU_CLOSED){
                s->state.generation=ev.generation;
                mh_strlcpy(s->state.screen,ev.kind==BBM_MENU_OPENED?ev.screen:"",sizeof s->state.screen);
            }else if(ev.kind==BBM_MENU_SELECTION_CHANGED){
                if(!ev.control||ev.value<0||ev.previous_value<0)continue;
            }else if(ev.kind!=BBM_MENU_CREATED)continue;
            bbm_event event={BBM_EV_NATIVE_MENU,0,ev.generation,ev.node,&ev,sizeof ev};mh_dispatch_event(h,&event);
        }
    }
    if(s->sink.publish&&s->published!=s->revision){
        mh_menu_snapshot *copy=calloc(1,sizeof *copy);if(!copy)return;
        copy->revision=s->revision;
        for(size_t i=0;i<h->nmods;i++){
            if(copy->mod_count<MH_MENU_LIMIT){
                size_t at=copy->mod_count++;mh_strlcpy(copy->mods[at].id,h->mods[i].m.id,sizeof copy->mods[at].id);
                mh_strlcpy(copy->mods[at].name,h->mods[i].m.name[0]?h->mods[i].m.name:h->mods[i].m.id,sizeof copy->mods[at].name);
                copy->mods[at].state=h->mods[i].state;
            }
            struct bbm_mod *m=h->mods[i].inst;if(!m||m->faulted||m->bypassed||h->mods[i].state!=MODHOST_ACTIVE)continue;
            for(size_t j=0;j<m->nres&&copy->count<MH_MENU_LIMIT;j++)if(m->res[j].kind==MH_RES_NATIVE_MENU){
                owned_node *p=m->res[j].p;mh_menu_item *item=&copy->items[copy->count++];
                item->id=m->res[j].id;item->version=p->version;item->node=p->node;
                mh_strlcpy(item->owner,m->entry->m.id,sizeof item->owner);
                snprintf(item->path,sizeof item->path,"%s/%s",m->entry->m.id,p->node.key);
            }
        }
        qsort(copy->items,copy->count,sizeof copy->items[0],compare);
        if(s->sink.publish(s->sink.user,copy))s->published=copy->revision;
        free(copy);
    }
    if(s->notified!=s->revision){
        s->notified=s->revision;
        bbm_menu_event ev={.size=sizeof ev,.kind=BBM_MENU_REGISTRY_CHANGED,.revision=s->revision};
        bbm_event event={BBM_EV_NATIVE_MENU,0,0,0,&ev,sizeof ev};mh_dispatch_event(h,&event);
    }
}
void mh_menu_stop(modhost *h){if(!h->menu)return;if(h->menu->sink.stop)h->menu->sink.stop(h->menu->sink.user);free(h->menu);h->menu=NULL;}
