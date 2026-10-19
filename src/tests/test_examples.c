/* The example mods against a 1.09-sized stand-in image:
 *   live-patches: clicking "Can't die" applies the real XML entry, a hot reload
 *   re-applies it (restore_state), unload restores every byte;
 *   devtools: every section draws without errors. */
#include "tcompat.h"
#include "modhost.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); fails++; } } while (0)

static const char *click; /* checkbox label to click on the next draw */
static int texts, errors;
static char all_text[4096];
static int begin(void *u, const char *t, const char *id) { (void)u; (void)t; (void)id; return 1; }
static void end(void *u) { (void)u; }
static void text(void *u, const char *s) {
    (void)u; texts++;
    if (strlen(all_text) + strlen(s) + 2 < sizeof all_text) { strcat(all_text, s); strcat(all_text, "\n"); }
}
static void colored(void *u, float r, float g, float b, float a, const char *s) {
    (void)u; (void)r; (void)g; (void)b; (void)a; errors++; fprintf(stderr, "panel error: %s\n", s);
}
static int checkbox(void *u, const char *label, int32_t *v) {
    (void)u;
    if (click && !strcmp(label, click)) { *v = !*v; click = NULL; return 1; }
    return 0;
}
static int header(void *u, const char *l) { (void)u; (void)l; return 1; }
static int button(void *u, const char *l) { (void)u; return !strcmp(l, "Read 64 bytes") || !strcmp(l, "Scan"); }
static int input(void *u, const char *l, char *buf, size_t cap) {
    (void)u;
    if (!strcmp(l, "address##peek") || !strcmp(l, "address##conv")) { snprintf(buf, cap, "0x400000"); return 1; }
    if (!strcmp(l, "pattern")) { snprintf(buf, cap, "DE AD ?? EF"); return 1; }
    return 0;
}

static uint64_t sum(const uint8_t *p, size_t n) {
    uint64_t s = 0;
    for (size_t i = 0; i < n; i += 4) s = s * 1099511628211ull + p[i] + ((uint64_t)p[i + 1] << 8);
    return s;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: test_examples <live_patches.so> <devtools.so> <Bloodborne.xml>\n"); return 2; }
    char root[300], mods[400], cache[400], toml[1500];
    tc_tmpdir(root, sizeof root, "bbmod-ex");
    snprintf(mods, sizeof mods, "%s/mods", root);
    snprintf(cache, sizeof cache, "%s/cache", root);
    snprintf(toml, sizeof toml, "[mod]\nid = \"example.live-patches\"\nabi = \"1.1\"\ndll = \"m.dll\"\n[patches]\nxml = \"%s\"\n", argv[3]);
    tc_make_mod(mods, "live", argv[1], toml);
    tc_make_mod(mods, "dev", argv[2], "[mod]\nid = \"example.devtools\"\nabi = \"1.1\"\ndll = \"m.dll\"\n");

    size_t size = 0x6000000;
    uint8_t *img = tc_alloc_rwx(size);
    for (size_t i = 0; i < size; i += 4096) img[i] = (uint8_t)(i >> 12);
    img[0x1234] = 0xDE; img[0x1235] = 0xAD; img[0x1236] = 0x00; img[0x1237] = 0xEF;

    modhost_config cfg = {0};
    cfg.mods_dir = mods; cfg.cache_dir = cache;
    cfg.eboot_base = (uintptr_t)img; cfg.eboot_size = size; cfg.ps4_eboot_base = 0x400000;
    cfg.ui.begin_panel = begin; cfg.ui.end_panel = end; cfg.ui.text = text; cfg.ui.text_colored = colored;
    cfg.ui.checkbox = checkbox; cfg.ui.collapsing_header = header; cfg.ui.button = button; cfg.ui.input_text = input;
    modhost *h = modhost_create(&cfg);
    CHECK(modhost_scan(h) == 2);
    for (size_t i = 0; i < 2; i++) { modhost_mod_view v; modhost_mod_view_at(h, i, &v); CHECK(v.state == MODHOST_ACTIVE); }

    uint64_t s0 = sum(img, size);
    click = "Can't die";
    modhost_draw_panels(h);
    CHECK(click == NULL);
    uint64_t s1 = sum(img, size);
    CHECK(s1 != s0);                                    /* the real XML entry went in */
    CHECK(modhost_reload_now(h, "example.live-patches") == 0);
    CHECK(sum(img, size) == s1);                        /* re-applied after hot reload */
    CHECK(modhost_unload_now(h, "example.live-patches") == 0);
    CHECK(sum(img, size) == s0);                        /* every byte restored */

    all_text[0] = 0;
    modhost_draw_panels(h);                             /* devtools: all sections open */
    modhost_draw_panels(h);
    CHECK(strstr(all_text, "eboot at host") != NULL);
    CHECK(strstr(all_text, "PS4 0x400000") != NULL);
    CHECK(strstr(all_text, "found at PS4 0x401234") != NULL);
    CHECK(errors == 0);

    modhost_destroy(h);
    tc_free_rwx(img, size);
    tc_rmtree(root);
    if (fails) return 1;
    puts("example mods test passed");
    return 0;
}
