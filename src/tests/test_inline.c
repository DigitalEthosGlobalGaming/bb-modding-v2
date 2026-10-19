/* Inline hooks under a live caller thread. Requires Zydis.
 *   test_inline <mod> [coop|os]
 * coop: the test parks its worker between calls (portable stand-in).
 * os:   default platform layer, i.e. real thread suspension and RIP checks
 *       (SuspendThread + GetThreadContext on Windows). No-op on POSIX. */
#include "tcompat.h"
#include "modhost.h"
#include <inttypes.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); fails++; } } while (0)

#ifdef _WIN32
#define SYSV __attribute__((sysv_abi))
#else
#define SYSV __attribute__((sysv_abi))
#endif
typedef SYSV int (*fn_t)(int);

static volatile int pause_req, parked, stop_worker, worker_live;
/* Windows long is 32-bit; a fast caller can exceed it during the soak. */
static volatile uint64_t bad, calls, saw_hooked, saw_plain;
static fn_t target;
static int coop;

static void *worker(void *u) {
    (void)u;
    while (!stop_worker) {
        if (coop && __atomic_load_n(&pause_req, __ATOMIC_ACQUIRE)) {
            __atomic_store_n(&parked, 1, __ATOMIC_RELEASE);
            while (__atomic_load_n(&pause_req, __ATOMIC_ACQUIRE)) tc_yield();
            __atomic_store_n(&parked, 0, __ATOMIC_RELEASE);
            continue;
        }
        int r = target(7);
        calls++;
        if (r == 12) saw_plain++;
        else if (r == 24) saw_hooked++;
        else bad++;
    }
    return NULL;
}
static int suspend_world(void *u) {
    (void)u;
    if (!worker_live) return 0;
    __atomic_store_n(&pause_req, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&parked, __ATOMIC_ACQUIRE)) tc_yield();
    return 0;
}
static void resume_world(void *u) {
    (void)u;
    if (!worker_live) return;
    __atomic_store_n(&pause_req, 0, __ATOMIC_RELEASE);
    while (__atomic_load_n(&parked, __ATOMIC_ACQUIRE)) tc_yield();
}
static int any_in(void *u, uintptr_t lo, uintptr_t hi) { (void)u; (void)lo; (void)hi; return 0; }
static void logcb(void *u, const char *id, int lvl, const char *m) {
    (void)u; if (lvl >= BBM_LOG_WARN) fprintf(stderr, "[%s] %s\n", id ? id : "host", m);
}

#ifdef _WIN32
static DWORD WINAPI watchdog(LPVOID p) { (void)p; Sleep(120000); fprintf(stderr, "watchdog: hung\n"); ExitProcess(99); return 0; }
#endif

