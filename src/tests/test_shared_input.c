/* Real Rust subscribers: fan-out, edge semantics, unload isolation, state reload. */
#include "tcompat.h"
#include "modhost.h"
static int fails, monitor_events, counter_events;
static uint32_t buttons;
static char counter_line[256];
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); fails++; } } while (0)
static uint32_t input(void *u) { (void)u; return buttons; }
static void log_event(void *u, const char *id, int level, const char *msg) {
    (void)u; (void)level;
    if (!id || strncmp(msg, "input:", 6)) return;
    if (!strcmp(id, "example.input-monitor")) monitor_events++;
    if (!strcmp(id, "example.input-counter")) {
        counter_events++;
        snprintf(counter_line, sizeof counter_line, "%s", msg);
    }
}
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    char root[300], mods[400], cache[400];
    tc_tmpdir(root, sizeof root, "bbmod-shared-input");
    snprintf(mods, sizeof mods, "%s/mods", root);
    snprintf(cache, sizeof cache, "%s/cache", root);
    tc_make_mod(mods, "monitor", argv[1], "[mod]\nid = \"example.input-monitor\"\nabi = \"1.3\"\ndll = \"m.dll\"\n");
    tc_make_mod(mods, "counter", argv[2], "[mod]\nid = \"example.input-counter\"\nabi = \"1.3\"\ndll = \"m.dll\"\n");
    modhost_config cfg = {0};
    cfg.mods_dir = mods; cfg.cache_dir = cache;
    cfg.input.pad_buttons = input; cfg.log = log_event;
    modhost *h = modhost_create(&cfg);
    CHECK(h != NULL);
    CHECK(modhost_scan(h) == 2);
    for (size_t i = 0; i < 2; i++) {
        modhost_mod_view v; modhost_mod_view_at(h, i, &v);
        CHECK(v.state == MODHOST_ACTIVE);
    }
    modhost_frame(h);
    CHECK(monitor_events == 0 && counter_events == 0);
    buttons = BBM_PAD_CROSS; modhost_frame(h);
    CHECK(monitor_events == 1 && counter_events == 1);
    CHECK(strstr(counter_line, "presses=1") != NULL);
    for (int i = 0; i < 20; i++) modhost_frame(h);
    CHECK(monitor_events == 1 && counter_events == 1); /* Held: no duplicate edges. */
    buttons = 0; modhost_frame(h);
    CHECK(monitor_events == 2 && counter_events == 2);
    CHECK(strstr(counter_line, "presses=1") != NULL); /* Release isn't a press. */
    modhost_request_unload(h, "example.input-monitor");
    buttons = BBM_PAD_CROSS | BBM_PAD_CIRCLE; modhost_frame(h);
    CHECK(monitor_events == 2 && counter_events == 3);
    CHECK(strstr(counter_line, "presses=3") != NULL);
    CHECK(modhost_reload_now(h, "example.input-counter") == 0);
    buttons = 0; modhost_frame(h);
    CHECK(counter_events == 4 && strstr(counter_line, "presses=3") != NULL);
    CHECK(modhost_load_now(h, "example.input-monitor") == 0);
    buttons = BBM_PAD_CROSS; modhost_frame(h);
    CHECK(monitor_events == 3 && counter_events == 5);
    CHECK(strstr(counter_line, "presses=4") != NULL);
    /* Repeated reloads must not accumulate duplicate subscribers. */
    for (int i = 0; i < 20; i++) {
        CHECK(modhost_reload_now(h, "example.input-monitor") == 0);
        buttons ^= BBM_PAD_CIRCLE; modhost_frame(h);
    }
    CHECK(monitor_events == 23 && counter_events == 25);
    modhost_destroy(h);
    tc_rmtree(root);
    return fails ? 1 : 0;
}
