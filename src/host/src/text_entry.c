/* Owned system text entry. All backend calls are serialized by run_lock.
 * No backend stores a DLL callback or a pointer into a mod's memory. */
#include "internal.h"
typedef struct owned_text {uint64_t token;uint32_t max_bytes;bbm_text_result result;} owned_text;
static int valid_utf8(const char *s,size_t cap,size_t *length){
    size_t n=0;while(n<cap&&s[n])++n;if(n==cap)return 0;
    for(size_t i=0;i<n;){
        unsigned c=(unsigned char)s[i++],value,min;int extra;
        if(c<0x80){if(c<32||c==127)return 0;continue;}
        if(c>=0xc2&&c<=0xdf){value=c&31;min=0x80;extra=1;}
        else if(c>=0xe0&&c<=0xef){value=c&15;min=0x800;extra=2;}
        else if(c>=0xf0&&c<=0xf4){value=c&7;min=0x10000;extra=3;}else return 0;
        if(i+(size_t)extra>n)return 0;
        while(extra--){c=(unsigned char)s[i++];if((c&0xc0)!=0x80)return 0;value=value<<6|(c&63);}
        if(value<min||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return 0;
    }
    if(length)*length=n;return 1;
}
static bbm_status begin(bbm_handle m,const bbm_text_request *request,bbm_id *out){
    if(!m||!m->host||!request||!out||m->faulted||m->bypassed)return BBM_ERR_ARG;
    *out=0;size_t length=0;
    if(request->size!=sizeof *request||request->max_bytes<1||request->max_bytes>4095||
       !request->title[0]||!valid_utf8(request->title,sizeof request->title,NULL)||
       !valid_utf8(request->initial,sizeof request->initial,&length)||length>request->max_bytes)return BBM_ERR_ARG;
    modhost *h=m->host;modhost_text_backend *b=&h->cfg.text;
    if(!b->begin||!b->poll||!b->cancel)return BBM_ERR_UNSUPPORTED;
    owned_text *p=calloc(1,sizeof *p);if(!p)return BBM_ERR_NOMEM;
    p->max_bytes=request->max_bytes;p->result.size=sizeof p->result;
    plat_mutex_lock(h->run_lock);
    bbm_status rc=b->begin(b->user,request,&p->token);
    if(!rc&&!p->token)rc=BBM_ERR_STATE;
    if(!rc){*out=mh_res_add(m,MH_RES_TEXT,p);if(!*out){b->cancel(b->user,p->token);rc=BBM_ERR_NOMEM;}}
    plat_mutex_unlock(h->run_lock);
    if(rc)free(p);return rc;
}
static bbm_status poll(bbm_handle m,bbm_id id,bbm_text_result *out){
    if(!m||!m->host||!out||m->faulted||m->bypassed)return BBM_ERR_ARG;
    modhost *h=m->host;plat_mutex_lock(h->run_lock);owned_text *p=NULL;
    for(size_t i=0;i<m->nres;i++)if(m->res[i].id==id&&m->res[i].kind==MH_RES_TEXT){p=m->res[i].p;break;}
    if(!p){plat_mutex_unlock(h->run_lock);return BBM_ERR_NOTFOUND;}
    if(p->result.state==BBM_TEXT_PENDING){
        bbm_text_result next={.size=sizeof next};
        bbm_status rc=h->cfg.text.poll(h->cfg.text.user,p->token,&next);size_t length=0;
        if(!rc&&(next.size!=sizeof next||next.reserved||next.state>BBM_TEXT_FAILED||(next.state!=BBM_TEXT_FAILED&&next.status!=BBM_OK)||
          (next.state==BBM_TEXT_ACCEPTED&&(!valid_utf8(next.text,sizeof next.text,&length)||length>p->max_bytes))))rc=BBM_ERR_STATE;
        if(rc){memset(&next,0,sizeof next);next.size=sizeof next;next.state=BBM_TEXT_FAILED;next.status=rc;}
        if(next.state==BBM_TEXT_FAILED&&next.status==BBM_OK)next.status=BBM_ERR;
        if(next.state!=BBM_TEXT_ACCEPTED)memset(next.text,0,sizeof next.text);
        p->result=next;
    }
    *out=p->result;plat_mutex_unlock(h->run_lock);return BBM_OK;
}
void mh_text_release(struct bbm_mod *m,void *record){
    owned_text *p=record;if(!p)return;
    m->host->cfg.text.cancel(m->host->cfg.text.user,p->token);free(p);
}
void mh_text_fill(bbm_host *api){api->text_begin=begin;api->text_poll=poll;}