static void mk(const char *mods, const char *name, const char *so, const char *target_off) {
    char toml[400];
    snprintf(toml, sizeof toml, "[mod]\nid = \"%s\"\ndll = \"m.dll\"\n[test]\ntarget = \"%s\"\n", name, target_off);
    tc_make_mod(mods, name, so, toml);
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
#ifdef _WIN32
    coop = argc > 2 && !strcmp(argv[2], "coop");
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
#else
    coop = !(argc > 2 && !strcmp(argv[2], "os"));
#endif
    printf("suspension mode: %s\n", coop ? "cooperative stand-in" : "platform layer (real)");

    char root[300], mods[400], cache[400];
    tc_tmpdir(root, sizeof root, "bbmod-inline");
    snprintf(mods, sizeof mods, "%s/mods", root);
    snprintf(cache, sizeof cache, "%s/cache", root);
    mk(mods, "good", argv[1], "0x200");
    mk(mods, "tiny", argv[1], "0x300");
    mk(mods, "shortjmp", argv[1], "0x400");

    size_t sz = 4096 * 4;
    uint8_t *e = tc_alloc_rwx(sz);
    CHECK(e != NULL);
    /* 0x200: push rbp; mov rbp,rsp; mov eax,edi; add eax,5; 12 x nop; pop rbp; ret  -> x+5 */
    static const uint8_t good[] = {0x55, 0x48, 0x89, 0xE5, 0x89, 0xF8, 0x83, 0xC0, 0x05,
        0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90, 0x5D, 0xC3};
    memcpy(e + 0x200, good, sizeof good);
    static const uint8_t tiny[] = {0x89, 0xF8, 0xC3};
    memcpy(e + 0x300, tiny, sizeof tiny);
    static const uint8_t sj[] = {0x85, 0xFF, 0x74, 0x02, 0x31, 0xC0, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0xC3};
    memcpy(e + 0x400, sj, sizeof sj);
    uint8_t before[sizeof good];
    memcpy(before, e + 0x200, sizeof good);
    target = (fn_t)(void *)(e + 0x200);
    CHECK(target(7) == 12);

    modhost_config cfg = {0};
    cfg.mods_dir = mods; cfg.cache_dir = cache;
    cfg.eboot_base = (uintptr_t)e; cfg.eboot_size = sz;
    cfg.log = logcb;
    /* Busy CI runners can deschedule the caller beyond the production 500 ms
     * drain default. Keep the same restoration checks with a bounded 5 s wait. */
    cfg.drain_timeout_ms = 5000;
    if (coop) {
        cfg.platform.suspend_world = suspend_world;
        cfg.platform.resume_world = resume_world;
        cfg.platform.any_thread_in = any_in;
    }
    modhost *h = modhost_create(&cfg);
    CHECK(modhost_scan(h) == 3);

    for (size_t i = 0; i < modhost_mod_count(h); i++) {
        modhost_mod_view v; modhost_mod_view_at(h, i, &v);
        if (!strcmp(v.id, "tiny") || !strcmp(v.id, "shortjmp")) {
            CHECK(v.state != MODHOST_ACTIVE);
            CHECK(v.last_error && v.last_error[0]);
        }
    }
    CHECK(memcmp(e + 0x300, tiny, sizeof tiny) == 0);
    CHECK(memcmp(e + 0x400, sj, sizeof sj) == 0);

    modhost_mod_view v;
    int gi = -1;
    for (size_t i = 0; i < modhost_mod_count(h); i++) { modhost_mod_view_at(h, i, &v); if (!strcmp(v.id, "good")) gi = (int)i; }
    CHECK(gi >= 0);
    modhost_mod_view_at(h, (size_t)gi, &v);
    CHECK(v.state == MODHOST_ACTIVE);
    CHECK(target(7) == 24);
    CHECK(memcmp(e + 0x200, before, sizeof before) != 0);
    CHECK(modhost_unload_now(h, "good") == 0);
    CHECK(memcmp(e + 0x200, before, sizeof before) == 0);
    CHECK(target(7) == 12);

    /* Soak with a live caller: hook/unhook while another thread calls it. */
    worker_live = 1;
    tc_thread t = tc_thread_start(worker, NULL);
    for (int i = 0; i < 300; i++) {
        if (modhost_load_now(h, "good") != 0) { CHECK(0 && "load"); break; }
        for (int f = 0; f < 3; f++) { modhost_frame(h); tc_sleep_us(200); }
        if (modhost_unload_now(h, "good") != 0) { CHECK(0 && "unload"); break; }
    }
    stop_worker = 1;
    tc_thread_join(t);
    worker_live = 0;
    printf("worker: %" PRIu64 " calls, %" PRIu64 " plain, %" PRIu64 " hooked, %" PRIu64 " bad\n",
           calls, saw_plain, saw_hooked, bad);
    CHECK(bad == 0);
    CHECK(calls > 1000);
    CHECK(saw_hooked > 0 && saw_plain > 0);
    CHECK(memcmp(e + 0x200, before, sizeof before) == 0);

    modhost_destroy(h);
    tc_free_rwx(e, sz);
    tc_rmtree(root);
    if (fails) return 1;
    puts("inline hook test passed");
    return 0;
}
