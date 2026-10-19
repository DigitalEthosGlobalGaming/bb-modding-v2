#include "internal.h"
#include <assert.h>

typedef struct fake { char drawn[80], scope[256]; int begins,ends,depth; } fake;
typedef struct cb { fake *f; struct bbm_mod *mod; char letter; bbm_id remove; } cb;
static int begin(void *u,const char *title,const char *id) {
    fake *f=u; assert(!strcmp(title,"hooks.testing")); assert(!strcmp(id,"bbmod.shared-slot")); f->begins++; return 1;
}
static void end(void *u) { ((fake*)u)->ends++; }
static void push(void *u,const char *owner,const char *key) {
    fake *f=u;assert(f->depth==0);f->depth++;
    snprintf(f->scope,sizeof f->scope,"%s/%s",owner,key);
}
static void pop(void *u) { fake *f=u; assert(f->depth==1);f->depth--; }
static void draw(void *u) {
    cb *c=u; assert(c->f->depth==1);
    assert(strstr(c->f->scope,c->mod->entry->m.id)==c->f->scope);
    size_t n=strlen(c->f->drawn); c->f->drawn[n]=c->letter;c->f->drawn[n+1]=0;
    if(c->remove) { assert(mh_res_release_one(c->mod,c->remove)==0);c->remove=0; }
}
int main(void) {
    fake f={0};modhost_config cfg={0};cfg.cache_dir="ui-slot-test-cache";
    cfg.ui.user=&f;cfg.ui.begin_panel=begin;cfg.ui.end_panel=end;cfg.ui.push_scope=push;cfg.ui.pop_scope=pop;
    modhost *h=modhost_create(&cfg);assert(h);
    h->mods=calloc(2,sizeof *h->mods);h->nmods=h->capmods=2;
    strcpy(h->mods[0].m.id,"z.mod");strcpy(h->mods[1].m.id,"a.mod");
    struct bbm_mod a={.host=h,.entry=&h->mods[0]},b={.host=h,.entry=&h->mods[1]};
    h->mods[0].inst=&a;h->mods[1].inst=&b;h->mods[0].state=h->mods[1].state=MODHOST_ACTIVE;
    cb ca={&f,&a,'A',0},cb1={&f,&b,'B',0},cb2={&f,&b,'C',0}; bbm_id ia,ib,ic,tmp;
    assert(h->api.ui_contribute(&a,"hooks.testing","shared",10,draw,&ca,&ia)==0);
    assert(h->api.ui_contribute(&b,"hooks.testing","shared",10,draw,&cb1,&ib)==0);
    assert(h->api.ui_contribute(&b,"hooks.testing","first",-10,draw,&cb2,&ic)==0);
    assert(h->api.ui_contribute(&a,"hooks.testing","shared",0,draw,&ca,&tmp)==BBM_ERR_CONFLICT);
    assert(h->api.ui_contribute(&a,"bad##slot","x",0,draw,&ca,&tmp)==BBM_ERR_ARG);
    modhost_draw_panels(h);assert(!strcmp(f.drawn,"CBA")&&f.begins==1&&f.ends==1&&f.depth==0);
    /* A callback may release another contribution before it would be called. */
    f.drawn[0]=0;cb2.remove=ib;modhost_draw_panels(h);assert(!strcmp(f.drawn,"CA"));
    mh_res_release_all(&a);f.drawn[0]=0;modhost_draw_panels(h);assert(!strcmp(f.drawn,"C"));
    /* Reload-style remove/re-register does not accumulate duplicate callbacks. */
    for(int i=0;i<20;i++) {
        mh_res_release_all(&b);
        assert(h->api.ui_contribute(&b,"hooks.testing","first",0,draw,&cb2,&ic)==0);
        f.drawn[0]=0;modhost_draw_panels(h);assert(!strcmp(f.drawn,"C")&&b.nres==1);
    }
    mh_res_release_all(&b);h->mods[0].inst=h->mods[1].inst=NULL;
    modhost_destroy(h);puts("shared UI ordering, namespace, release and reload: OK");return 0;
}
