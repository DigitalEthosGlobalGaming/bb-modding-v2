/* Windows only: the real thread-freezing path. A thread spins inside a known
 * code range; the quiesce check must see it there, see nothing in another
 * range, and resume must really restart every thread. */
#include "tcompat.h"
#include "internal.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); fails++; } } while (0)

static volatile long ticks;
static volatile int stop_ticker;
static void *ticker(void *u) { (void)u; while (!stop_ticker) { ticks++; Sleep(1); } return NULL; }

static void (*spin)(void);
static void *spinner(void *u) { (void)u; spin(); return NULL; }

int main(void) {
    uint8_t *buf = tc_alloc_rwx(4096);
    buf[0] = 0xEB; buf[1] = 0xFE; /* jmp $ */
    spin = (void (*)(void))(void *)buf;

    modhost_config cfg = {0};
    cfg.mods_dir = "x"; cfg.cache_dir = "y";
    modhost *h = modhost_create(&cfg);

    tc_thread tk = tc_thread_start(ticker, NULL);
    tc_thread sp = tc_thread_start(spinner, NULL);
    Sleep(100);

    CHECK(mh_quiesce_range(h, (uintptr_t)buf, (uintptr_t)buf + 2) == -1);          /* seen inside */
    CHECK(mh_quiesce_range(h, (uintptr_t)buf + 64, (uintptr_t)buf + 128) == 0);    /* clear range */

    long t0 = ticks;
    /* Sleep(1) need not wake every millisecond on Windows. Wait for the
     * same progress condition with a bounded deadline instead. */
    ULONGLONG deadline = GetTickCount64() + 5000;
    while (ticks <= t0 + 5 && GetTickCount64() < deadline) Sleep(10);
    CHECK(ticks > t0 + 5);                      /* resume really restarted the ticker */

    /* Let the spinner out: nop; ret overwrites jmp $ (aligned 2-byte store). */
    CHECK(plat_suspend_world() == 0);
    CHECK(plat_any_thread_in((uintptr_t)buf, (uintptr_t)buf + 2) == 1);
    uint8_t out[2] = {0x90, 0xC3};
    CHECK(plat_write_code(buf, out, 2) == 0);
    plat_resume_world();
    tc_thread_join(sp);
    CHECK(mh_quiesce_range(h, (uintptr_t)buf, (uintptr_t)buf + 2) == 0);           /* gone now */

    stop_ticker = 1;
    tc_thread_join(tk);
    modhost_destroy(h);
    tc_free_rwx(buf, 4096);
    if (fails) return 1;
    puts("windows suspend test passed");
    return 0;
}
