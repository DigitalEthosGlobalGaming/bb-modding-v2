/* Load two copies of the actual Rust diagnostic to exercise ownership/reload. */
#define main character_reader_test_main
#include "test_characters.c"
#undef main
#include "tcompat.h"
static int sections,depth,found,table_open,table_rows,table_column,first_health;
static int reference_seen,team_seen,script_seen,candidate_seen;
static int begin_ui(void*u,const char*t,const char*id){(void)u;(void)t;(void)id;return 1;}
static void end_ui(void*u){(void)u;assert(depth==0);}
static void push_ui(void*u,const char*owner,const char*key){(void)u;assert(!depth&&strstr(owner,"character-observer")&&!strcmp(key,"character-observer"));depth++;sections++;}
static void pop_ui(void*u){(void)u;assert(depth==1&&!table_open);depth--;}
static int header_ui(void*u,const char*t){(void)u;(void)t;return 1;}
static int checkbox_ui(void*u,const char*t,int32_t*v){(void)u;(void)t;(void)v;return 0;}
static void text_ui(void*u,const char*t){(void)u;if(strstr(t,"36 loaded characters"))found++;if(table_open&&table_rows==1&&table_column==3)first_health=atoi(t);
 if(strstr(t,"Drowned Beast")&&strstr(t,"[reference]"))reference_seen++;
 if(!strcmp(t,"Enemy1 (23)"))team_seen++;
 if(!strcmp(t,"9000 (unresolved)"))script_seen++;
 if(strstr(t,"Raw 9000 resembles Event9000 (action-state ID 462); mapping unverified"))candidate_seen++;
}
static int table_begin(void*u,const char*id,const char*const*headers,uint32_t count,float height,int32_t*sort,int32_t*desc){
 (void)u;assert(!table_open&&depth==1&&!strcmp(id,"characters")&&count==15&&height==360&&!strcmp(headers[3],"HP"));*sort=3;*desc=0;table_open=1;table_rows=0;table_column=-1;return 1;
}
static void table_row(void*u){(void)u;assert(table_open);table_rows++;table_column=-1;}
static void table_column_next(void*u){(void)u;assert(table_open);table_column++;assert(table_column<15);}
static void table_end(void*u){(void)u;assert(table_open&&table_rows==35&&table_column==14&&first_health==100);table_open=0;}
static void draw_ui(modhost*h,int expected){sections=found=reference_seen=team_seen=script_seen=candidate_seen=0;modhost_draw_panels(h);assert(sections==expected&&found>=expected&&depth==0&&!table_open);assert(reference_seen==expected&&team_seen==expected&&script_seen==expected&&candidate_seen==expected);}
int main(int argc,char**argv){
 if(argc!=2)return 2;Memory m={0};fixture(&m);p32(&m,ACTOR(1)+0x8f8,900);p32(&m,ACTOR(2)+0x8f8,100);char root[300],mods[400],cache[400];tc_tmpdir(root,sizeof root,"character-mod");snprintf(mods,sizeof mods,"%s/mods",root);snprintf(cache,sizeof cache,"%s/cache",root);
 p32(&m,ACTOR(1)+0xc80,112401);p32(&m,ACTOR(1)+0xc98,112000);p32(&m,ACTOR(1)+0x88,23);p32(&m,ACTOR(1)+0xc40,9000);
 tc_make_mod(mods,"a",argv[1],"[mod]\nid=\"example.character-observer-a\"\nabi=\"1.6\"\ndll=\"m.dll\"\n");
 tc_make_mod(mods,"b",argv[1],"[mod]\nid=\"example.character-observer-b\"\nabi=\"1.6\"\ndll=\"m.dll\"\n");
 modhost_config cfg={0};cfg.mods_dir=mods;cfg.cache_dir=cache;cfg.eboot_base=BASE;cfg.eboot_size=CHAR_IMAGE_SIZE;cfg.platform.user=&m;cfg.platform.read_safe=read_host;
 cfg.ui.begin_panel=begin_ui;cfg.ui.end_panel=end_ui;cfg.ui.push_scope=push_ui;cfg.ui.pop_scope=pop_ui;cfg.ui.collapsing_header=header_ui;cfg.ui.checkbox=checkbox_ui;cfg.ui.text=text_ui;
 cfg.ui.table_begin=table_begin;cfg.ui.table_next_row=table_row;cfg.ui.table_next_column=table_column_next;cfg.ui.table_end=table_end;
 modhost*h=modhost_create(&cfg);assert(h&&modhost_scan(h)==2);modhost_frame(h);draw_ui(h,2);
 for(int i=0;i<10;i++){assert(!modhost_reload_now(h,"example.character-observer-a"));modhost_frame(h);draw_ui(h,2);}
 modhost_request_unload(h,"example.character-observer-a");modhost_frame(h);draw_ui(h,1);
 modhost_request_unload(h,"example.character-observer-b");modhost_frame(h);draw_ui(h,0);
 unsigned reads=m.reads;plat_sleep_ms(260);mh_characters_tick(h);assert(m.reads==reads);
 modhost_destroy(h);free(m.b);tc_rmtree(root);puts("two actual character observer DLLs: UI scopes, shared cache, ten reloads, independent unload, idle: OK");return 0;
}
