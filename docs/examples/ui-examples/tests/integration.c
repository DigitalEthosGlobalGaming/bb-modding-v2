/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"
#include "tcompat.h"
#include <assert.h>
typedef struct {
 char owner[100],key[100],action_key[100],action[100];
 int checkbox_value,depth,settings_draws,other_draws;
 unsigned long long dashboard,settings;
} Ui;
static int begin(void*u,const char*t,const char*id){(void)u;(void)t;(void)id;return 1;}
static void end(void*u){(void)u;}
static void push(void*u,const char*o,const char*k){Ui*s=u;assert(!s->depth);s->depth=1;snprintf(s->owner,sizeof s->owner,"%s",o);snprintf(s->key,sizeof s->key,"%s",k);if(!strcmp(o,"example.ui-examples")&&!strcmp(k,"example-controls"))s->settings_draws++;}
static void pop(void*u){Ui*s=u;assert(s->depth==1);s->depth=0;s->owner[0]=s->key[0]=0;}
static int matches(Ui*s,const char*label){return !strcmp(s->owner,"example.ui-examples")&&!strcmp(s->key,s->action_key)&&!strcmp(label,s->action);}
static int button(void*u,const char*label){Ui*s=u;if(matches(s,label)){s->action[0]=0;return 1;}return 0;}
static int checkbox(void*u,const char*label,int32_t*value){Ui*s=u;if(matches(s,label)){s->action[0]=0;*value=s->checkbox_value;return 1;}return 0;}
static void text(void*u,const char*t){Ui*s=u;if(strcmp(s->owner,"example.ui-examples"))return;if(!strncmp(t,"Dashboard counter: ",19))sscanf(t+19,"%llu",&s->dashboard);if(!strncmp(t,"Settings counter: ",18))sscanf(t+18,"%llu",&s->settings);}
static void other(void*u){Ui*s=u;assert(!strcmp(s->owner,"example.hooks-observer"));s->other_draws++;}
static void action(modhost*h,Ui*s,const char*key,const char*label,int value){snprintf(s->action_key,sizeof s->action_key,"%s",key);snprintf(s->action,sizeof s->action,"%s",label);s->checkbox_value=value;modhost_draw_panels(h);assert(!s->action[0]);modhost_frame(h);modhost_draw_panels(h);}
static struct bbm_mod*find(modhost*h,const char*id){for(size_t i=0;i<h->nmods;i++)if(!strcmp(h->mods[i].m.id,id))return h->mods[i].inst;return NULL;}
int main(int argc,char**argv){
 if(argc!=3)return 2;char root[300],mods[400],cache[400];tc_tmpdir(root,sizeof root,"bbmod-ui-example");snprintf(mods,sizeof mods,"%s/mods",root);snprintf(cache,sizeof cache,"%s/cache",root);
 tc_make_mod(mods,"ui",argv[1],"[mod]\nid = \"example.ui-examples\"\nabi = \"1.4\"\ndll = \"m.dll\"\n");
 tc_make_mod(mods,"observer",argv[2],"[mod]\nid = \"example.hooks-observer\"\nabi = \"1.4\"\ndll = \"m.dll\"\n");
 Ui ui={0};modhost_config cfg={0};cfg.mods_dir=mods;cfg.cache_dir=cache;cfg.ui.user=&ui;cfg.ui.begin_panel=begin;cfg.ui.end_panel=end;cfg.ui.push_scope=push;cfg.ui.pop_scope=pop;cfg.ui.button=button;cfg.ui.checkbox=checkbox;cfg.ui.text=text;
 modhost*h=modhost_create(&cfg);assert(h);assert(modhost_scan(h)==2);assert(find(h,"example.ui-examples"));struct bbm_mod*observer=find(h,"example.hooks-observer");assert(observer);
 bbm_id other_id;assert(!h->api.ui_contribute(observer,"bbmod.settings","other-mod",200,other,&ui,&other_id));
 modhost_frame(h);modhost_draw_panels(h);assert(ui.settings_draws==1&&ui.other_draws==1);
 action(h,&ui,"example-overview","Increment",0);assert(ui.dashboard==1&&ui.settings==0);
 action(h,&ui,"example-controls","Increment",0);assert(ui.dashboard==1&&ui.settings==1);
 action(h,&ui,"example-overview","Increment",0);assert(ui.dashboard==2&&ui.settings==1);
 assert(!modhost_reload_now(h,"example.ui-examples"));modhost_frame(h);modhost_draw_panels(h);assert(ui.dashboard==2&&ui.settings==1);
 for(int i=0;i<20;i++){
  action(h,&ui,"example-overview","Show example settings contribution",0);
  int own=ui.settings_draws,other_count=ui.other_draws;modhost_draw_panels(h);assert(ui.settings_draws==own&&ui.other_draws==other_count+1);
  assert(!modhost_reload_now(h,"example.ui-examples"));modhost_frame(h);own=ui.settings_draws;modhost_draw_panels(h);assert(ui.settings_draws==own&&ui.dashboard==2);
  action(h,&ui,"example-overview","Show example settings contribution",1);
  own=ui.settings_draws;modhost_draw_panels(h);assert(ui.settings_draws==own+1&&ui.settings==1);
 }
 modhost_request_unload(h,"example.ui-examples");modhost_frame(h);int own=ui.settings_draws,other_count=ui.other_draws;modhost_draw_panels(h);assert(ui.settings_draws==own&&ui.other_draws==other_count+1);
 modhost_destroy(h);tc_rmtree(root);puts("actual UI DLL: same-label independence, shared-slot ownership, 20 hide/show/reload cycles and state: OK");return 0;
}
