/* ABI 1.2: eboot_size and the region walk. A mapped buffer must appear in the
 * walk exactly once, with the right flags, and the walk must terminate. */
#include "tcompat.h"
#include "modhost.h"
#include "internal.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(void) {
#ifdef _WIN32
    /* A guest data section allows writes but not executable protection. */
    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, NULL);
    CHECK(section != NULL);
    if (section) {
        void *view = MapViewOfFile(section, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, 4096);
        CHECK(view != NULL);
        if (view) {
            uint32_t value = 572, result = 0;
            DWORD old = 0;
            CHECK(!VirtualProtect(view, 4096, PAGE_EXECUTE_READWRITE, &old));
            CHECK(plat_write_code(view, &value, sizeof value) == 0);
            CHECK(plat_read_safe(view, &result, sizeof result) == 0 && result == value);
            MEMORY_BASIC_INFORMATION info;
            CHECK(VirtualQuery(view, &info, sizeof info) == sizeof info);
            CHECK(info.Protect == PAGE_READWRITE);
            UnmapViewOfFile(view);
        }
        CloseHandle(section);
    }
#endif
    size_t sz = 0x20000;
    uint8_t *img = tc_alloc_rwx(sz);
    modhost_config cfg = {0};
    cfg.mods_dir = "m"; cfg.cache_dir = "c";
    cfg.eboot_base = (uintptr_t)img; cfg.eboot_size = sz;
    modhost *h = modhost_create(&cfg);

    /* Walk the whole address space; our buffer must show up once, readable. */
    uintptr_t addr = 0;
    int seen = 0, steps = 0;
    uintptr_t last_end = 0;
    for (;;) {
        uintptr_t base; size_t rs; uint32_t fl;
        if (plat_next_region(addr, &base, &rs, &fl) != 0) break;
        CHECK(base + rs > addr);           /* ends above the query */
        CHECK(base + rs > last_end || base >= last_end); /* monotonic progress */
        if ((uintptr_t)img >= base && (uintptr_t)img < base + rs) {
            seen++;
            CHECK(fl & BBM_REGION_WRITE);  /* rwx mapping */
            CHECK(fl & BBM_REGION_EXEC);
        }
        last_end = base + rs;
        addr = base + rs;
        if (++steps > 100000) { CHECK(0 && "walk did not terminate"); break; }
    }
    CHECK(seen == 1);
    CHECK(steps > 1);

    modhost_destroy(h);
    tc_free_rwx(img, sz);
    if (fails) return 1;
    puts("regions test passed");
    return 0;
}
