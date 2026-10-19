/* Loads a Rust mod (argv[1]) into the host: frames, panel text, hot reload
 * with state hand-off, clean unload. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "modhost.h"

static char last_text[256];
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); fails++; } } while (0)

static int begin_panel(void *u, const char *t, const char *id) { (void)u; (void)t; (void)id; return 1; }
static void end_panel(void *u) { (void)u; }
static void text(void *u, const char *s) { (void)u; snprintf(last_text, sizeof last_text, "%s", s); }
static void logcb(void *u, const char *id, int lvl, const char *m) {
    (void)u; if (lvl >= BBM_LOG_INFO) printf("[%s] %s\n", id ? id : "host", m);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: test_rust <mod.so> <mod.toml>\n"); return 2; }
    char root[] = "/tmp/bbmod-rust-XXXXXX";
    if (!mkdtemp(root)) return 2;
    char cmd[2048];
    snprintf(cmd, sizeof cmd, "mkdir -p %s/mods/hello && cp %s %s/mods/hello/hello_rust.dll && cp %s %s/mods/hello/mod.toml",
             root, argv[1], root, argv[2], root);
    if (system(cmd)) return 2;

    size_t pg = (size_t)sysconf(_SC_PAGESIZE);
    void *eboot = mmap(NULL, pg, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    char mods[512], cache[512];
    snprintf(mods, sizeof mods, "%s/mods", root);
    snprintf(cache, sizeof cache, "%s/cache", root);
    modhost_config cfg = {0};
    cfg.mods_dir = mods; cfg.cache_dir = cache;
    cfg.eboot_base = (uintptr_t)eboot; cfg.eboot_size = pg;
    cfg.log = logcb;
    cfg.ui.begin_panel = begin_panel; cfg.ui.end_panel = end_panel; cfg.ui.text = text;
    modhost *h = modhost_create(&cfg);
    CHECK(modhost_scan(h) == 1);

    modhost_mod_view v;
    modhost_mod_view_at(h, 0, &v);
    CHECK(v.state == MODHOST_ACTIVE);

    for (int i = 0; i < 7; i++) { modhost_frame(h); modhost_draw_panels(h); }
    CHECK(strcmp(last_text, "frames seen: 7") == 0);

    for (int r = 0; r < 25; r++) CHECK(modhost_reload_now(h, "example.hello-rust") == 0);
    modhost_frame(h);
    modhost_draw_panels(h);
    CHECK(strcmp(last_text, "frames seen: 8") == 0); /* state survived 25 reloads */
    modhost_mod_view_at(h, 0, &v);
    CHECK(v.state == MODHOST_ACTIVE && v.reload_count >= 25);

    CHECK(modhost_unload_now(h, "example.hello-rust") == 0);
    modhost_destroy(h);
    snprintf(cmd, sizeof cmd, "rm -rf %s", root);
    if (system(cmd)) {}
    if (fails) return 1;
    puts("rust mod test passed");
    return 0;
}
