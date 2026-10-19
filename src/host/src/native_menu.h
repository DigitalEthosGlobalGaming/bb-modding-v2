/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MH_NATIVE_MENU_H
#define MH_NATIVE_MENU_H
#include "internal.h"
#define MH_MENU_LIMIT 1024
#define MH_MENU_VISIBLE_ROWS 6
#define MH_MENU_QUEUE 128
typedef struct mh_menu_item {
    bbm_id id;
    uint64_t version;
    char owner[128], path[192];
    bbm_menu_node node;
} mh_menu_item;
typedef struct mh_menu_snapshot {
    uint64_t revision;
    uint32_t count, mod_count;
    mh_menu_item items[MH_MENU_LIMIT];
    struct {char id[128],name[128];uint32_t state;} mods[MH_MENU_LIMIT];
} mh_menu_snapshot;
/* Adapter boundary. Engine threads only see copies and numeric tokens. */
typedef struct mh_menu_sink {
    void *user;
    int (*publish)(void*, const mh_menu_snapshot*); /* 0 = busy, retry next tick */
    size_t (*drain)(void*, bbm_menu_event*, size_t, uint64_t*);
    void (*stop)(void*);
    uint32_t capabilities;
} mh_menu_sink;
void mh_menu_bind(modhost*, const mh_menu_sink*);
int mh_menu_bridge_start(modhost*, mh_menu_sink*);
int mh_menu_bridge_resolve(modhost*,const char*,char*,size_t);
#endif
