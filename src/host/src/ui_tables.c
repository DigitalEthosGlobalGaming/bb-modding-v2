/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"
#include <math.h>
static int active(bbm_handle m){return m&&m->host->current_panel_mod==m;}
static int32_t begin(bbm_handle m,const char*id,const char*const*headers,uint32_t count,float height,int32_t*sort,int32_t*descending){
    if(!active(m)||m->ui_table_open||!id||!*id||!headers||count<1||count>32||!isfinite(height)||height<64||height>1200||!sort||!descending)return -1;
    for(uint32_t i=0;i<count;i++)if(!headers[i]||strlen(headers[i])>128)return -1;
    modhost_ui_backend*u=&m->host->cfg.ui;
    if(!u->table_begin||!u->table_next_row||!u->table_next_column||!u->table_end)return -1;
    if(*sort<0||(uint32_t)*sort>=count)*sort=0;
    *descending=*descending!=0;
    int visible=u->table_begin(u->user,id,headers,count,height,sort,descending);
    if(visible>0){m->ui_table_open=1;return 1;}return 0;
}
static void row(bbm_handle m){if(active(m)&&m->ui_table_open)m->host->cfg.ui.table_next_row(m->host->cfg.ui.user);}
static void column(bbm_handle m){if(active(m)&&m->ui_table_open)m->host->cfg.ui.table_next_column(m->host->cfg.ui.user);}
void mh_ui_tables_cleanup(struct bbm_mod*m){
    if(m->ui_table_open){m->host->cfg.ui.table_end(m->host->cfg.ui.user);m->ui_table_open=0;}
}
static void end(bbm_handle m){if(active(m))mh_ui_tables_cleanup(m);}
void mh_ui_tables_fill(bbm_host*a){a->ui_table_begin=begin;a->ui_table_next_row=row;a->ui_table_next_column=column;a->ui_table_end=end;}
