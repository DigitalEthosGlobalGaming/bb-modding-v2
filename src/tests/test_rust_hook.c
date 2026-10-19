/* The Rust SDK's detour! macro end to end (needs Zydis): hooks a real function,
 * calls the original through the trampoline, falls back to the original when
 * the body panics, and unhooks cleanly. */
#include "tcompat.h"
#include "modhost.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); fails++; } } while (0)
typedef __attribute__((sysv_abi)) int (*fn_t)(int);

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    char root[300], mods[400], cache[400];
    tc_tmpdir(root, sizeof root, "bbmod-rhook");
    snprintf(mods, sizeof mods, "%s/mods", root);
    snprintf(cache, sizeof cache, "%s/cache", root);
    tc_make_mod(mods, "hook", argv[1], "[mod]\nid = \"rust.hook\"\ndll = \"m.dll\"\n");
    uint8_t *e = tc_alloc_rwx(16384);
    static const uint8_t good[] = {0x55, 0x48, 0x89, 0xE5, 0x89, 0xF8, 0x83, 0xC0, 0x05,
        0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90, 0x5D, 0xC3};
    memcpy(e + 0x200, good, sizeof good);
    fn_t f = (fn_t)(void *)(e + 0x200);
    modhost_config cfg = {0};
    cfg.mods_dir = mods; cfg.cache_dir = cache;
    cfg.eboot_base = (uintptr_t)e; cfg.eboot_size = 16384;
    modhost *h = modhost_create(&cfg);
    CHECK(modhost_scan(h) == 1);
    modhost_mod_view v;
    modhost_mod_view_at(h, 0, &v);
    CHECK(v.state == MODHOST_ACTIVE);
    CHECK(f(7) == 24);   /* (7 + 5) * 2 through the Rust detour */
    CHECK(f(13) == 18);  /* body panicked: original called instead, no crash */
    CHECK(f(1) == 12);   /* still hooked after the panic */
    for (int i = 0; i < 50; i++) CHECK(modhost_reload_now(h, "rust.hook") == 0);
    CHECK(f(7) == 24);
    CHECK(modhost_unload_now(h, "rust.hook") == 0);
    CHECK(f(7) == 12 && memcmp(e + 0x200, good, sizeof good) == 0);
    modhost_destroy(h);
    tc_rmtree(root);
    if (fails) return 1;
    puts("rust detour test passed");
    return 0;
}
