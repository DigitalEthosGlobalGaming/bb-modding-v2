#include "internal.h"
#include <assert.h>
typedef struct backend {uint64_t seq,active;int polls,cancels;bbm_text_request request;bbm_text_result result;} backend;
static bbm_status begin(void*u,const bbm_text_request*r,uint64_t*t){
    backend*b=u;if(b->active)return BBM_ERR_CONFLICT;b->request=*r;b->active=++b->seq;*t=b->active;return BBM_OK;
}
static bbm_status poll(void*u,uint64_t t,bbm_text_result*r){backend*b=u;if(t!=b->active)return BBM_ERR_NOTFOUND;b->polls++;*r=b->result;return BBM_OK;}
static void cancel(void*u,uint64_t t){backend*b=u;if(b->active==t){b->active=0;b->cancels++;}}
int main(void){
    _Static_assert(sizeof(bbm_text_request)==4232,"request ABI");
    _Static_assert(sizeof(bbm_text_result)==4112,"result ABI");
    backend b={0};b.result.size=sizeof b.result;
    modhost_config cfg={0};cfg.cache_dir="text-entry-test-cache";
    modhost*h=modhost_create(&cfg);assert(h);
    struct bbm_mod a={.host=h},other={.host=h};bbm_id id=0,second=0;
    bbm_text_request r={.size=sizeof r,.max_bytes=127};strcpy(r.title,"Label");strcpy(r.initial,"Original");
    assert(h->api.text_begin(&a,&r,&id)==BBM_ERR_UNSUPPORTED&&!id);
    h->cfg.text=(modhost_text_backend){&b,begin,poll,cancel};
    r.max_bytes=0;assert(h->api.text_begin(&a,&r,&id)==BBM_ERR_ARG);
    r.max_bytes=4;assert(h->api.text_begin(&a,&r,&id)==BBM_ERR_ARG);
    r.max_bytes=127;r.initial[0]=(char)0xc0;r.initial[1]=(char)0x80;r.initial[2]=0;
    assert(h->api.text_begin(&a,&r,&id)==BBM_ERR_ARG);strcpy(r.initial,"Original");
    assert(!h->api.text_begin(&a,&r,&id)&&id&&a.nres==1);
    strcpy(r.initial,"Changed after begin");assert(!strcmp(b.request.initial,"Original"));
    assert(h->api.text_begin(&other,&r,&second)==BBM_ERR_CONFLICT&&!second);
    bbm_text_result result;
    assert(h->api.text_poll(&other,id,&result)==BBM_ERR_NOTFOUND);
    assert(h->api.release(&other,id)==BBM_ERR_NOTFOUND);
    assert(!h->api.text_poll(&a,id,&result)&&result.state==BBM_TEXT_PENDING);
    strcpy(b.result.text,"Unicode \xe2\x9c\x93");b.result.state=BBM_TEXT_ACCEPTED;
    assert(!h->api.text_poll(&a,id,&result)&&result.state==BBM_TEXT_ACCEPTED&&!strcmp(result.text,b.result.text));
    int polls=b.polls;strcpy(b.result.text,"Later");assert(!h->api.text_poll(&a,id,&result)&&b.polls==polls&&!strcmp(result.text,"Unicode \xe2\x9c\x93"));
    assert(!h->api.release(&a,id)&&b.cancels==1);
    assert(h->api.text_poll(&a,id,&result)==BBM_ERR_NOTFOUND);
    for(int i=0;i<20;i++){
        assert(!h->api.text_begin(&a,&r,&id));mh_res_release_all(&a);assert(!b.active);
        assert(!h->api.text_begin(&other,&r,&second));assert(h->api.text_poll(&a,id,&result)==BBM_ERR_NOTFOUND);
        mh_res_release_all(&other);assert(!b.active);
    }
    assert(!h->api.text_begin(&a,&r,&id));b.result.text[0]=(char)0xed;b.result.text[1]=(char)0xa0;b.result.text[2]=(char)0x80;b.result.text[3]=0;
    assert(!h->api.text_poll(&a,id,&result)&&result.state==BBM_TEXT_FAILED&&result.status==BBM_ERR_STATE&&!result.text[0]);
    mh_res_release_all(&a);r.max_bytes=2;r.initial[0]=0;
    assert(!h->api.text_begin(&a,&r,&id));strcpy(b.result.text,"abc");
    assert(!h->api.text_poll(&a,id,&result)&&result.state==BBM_TEXT_FAILED);
    mh_res_release_all(&a);modhost_destroy(h);
    puts("owned text entry: UTF-8/length validation, copied input, cached results, owner isolation, unload and 20 reloads: OK");return 0;
}
