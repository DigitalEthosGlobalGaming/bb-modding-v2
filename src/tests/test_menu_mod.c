/* Real DLLs: Mod Menu (manage capability) + Difficulty (declared settings) in the real host.
 * The overlay is replaced by reading the host's published layers and sending actions, so
 * this proves the ABI, the Rust encoder against the C validator, settings persistence and
 * unload ownership. It does not prove rendering or real controller input. */
#include "internal.h"
#include "tcompat.h"
#include <assert.h>

static uint8_t keys[BBM_KEY_COUNT];
static const uint8_t *keyboard(void *u, int *count) { (void)u; *count = BBM_KEY_COUNT; return keys; }

static void frames(modhost *h, int n) { for (int i = 0; i < n; i++) modhost_frame(h); }
static void act(modhost *h, uint32_t a) { modhost_ui_input(h, a); frames(h, 3); }

/* The newest published text containing `needle`, searched in the foreground layer's list. */
static int shows(modhost *h, const char *needle) {
    modhost_ui_layer_view v[8];
    size_t n = modhost_ui_layers(h, v, 8);
    for (size_t i = 0; i < n; i++) {
        if (!v[i].foreground) continue;
        size_t at = 12;
        const uint8_t *p = v[i].cmds;
        uint32_t count = (uint32_t)p[8] | (uint32_t)p[9] << 8;
        for (uint32_t c = 0; c < count; c++) {
            uint8_t op = p[at++];
            at += 16;
            if (op == 1) at += 4;
            else if (op == 2) at += 8;
            else if (op == 3) {
                at += 8 + 2; /* size, rgba, align, bold */
                uint16_t len = (uint16_t)(p[at] | p[at + 1] << 8);
                at += 2;
                if (len >= strlen(needle)) {
                    char tmp[1100]; memcpy(tmp, p + at, len); tmp[len] = 0;
                    if (strstr(tmp, needle)) return 1;
                }
                at += len;
            } else { at += 4; at += 1 + p[at]; }
        }
    }
    return 0;
}

