#include "internal.h"
#include <assert.h>
#include <math.h>
static int opened,ended,rows,columns;
static int begin(void*u,const char*id,const char*const*headers,uint32_t n,float height,int32_t*sort,int32_t*desc){
 (void)u;assert(!opened&&n==2&&!strcmp(id,"test")&&!strcmp(headers[1],"HP")&&height==300);opened=1;*sort=1;*desc=1;return 1;
}
static void row(void*u){(void)u;assert(opened);rows++;}
static void col(void*u){(void)u;assert(opened);columns++;}
static void end(void*u){(void)u;assert(opened);opened=0;ended++;}
int main(void){
 modhost_config cfg={0};cfg.mods_dir="ui-table-no-mods";cfg.cache_dir="ui-table-test-cache";
 cfg.ui.table_begin=begin;cfg.ui.table_next_row=row;cfg.ui.table_next_column=col;cfg.ui.table_end=end;
 modhost*h=modhost_create(&cfg);assert(h);struct bbm_mod a={.host=h},b={.host=h};const char*headers[]={"Name","HP"};int32_t sort=0,desc=0;
 assert(h->api.ui_table_begin(&a,"test",headers,2,300,&sort,&desc)==-1);h->current_panel_mod=&a;
 assert(h->api.ui_table_begin(&a,"test",headers,33,300,&sort,&desc)==-1);
 assert(h->api.ui_table_begin(&a,"test",headers,2,NAN,&sort,&desc)==-1);
 assert(h->api.ui_table_begin(&a,"test",headers,2,300,&sort,&desc)==1&&sort==1&&desc==1);
 assert(h->api.ui_table_begin(&a,"test",headers,2,300,&sort,&desc)==-1);h->api.ui_table_next_row(&a);h->api.ui_table_next_column(&a);
 h->api.ui_table_end(&b);assert(opened);h->api.ui_table_end(&a);assert(!opened&&rows==1&&columns==1&&ended==1);
 h->api.ui_table_end(&a);assert(ended==1);
 assert(h->api.ui_table_begin(&a,"test",headers,2,300,&sort,&desc)==1);mh_ui_tables_cleanup(&a);assert(!opened&&ended==2); /* guarded fault cleanup */
 h->cfg.ui.table_begin=NULL;assert(h->api.ui_table_begin(&a,"test",headers,2,300,&sort,&desc)==-1);
 h->current_panel_mod=NULL;modhost_destroy(h);puts("scoped UI tables: bounds, sort output, owner isolation, balanced cleanup, unsupported backend: OK");return 0;
}
