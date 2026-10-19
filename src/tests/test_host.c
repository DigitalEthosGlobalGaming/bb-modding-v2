/* Host test harness: loads test_mod against a fake eboot buffer. */
#include "tcompat.h"
#include "modhost.h"

static int fails;
#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } \
    } while (0)

static uint8_t keys[512];
static uint32_t pad;
static const uint8_t *kb(void *u, int *n) { (void)u; *n = 512; return keys; }
static uint32_t padb(void *u) { (void)u; return pad; }
static char panel_text[128];
static int begin_panel(void *u, const char *t, const char *id) { (void)u; (void)t; (void)id; return 1; }
static void end_panel(void *u) { (void)u; }
static void ui_text(void *u, const char *s) { (void)u; snprintf(panel_text, sizeof panel_text, "%s", s); }

/* Overlay thread stand-in: draws panels as fast as it can. */
static volatile int stop_drawer;
static volatile long draws;
static modhost *drawer_host;
static void *drawer(void *u) {
    (void)u;
    while (!__atomic_load_n(&stop_drawer, __ATOMIC_ACQUIRE)) {
        if (modhost_try_lock(drawer_host)) {
            modhost_mod_view v;
            for (size_t i = 0; i < modhost_mod_count(drawer_host); i++) modhost_mod_view_at(drawer_host, i, &v);
            modhost_draw_panels(drawer_host);
            modhost_unlock(drawer_host);
            draws++;
        }
        char out[256];
        modhost_vfs_resolve(drawer_host, "dvdroot_ps4/test/a.bin", out, sizeof out); /* file I/O thread */
    }
    return NULL;
}

static void logcb(void *u, const char *id, int lvl, const char *msg) {
    (void)u;
    if (lvl >= BBM_LOG_WARN) fprintf(stderr, "[%s] %s\n", id ? id : "host", msg);
}

