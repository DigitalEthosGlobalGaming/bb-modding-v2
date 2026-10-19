#if __has_include("native_menu_profile.local.h")
#include "native_menu_profile.local.h"
#else
/* Authored fail-closed fallback; toolkit profiles are generated locally. */
static const struct {uintptr_t rva;unsigned char bytes[20];} menu_signatures[]={{0,{0x42,0x42,0x54,0x4b}}};
#endif
