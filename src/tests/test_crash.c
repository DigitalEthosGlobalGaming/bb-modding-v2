/* Real crash recovery (POSIX): call_guarded is sigsetjmp + a SIGSEGV handler,
 * the same idea as bbport's runtime_fault_recover/bb_longjmp. A crashing mod
 * must be disabled and reverted while a healthy mod and the "game" keep going. */
#include "tcompat.h"
#include <setjmp.h>
#include <signal.h>
#include <time.h>
#include "modhost.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); fails++; } } while (0)

static __thread sigjmp_buf *recover;
static void on_segv(int sig) {
    if (recover) { sigjmp_buf *r = recover; recover = NULL; siglongjmp(*r, 1); }
    signal(sig, SIG_DFL);
    raise(sig);
}
static int call_guarded(void *u, void (*fn)(void *), void *arg) {
    (void)u;
    sigjmp_buf buf, *prev = recover;
    if (sigsetjmp(buf, 1)) { recover = prev; return 1; }
    recover = &buf;
    fn(arg);
    recover = prev;
    return 0;
}

static char last_text[128];
static int begin_panel(void *u, const char *t, const char *id) { (void)u; (void)t; (void)id; return 1; }
static void end_panel(void *u) { (void)u; }
static void text(void *u, const char *s) { (void)u; snprintf(last_text, sizeof last_text, "%s", s); }
static int notes;
static void notify(void *u, const char *id, const char *m) { (void)u; (void)id; (void)m; notes++; }

static uint8_t *eboot;
static char mods[400], cache[400];

static modhost *make(const char *crash_mode, const char *healthy_so, const char *crash_so) {
    char toml[300];
    snprintf(toml, sizeof toml, "[mod]\nid = \"crash\"\ndll = \"m.dll\"\n[test]\ncrash = \"%s\"\n", crash_mode);
    tc_make_mod(mods, "crash", crash_so, toml);
    tc_make_mod(mods, "healthy", healthy_so, "[mod]\nid = \"healthy\"\ndll = \"m.dll\"\n");
    modhost_config cfg = {0};
    cfg.mods_dir = mods; cfg.cache_dir = cache;
    cfg.eboot_base = (uintptr_t)eboot; cfg.eboot_size = 4096 * 4;
    cfg.platform.call_guarded = call_guarded;
    cfg.ui.begin_panel = begin_panel; cfg.ui.end_panel = end_panel; cfg.ui.text = text; cfg.ui.notify = notify;
    modhost *h = modhost_create(&cfg);
    modhost_scan(h);
    return h;
}
static modhost_state state_of(modhost *h, const char *id) {
    for (size_t i = 0; i < modhost_mod_count(h); i++) {
        modhost_mod_view v; modhost_mod_view_at(h, i, &v);
        if (!strcmp(v.id, id)) return v.state;
    }
    return (modhost_state)-1;
}

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    signal(SIGSEGV, on_segv);
    char root[300];
    tc_tmpdir(root, sizeof root, "bbmod-crash");
    snprintf(mods, sizeof mods, "%s/mods", root);
    snprintf(cache, sizeof cache, "%s/cache", root);
    eboot = tc_alloc_rwx(4096 * 4);
    eboot[0x20] = 0xAB;

    /* 1. Crash in a frame callback. */
    modhost *h = make("frame", argv[1], argv[2]);
    CHECK(state_of(h, "crash") == MODHOST_ACTIVE && eboot[0x20] == 0x90);
    for (int i = 0; i < 6; i++) modhost_frame(h);
    CHECK(state_of(h, "crash") == MODHOST_FAULTED);
    CHECK(eboot[0x20] == 0xAB);                       /* its patch reverted */
    CHECK(state_of(h, "healthy") == MODHOST_ACTIVE);  /* the other mod untouched */
    CHECK(notes >= 1);                                /* the player was told */
    modhost_destroy(h);
    tc_rmtree(root);

    /* 2. Crash while its panel draws. */
    h = make("panel", argv[1], argv[2]);
    modhost_draw_panels(h);
    modhost_frame(h);
    CHECK(state_of(h, "crash") == MODHOST_FAULTED && eboot[0x20] == 0xAB);
    CHECK(state_of(h, "healthy") == MODHOST_ACTIVE);
    modhost_destroy(h);
    tc_rmtree(root);

    /* 3. Crash in on_load, then the author fixes it and reloads. */
    h = make("load", argv[1], argv[2]);
    CHECK(state_of(h, "crash") == MODHOST_FAULTED && eboot[0x20] == 0xAB);
    char p[500];
    snprintf(p, sizeof p, "%s/crash/mod.toml", mods);
    tc_write(p, "[mod]\nid = \"crash\"\ndll = \"m.dll\"\n[test]\ncrash = \"none\"\n");
    CHECK(modhost_reload_now(h, "crash") == 0);
    CHECK(state_of(h, "crash") == MODHOST_ACTIVE && eboot[0x20] == 0x90);
    modhost_destroy(h);
    tc_rmtree(root);

    /* 4. A thread that ignores its stop flag must not hang the unload. */
    h = make("thread", argv[1], argv[2]);
    time_t t0 = time(NULL);
    CHECK(modhost_unload_now(h, "crash") == 0);
    CHECK(time(NULL) - t0 <= 4);
    CHECK(state_of(h, "crash") == MODHOST_UNLOADED && eboot[0x20] == 0xAB);
    modhost_destroy(h);

    tc_rmtree(root);
    if (fails) return 1;
    puts("crash recovery test passed");
    return 0;
}
