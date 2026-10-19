/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "bbmod.h"
typedef int (*mh_character_reader)(void*,uintptr_t,void*,size_t);
typedef struct { bbm_character value; uintptr_t actor, data; } mh_character;
typedef struct { uintptr_t world, player; uint32_t count; mh_character entries[512]; } mh_character_sample;
int mh_characters_profile(uintptr_t,mh_character_reader,void*);
bbm_status mh_characters_read(uintptr_t,mh_character_reader,void*,mh_character_sample*);
