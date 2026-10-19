/* The host's XML patch reader against bbport's real Bloodborne.xml: every
 * entry applies to a stand-in eboot image and reverts byte for byte. */
#include "tcompat.h"
#include "modhost.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); fails++; } } while (0)
static int ok = -1, bad = -1;
static void notify(void *u, const char *id, const char *m) {
    (void)u; (void)id;
    if (sscanf(m, "xml ok=%d bad=%d", &ok, &bad) != 2) fprintf(stderr, "%s\n", m);
}

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    char root[300], mods[400], cache[400], toml[1200];
    tc_tmpdir(root, sizeof root, "bbmod-xml");
    snprintf(mods, sizeof mods, "%s/mods", root);
    snprintf(cache, sizeof cache, "%s/cache", root);
    snprintf(toml, sizeof toml, "[mod]\nid = \"xml\"\ndll = \"m.dll\"\n[test]\nxml = \"%s\"\n", argv[2]);
    tc_make_mod(mods, "xml", argv[1], toml);

    size_t size = 0x6000000; /* covers 0x400000..0x6400000 */
    uint8_t *img = tc_alloc_rwx(size);
    CHECK(img != NULL);
    for (size_t i = 0; i < size; i += 4096) img[i] = (uint8_t)(i >> 12); /* touch a pattern */
    uint64_t sum0 = 0;
    for (size_t i = 0; i < size; i += 64) sum0 = sum0 * 31 + img[i] + img[i + 17];

    modhost_config cfg = {0};
    cfg.mods_dir = mods; cfg.cache_dir = cache;
    cfg.eboot_base = (uintptr_t)img; cfg.eboot_size = size; cfg.ps4_eboot_base = 0x400000;
    cfg.ui.notify = notify;
    modhost *h = modhost_create(&cfg);
    CHECK(modhost_scan(h) == 1);
    printf("Bloodborne.xml: %d entries applied and reverted, %d failed\n", ok, bad);
    CHECK(ok >= 60);
    CHECK(bad == 0);
    uint64_t sum1 = 0;
    for (size_t i = 0; i < size; i += 64) sum1 = sum1 * 31 + img[i] + img[i + 17];
    CHECK(sum0 == sum1);
    modhost_destroy(h);
    tc_free_rwx(img, size);
    tc_rmtree(root);
    if (fails) return 1;
    puts("xml patch test passed");
    return 0;
}
