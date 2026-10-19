/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BBMOD_INVENTORY_H
#define BBMOD_INVENTORY_H
#include <stdint.h>
#include <stddef.h>
#define MH_INVENTORY_LIMIT 4096
#define MH_INVENTORY_ID_RESOLVED 1u
typedef int (*mh_inventory_reader)(void *user, uintptr_t address, void *out, size_t size);
typedef struct mh_inventory_entry {
    uint32_t slot, reference, encoded_item_id, quantity;
    uint32_t metadata_04, metadata_0c, flags, reserved;
} mh_inventory_entry;
typedef struct mh_inventory_snapshot {
    uintptr_t actor, character;
    uint32_t slot_count, count;
    mh_inventory_entry entries[MH_INVENTORY_LIMIT];
} mh_inventory_snapshot;
/* Profile and full carried-inventory enumeration. No native calls or writes.
 * Storage/repository is intentionally not interpreted as carried inventory. */
int mh_inventory_profile(uintptr_t base,mh_inventory_reader read,void *user);
int mh_inventory_read(uintptr_t base,uintptr_t actor,mh_inventory_reader read,void *user,mh_inventory_snapshot *out);
#endif