static void dump(modhost *h) {
    modhost_ui_layer_view v[8];
    size_t n = modhost_ui_layers(h, v, 8);
    for (size_t i = 0; i < n; i++) {
        if (!v[i].foreground) continue;
        size_t at = 12; const uint8_t *p = v[i].cmds;
        uint32_t count = (uint32_t)p[8] | (uint32_t)p[9] << 8;
        for (uint32_t c = 0; c < count; c++) {
            uint8_t op = p[at++]; at += 16;
            if (op == 1) at += 4; else if (op == 2) at += 8;
            else if (op == 3) { at += 10; uint16_t len = (uint16_t)(p[at] | p[at + 1] << 8); at += 2; fprintf(stderr, "  text: %.*s\n", len, p + at); at += len; }
            else { at += 4; at += 1 + p[at]; }
        }
    }
}

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    static char buf[8192]; size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f); return buf;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    char root[300], mods[400], cache[400], cfgfile[500];
    tc_tmpdir(root, sizeof root, "bbmod-menu-mod");
    snprintf(mods, sizeof mods, "%s/mods", root);
    snprintf(cache, sizeof cache, "%s/cache", root);
    snprintf(cfgfile, sizeof cfgfile, "%s/config/me.difficulty.settings.toml", root);
    tc_make_mod(mods, "menu", argv[1], "[mod]\nid=\"bbmod.manager\"\nname=\"Mod Menu\"\nversion=\"0.1.0\"\nabi=\"1.11\"\ndll=\"m.dll\"\nmanage=true\n");
    tc_make_mod(mods, "difficulty", argv[2], "[mod]\nid=\"me.difficulty\"\nname=\"Difficulty\"\nversion=\"0.1.0\"\nabi=\"1.1\"\ndll=\"m.dll\"\n");
    modhost_config cfg = {0};
    cfg.mods_dir = mods; cfg.cache_dir = cache;
    cfg.input.keyboard = keyboard;
    modhost *h = modhost_create(&cfg); assert(h);
    assert(modhost_scan(h) == 2);
    for (size_t i = 0; i < h->nmods; i++) assert(h->mods[i].state == MODHOST_ACTIVE);
    frames(h, 3);
    modhost_ui_layer_view v[8];
    assert(!modhost_ui_captures(h) && modhost_ui_layers(h, v, 8) <= 1);

    /* Open with F2: the layer fades in, owns input, and shows the mod list. */
    keys[59] = 1; frames(h, 2); keys[59] = 0; frames(h, 3);
    assert(modhost_ui_captures(h));
    assert(shows(h, "Mods") && shows(h, "Mod Menu") && shows(h, "Difficulty"));
    assert(shows(h, "Status: Loaded (always on)") || shows(h, "Status: Loaded"));

    /* Settings tab: the declared schemas are listed; the menu's own and Difficulty's. */
    act(h, BBM_UI_ACT_NEXT_TAB);
    assert(shows(h, "Mod Menu") && shows(h, "Difficulty"));
    /* the host lists Difficulty first: it is already selected */
    assert(shows(h, "Difficulty settings") && shows(h, "Change damage dealt") && !shows(h, "Regen delay") && shows(h, "Items"));
    act(h, BBM_UI_ACT_CONFIRM);                    /* into the controls */
    act(h, BBM_UI_ACT_CONFIRM);                    /* toggle "Change damage dealt" */
    assert(shows(h, "Damage dealt %"));            /* its slider appears (dependency met) */
    frames(h, 3);
    char *saved = slurp(cfgfile);
    assert(saved && strstr(saved, "dealt_enabled = true"));
    act(h, BBM_UI_ACT_DOWN);                       /* to the slider */
    act(h, BBM_UI_ACT_RIGHT);                      /* +5 */
    saved = slurp(cfgfile);
    assert(saved && strstr(saved, "dealt_pct = 105"));
    act(h, BBM_UI_ACT_BACK);                       /* back to the list */
    assert(modhost_ui_captures(h));

    /* Mods tab: unload Difficulty (settings stay), then load it again. */
    act(h, BBM_UI_ACT_PREV_TAB);
    act(h, BBM_UI_ACT_CONFIRM);                    /* Difficulty is the selected row: its actions open on the right */
    act(h, BBM_UI_ACT_CONFIRM);                    /* the focused Unload button */
    frames(h, 3);
    mh_entry *d = mh_find(h, "me.difficulty");
    assert(d && d->state == MODHOST_UNLOADED);
    assert(shows(h, "Not loaded"));
    act(h, BBM_UI_ACT_CONFIRM);
    frames(h, 3);
    assert(d->state == MODHOST_ACTIVE);
    saved = slurp(cfgfile);
    assert(saved && strstr(saved, "dealt_pct = 105"));  /* value survived the reload */

    /* Back leaves the actions for the list, and once more closes the menu: input is released and the layer fades out. */
    act(h, BBM_UI_ACT_BACK);
    assert(modhost_ui_captures(h));
    act(h, BBM_UI_ACT_BACK);
    assert(!modhost_ui_captures(h));
    modhost_ui_input(h, BBM_UI_ACT_CONFIRM); assert(h->nin == 0);

    /* Unloading the menu removes its layer; unloading it again later is safe. */
    for (int i = 0; i < 20; i++) {
        assert(!modhost_reload_now(h, "bbmod.manager"));
        frames(h, 2);
        keys[59] = 1; frames(h, 2); keys[59] = 0; frames(h, 2);
        assert(modhost_ui_captures(h));
        assert(!modhost_unload_now(h, "bbmod.manager"));
        assert(!modhost_ui_captures(h) && modhost_ui_layers(h, v, 8) == 0);
        assert(!modhost_load_now(h, "bbmod.manager"));
    }
    modhost_destroy(h);
    tc_rmtree(root);
    puts("Mod Menu + Difficulty (real DLLs): open, tabs, settings edit/persist, unload/load, close, 20 reloads: OK");
    return 0;
}
