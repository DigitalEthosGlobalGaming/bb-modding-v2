/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Independently checked CUSA03173 1.09 layout facts; see equipment-ui-research.md. */
#include "equipment.h"
#include <string.h>
#include <limits.h>

static int ptr(mh_equipment_reader read, void *user, uintptr_t at, uintptr_t *out) {
    return read(user, at, out, sizeof *out) || !*out || (*out & 7) ? -1 : 0;
}

int mh_equipment_profile(uintptr_t base, mh_equipment_reader read, void *user) {
    static const struct { uintptr_t rva; unsigned char n, bytes[16]; } checks[] = {
        {0x14f3c19, 16, {0x48,0x8d,0xb3,0x40,0x02,0x00,0x00,0x48,0x8b,0x7d,0xa8,0xe8,0x37,0x43,0xfe,0xff}},
        {0x1895a62, 9, {0x48,0x8d,0x73,0x64,0xba,0x40,0x00,0x00,0x00}},
        {0x1895ae5, 10, {0x49,0x83,0xc4,0x04,0x49,0x83,0xfc,0x64,0x75,0xa1}},
        {0x1895c3b, 11, {0x8b,0x47,0x0c,0x01,0xc0,0x48,0x98,0x8b,0x74,0x87,0x64}},
        {0x1895cdb, 13, {0x8b,0x47,0x10,0x8d,0x44,0x00,0x01,0x48,0x98,0x8b,0x74,0x87,0x64}}
    };
    unsigned char bytes[16];
    if (!base || !read) return -1;
    for (size_t i=0; i<sizeof checks/sizeof checks[0]; ++i)
        if (base > UINTPTR_MAX-checks[i].rva || read(user, base+checks[i].rva, bytes, checks[i].n) ||
            memcmp(bytes, checks[i].bytes, checks[i].n)) return -1;
    return 0;
}

int mh_equipment_read(uintptr_t base, uintptr_t actor, mh_equipment_reader read,
                      void *user, mh_equipment_snapshot *out) {
    uintptr_t world, current, manager, character, owned, again;
    unsigned char first[0x9c], second[0x9c]; /* equipment+8 through +A3 */
    mh_equipment_snapshot result = {0};
    if (!out || !read || !base || base>UINTPTR_MAX-0x553e878 || !actor || actor>UINTPTR_MAX-0x3c0) return -1;
    memset(out, 0, sizeof *out);
    if (ptr(read,user,base+0x553e878,&world) || world>UINTPTR_MAX-0x60 ||
        ptr(read,user,world+0x60,&current) || current!=actor ||
        ptr(read,user,actor+0x3c0,&character) || character>UINTPTR_MAX-0x2e4 ||
        ptr(read,user,base+0x553b130,&manager) || manager>UINTPTR_MAX-8 ||
        ptr(read,user,manager+8,&owned) || owned!=character ||
        read(user,character+0x248,first,sizeof first) ||
        read(user,character+0x248,second,sizeof second) || memcmp(first,second,sizeof first) ||
        ptr(read,user,world+0x60,&again) || again!=actor ||
        ptr(read,user,actor+0x3c0,&again) || again!=character ||
        ptr(read,user,manager+8,&again) || again!=character) return -1;
    result.actor=actor; result.character=character;
    memcpy(&result.mode,first,4);
    memcpy(result.selections,first+4,24);
    memcpy(result.runtime_references,first+0x1c,64);
    memcpy(result.saved_item_ids,first+0x5c,64);
    result.selected_even=result.selections[0]<8 ? result.selections[0]*2 : UINT32_MAX;
    result.selected_odd=result.selections[1]<8 ? result.selections[1]*2+1 : UINT32_MAX;
    *out=result;
    return 0;
}
