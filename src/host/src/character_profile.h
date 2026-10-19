#if __has_include("character_profile.local.h")
#include "character_profile.local.h"
#else
/* Authored fail-closed fallback; toolkit profiles are generated locally. */
static const struct character_check {uintptr_t rva;size_t n;unsigned char bytes[32];} character_checks[]={{0,4,{0x42,0x42,0x54,0x4b}}};
#endif