int main(void) {
    char root[300], mods[400], cache[400];
    tc_tmpdir(root, sizeof root, "bbmod-test");
    snprintf(mods, sizeof mods, "%s/mods", root);
    snprintf(cache, sizeof cache, "%s/cache", root);
    tc_make_mod(mods, "test_mod", TEST_MOD_PATH,
                "[mod]\nid = \"test.mod\"\nname = \"Test\"\nversion = \"1.2.3\"\ndll = \"m.dll\"\n");

    size_t sz = 4096 * 4;
    uint8_t *eboot = tc_alloc_rwx(sz);
    CHECK(eboot != NULL);
    for (int i = 0; i < 0x40; i++) eboot[0x10 + i] = (uint8_t)(0xC0 + i);
    uint8_t before[4];
    memcpy(before, eboot + 0x10, 4);
    uintptr_t slot_orig = 0xABCDEF;
    memcpy(eboot + 0x100, &slot_orig, sizeof slot_orig);

    modhost_config cfg = {0};
    cfg.mods_dir = mods;
    cfg.cache_dir = cache;
    cfg.eboot_base = (uintptr_t)eboot;
    cfg.eboot_size = sz;
    cfg.log = logcb;
    cfg.dev_mode = 1;
    cfg.input.keyboard = kb;
    cfg.input.pad_buttons = padb;
    cfg.ui.begin_panel = begin_panel;
    cfg.ui.end_panel = end_panel;
    cfg.ui.text = ui_text;
    cfg.watch_interval_ms = 50;
    modhost *h = modhost_create(&cfg);
    CHECK(h);
    CHECK(modhost_scan(h) == 1);

    modhost_mod_view v;
    CHECK(modhost_mod_view_at(h, 0, &v) == 0);
    CHECK(v.state == MODHOST_ACTIVE);
    CHECK(!strcmp(v.id, "test.mod"));
    CHECK(v.version.major == 1 && v.version.minor == 2 && v.version.patch == 3);
    CHECK(eboot[0x10] == 0x90 && eboot[0x13] == 0x90);
    uintptr_t slot;
    memcpy(&slot, eboot + 0x100, sizeof slot);
    CHECK(slot != slot_orig);
    char out[1024];
    CHECK(modhost_vfs_resolve(h, "dvdroot_ps4/test/a.bin", out, sizeof out) == 1);
    CHECK(!strcmp(out, "/tmp/a.bin"));

    for (int i = 0; i < 5; i++) modhost_frame(h);
    modhost_mod_view_at(h, 0, &v);
    CHECK(v.resources == 5);

    CHECK(modhost_unload_now(h, "test.mod") == 0);
    CHECK(memcmp(eboot + 0x10, before, 4) == 0);
    memcpy(&slot, eboot + 0x100, sizeof slot);
    CHECK(slot == slot_orig);
    CHECK(modhost_vfs_resolve(h, "dvdroot_ps4/test/a.bin", out, sizeof out) == 0);

    CHECK(modhost_load_now(h, "test.mod") == 0);
    for (int i = 0; i < 1000; i++) {
        if (modhost_reload_now(h, "test.mod") != 0) { CHECK(0 && "reload"); break; }
    }
    modhost_mod_view_at(h, 0, &v);
    CHECK(v.state == MODHOST_ACTIVE);
    CHECK(v.resources == 5);
    CHECK(v.reload_count >= 1000);
    CHECK(modhost_unload_now(h, "test.mod") == 0);
    CHECK(memcmp(eboot + 0x10, before, 4) == 0);

    /* Fault isolation: a fault inside mod code disables only that mod. */
    CHECK(modhost_load_now(h, "test.mod") == 0);
    memcpy(&slot, eboot + 0x100, sizeof slot);      /* = the mod's detour address */
    const char *owner = modhost_fault_owner(h, slot);
    CHECK(owner && !strcmp(owner, "test.mod"));
    CHECK(modhost_on_fault(h, (uintptr_t)eboot) == 0);   /* game code: not ours */
    CHECK(modhost_on_fault(h, slot) == 1);
    uintptr_t now_slot;
    memcpy(&now_slot, eboot + 0x100, sizeof now_slot);
    CHECK(now_slot == slot_orig);                    /* hook bypassed at once */
    modhost_mod_view_at(h, 0, &v);
    CHECK(v.state == MODHOST_FAULTED);
    modhost_frame(h);                                /* queued unload runs at the sync point */
    modhost_mod_view_at(h, 0, &v);
    CHECK(v.state == MODHOST_FAULTED);               /* still flagged until reloaded */
    CHECK(memcmp(eboot + 0x10, before, 4) == 0);     /* patches reverted */
    CHECK(modhost_reload_now(h, "test.mod") == 0);
    modhost_mod_view_at(h, 0, &v);
    CHECK(v.state == MODHOST_ACTIVE);

    /* Watcher: changing the DLL on disk reloads it after the debounce. */
    uint32_t rc0 = v.reload_count;
    char dllpath[500];
    snprintf(dllpath, sizeof dllpath, "%s/test_mod/m.dll", mods);
    FILE *f = fopen(dllpath, "ab");
    CHECK(f != NULL);
    if (f) { fputc(0, f); fclose(f); }
    int reloaded = 0;
    for (int i = 0; i < 60 && !reloaded; i++) {
        tc_sleep_us(50000);
        modhost_frame(h);
        modhost_mod_view_at(h, 0, &v);
        reloaded = v.reload_count > rc0 && v.state == MODHOST_ACTIVE;
    }
    CHECK(reloaded);
    CHECK(modhost_unload_now(h, "test.mod") == 0);

    /* Input: edges are per frame; held keys do not repeat. */
    CHECK(modhost_load_now(h, "test.mod") == 0);
    memset(keys, 0, sizeof keys);
    modhost_frame(h);
    keys[62] = 1; modhost_frame(h);   /* F5 goes down: one press */
    modhost_frame(h);                 /* still held: no new press */
    keys[62] = 0; modhost_frame(h);
    keys[62] = 1; modhost_frame(h);   /* second press */
    pad = BBM_PAD_TRIANGLE; modhost_frame(h);
    pad = 0; modhost_frame(h);
    modhost_draw_panels(h);
    CHECK(strcmp(panel_text, "presses=2 pad=1") == 0);
    keys[62] = 0;

    /* Concurrency: an overlay thread draws panels and a file thread resolves
     * paths while the game thread reloads the mod hundreds of times. */
    drawer_host = h;
    stop_drawer = 0;
    tc_thread dt = tc_thread_start(drawer, NULL);
    for (int i = 0; i < 300; i++) {
        if (modhost_reload_now(h, "test.mod") != 0) { CHECK(0 && "reload under draw"); break; }
        modhost_frame(h);
        tc_sleep_us(300); /* the rest of a game frame, outside the host */
    }
    __atomic_store_n(&stop_drawer, 1, __ATOMIC_RELEASE);
    tc_thread_join(dt);
    printf("overlay thread drew %ld times during 300 reloads\n", draws);
    CHECK(draws > 0);
    modhost_mod_view_at(h, 0, &v);
    CHECK(v.state == MODHOST_ACTIVE);
    CHECK(modhost_unload_now(h, "test.mod") == 0);

    uint64_t a = modhost_modset_hash(h), b = modhost_modset_hash(h);
    CHECK(a == b);

    modhost_destroy(h);
    tc_free_rwx(eboot, sz);
    tc_rmtree(root);
    if (fails) { fprintf(stderr, "%d check(s) failed\n", fails); return 1; }
    puts("host tests passed");
    return 0;
}
