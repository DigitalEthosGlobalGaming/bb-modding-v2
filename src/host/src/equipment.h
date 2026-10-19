/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MH_EQUIPMENT_H
#define MH_EQUIPMENT_H
#include <stddef.h>
#include <stdint.h>

/* Diagnostic layout, deliberately not an inventory or named-hand API. */
typedef struct mh_equipment_snapshot {
    uintptr_t actor, character;
    uint32_t mode;
    uint32_t selections[6];
    uint32_t runtime_references[16];
    uint32_t saved_item_ids[16];
    /* Native accessor indices; UINT32_MAX means selector outside array. */
    uint32_t selected_even, selected_odd;
} mh_equipment_snapshot;

/* Reader returns zero on success; never invokes native game routines. */
typedef int (*mh_equipment_reader)(void *user, uintptr_t address, void *out, size_t size);
int mh_equipment_profile(uintptr_t base, mh_equipment_reader read, void *user);
/* Caller validates profile once per image. 0 success, -1 unavailable/unstable. */
int mh_equipment_read(uintptr_t base, uintptr_t actor,
                      mh_equipment_reader read, void *user,
                      mh_equipment_snapshot *out);
#endif
