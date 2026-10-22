/* ABI 1.11: UI layers, mod settings and the manage capability, against the real host
 * with synthetic mod instances. Proves host logic only, not rendering. */
#include "internal.h"
#include "tcompat.h"
#include <assert.h>
#include <math.h>

_Static_assert(sizeof(bbm_ui_layer) == 32, "layer ABI");
_Static_assert(sizeof(bbm_ui_action) == 16, "action ABI");
_Static_assert(sizeof(bbm_setting_def) == 800, "setting def ABI");
_Static_assert(sizeof(bbm_setting_value) == 120, "setting value ABI");
_Static_assert(sizeof(bbm_setting_event) == 176, "setting event ABI");
_Static_assert(sizeof(bbm_mod_info) == 1144, "mod info ABI");

typedef struct sink { unsigned actions[16]; unsigned n; uint64_t layer; bbm_setting_event setting; unsigned settings; } sink;
static void on_event(bbm_ctx *ctx, const bbm_event *e) {
    sink *s = (void *)ctx;
    if (e->type == BBM_EV_UI_ACTION) {
        const bbm_ui_action *a = e->data;
        assert(e->data_len == sizeof *a && a->size == sizeof *a);
        s->layer = a->layer;
        if (s->n < 16) s->actions[s->n++] = a->action;
    } else if (e->type == BBM_EV_SETTING_CHANGED) {
        assert(e->data_len == sizeof(bbm_setting_event));
        s->setting = *(const bbm_setting_event *)e->data;
        s->settings++;
    }
}

static void put_f32(uint8_t *p, float v) { memcpy(p, &v, 4); }
static size_t stream(uint8_t *b, int fills, const char *text) {
    memcpy(b, "BBUI", 4); b[4] = 1; b[5] = b[6] = b[7] = 0;
    uint32_t count = (uint32_t)fills + (text ? 1u : 0u);
    memcpy(b + 8, &count, 4);
    size_t at = 12;
    for (int i = 0; i < fills; i++) {
        b[at++] = 1;
        for (int k = 0; k < 4; k++) { put_f32(b + at, (float)(k + 1)); at += 4; }
        uint32_t rgba = 0xa5a58cff; memcpy(b + at, &rgba, 4); at += 4;
    }
    if (text) {
        b[at++] = 3;
        for (int k = 0; k < 4; k++) { put_f32(b + at, 10.0f); at += 4; }
        put_f32(b + at, 22.0f); at += 4;
        uint32_t rgba = 0xffffffff; memcpy(b + at, &rgba, 4); at += 4;
        uint16_t len = (uint16_t)strlen(text);
        b[at++] = 1; b[at++] = 0; b[at++] = (uint8_t)len; b[at++] = (uint8_t)(len >> 8);
        memcpy(b + at, text, len); at += len;
    }
    return at;
}
static bbm_setting_def def(const char *key, uint32_t type) {
    bbm_setting_def d = {.size = sizeof d, .type = type};
    strcpy(d.key, key); strcpy(d.label, "Label"); d.min = 0; d.max = 10; d.step = 1; d.def_i = 3;
    return d;
}

int main(void) {
    char root[300], mods[400], cache[400];
    tc_tmpdir(root, sizeof root, "bbmod-ui-layers");
    snprintf(mods, sizeof mods, "%s/mods", root);
    snprintf(cache, sizeof cache, "%s/cache", root);
    modhost_config cfg = {0}; cfg.mods_dir = mods; cfg.cache_dir = cache;
    modhost *h = modhost_create(&cfg); assert(h);
    h->mods = calloc(3, sizeof *h->mods); h->nmods = h->capmods = 3;
    strcpy(h->mods[0].m.id, "ex.a"); strcpy(h->mods[1].m.id, "ex.b"); strcpy(h->mods[2].m.id, "ex.menu");
    h->mods[2].m.manage = 1;
    static sink sa, sb, sm;
    struct bbm_mod a = {.host = h, .entry = &h->mods[0], .ctx = (void *)&sa};
    struct bbm_mod b = {.host = h, .entry = &h->mods[1], .ctx = (void *)&sb};
    struct bbm_mod menu = {.host = h, .entry = &h->mods[2], .ctx = (void *)&sm};
    a.api.on_event = b.api.on_event = menu.api.on_event = on_event;
    a.events = b.events = menu.events = BBM_EV_UI_ACTION | BBM_EV_SETTING_CHANGED;
    for (int i = 0; i < 3; i++) { h->mods[i].state = MODHOST_ACTIVE; }
    h->mods[0].inst = &a; h->mods[1].inst = &b; h->mods[2].inst = &menu;

    /* ---- layers ---- */
    uint8_t buf[4096];
    size_t n = stream(buf, 3, "Hello");
    bbm_ui_layer hud = {sizeof hud, BBM_UI_HUD, BBM_UI_VISIBLE, 0};
    bbm_ui_layer screen = {sizeof screen, BBM_UI_SCREEN, BBM_UI_VISIBLE, 0};
    bbm_ui_layer modal = {sizeof modal, BBM_UI_MODAL, BBM_UI_VISIBLE, 0, 600, 400, 720, 200};
    bbm_id hid = 0, sid = 0, mid = 0, tmp = 0;
    assert(!h->api.ui_layer_set(&a, &hid, &hud, buf, n) && hid);
    assert(!modhost_ui_captures(h)); /* a HUD never captures */
    modhost_ui_input(h, BBM_UI_ACT_CONFIRM); assert(h->nin == 0);
    /* malformed streams are rejected whole */
    uint8_t bad[4096]; memcpy(bad, buf, n);
    bad[0] = 'X'; assert(h->api.ui_layer_set(&a, &tmp, &screen, bad, n) == BBM_ERR_ARG && !tmp);
    memcpy(bad, buf, n); assert(h->api.ui_layer_set(&a, &tmp, &screen, bad, n - 1) == BBM_ERR_ARG);
    memcpy(bad, buf, n); put_f32(bad + 13, NAN); assert(h->api.ui_layer_set(&a, &tmp, &screen, bad, n) == BBM_ERR_ARG);
    memcpy(bad, buf, n); bad[12] = 9; assert(h->api.ui_layer_set(&a, &tmp, &screen, bad, n) == BBM_ERR_ARG);
    bbm_ui_layer wrong = screen; wrong.size = 8; assert(h->api.ui_layer_set(&a, &tmp, &wrong, buf, n) == BBM_ERR_ARG);
    bbm_ui_layer empty_modal = modal; empty_modal.w = 0; assert(h->api.ui_layer_set(&a, &tmp, &empty_modal, buf, n) == BBM_ERR_ARG);
    assert(!h->api.ui_layer_set(&a, &sid, &screen, buf, n) && sid);
    assert(modhost_ui_captures(h));
    /* only the owner of the foreground layer gets actions */
    modhost_ui_input(h, BBM_UI_ACT_DOWN); modhost_ui_input(h, 99); /* invalid dropped */
    modhost_frame(h);
    assert(sa.n == 1 && sa.actions[0] == BBM_UI_ACT_DOWN && sa.layer == sid && sb.n == 0);
    /* a modal from another mod takes the foreground */
    assert(!h->api.ui_layer_set(&b, &mid, &modal, buf, n));
    modhost_ui_input(h, BBM_UI_ACT_CONFIRM); modhost_frame(h);
    assert(sb.n == 1 && sb.actions[0] == BBM_UI_ACT_CONFIRM && sa.n == 1);
    /* views: back to front, the modal is the foreground, fades take 12 ticks (200 ms) */
    modhost_ui_layer_view v[8];
    uint64_t t0 = 1000000;
    size_t count = modhost_ui_layers_at(h, t0 + 100000, v, 8);
    assert(count == 3 && v[0].kind == BBM_UI_HUD && v[1].kind == BBM_UI_SCREEN && v[2].kind == BBM_UI_MODAL && v[2].foreground && !v[1].foreground);
    assert(v[2].n == n && !strcmp(v[2].owner, "ex.b"));
    /* hide the modal: input returns to the screen immediately, then it fades out and disappears */
    modal.flags = 0; assert(!h->api.ui_layer_set(&b, &mid, &modal, buf, n));
    assert(modhost_ui_captures(h));
    modhost_ui_input(h, BBM_UI_ACT_BACK); modhost_frame(h);
    assert(sa.n == 2 && sa.actions[1] == BBM_UI_ACT_BACK && sb.n == 1);
    count = modhost_ui_layers_at(h, plat_now_ms() + 2000, v, 8);
    assert(count == 2); /* the faded-out modal is omitted */
    /* wrong owner / kind change */
    assert(h->api.ui_layer_set(&a, &mid, &modal, buf, n) == BBM_ERR_NOTFOUND);
    hud.kind = BBM_UI_SCREEN; assert(h->api.ui_layer_set(&a, &hid, &hud, buf, n) == BBM_ERR_ARG);
    /* per-mod limit and reload cleanup */
    bbm_id many[9] = {0}; int ok = 0;
    for (int i = 0; i < 9; i++) { bbm_ui_layer l = {sizeof l, BBM_UI_HUD, BBM_UI_VISIBLE, i}; if (!h->api.ui_layer_set(&b, &many[i], &l, buf, n)) ok++; }
    assert(ok == 7); /* 8 per mod, one already used */
    mh_res_release_all(&a);
    assert(!modhost_ui_captures(h) || h->ui_capture == 0);
    modhost_frame(h); assert(!modhost_ui_captures(h));
    modhost_ui_input(h, BBM_UI_ACT_UP); assert(h->nin == 0);
    mh_res_release_all(&b);
    assert(modhost_ui_layers_at(h, plat_now_ms(), v, 8) == 0);

    /* ---- settings ---- */
    bbm_setting_def d = def("level", BBM_SET_INT);
    assert(!h->api.setting_declare(&a, &d));
    assert(h->api.setting_declare(&a, &d) == BBM_ERR_CONFLICT);
    bbm_setting_def bad_def = def("bad key", BBM_SET_INT); assert(h->api.setting_declare(&a, &bad_def) == BBM_ERR_ARG);
    bad_def = def("e", BBM_SET_ENUM); strcpy(bad_def.choices, "OnlyOne"); assert(h->api.setting_declare(&a, &bad_def) == BBM_ERR_ARG);
    bad_def = def("r", BBM_SET_INT); bad_def.min = 5; bad_def.max = 1; assert(h->api.setting_declare(&a, &bad_def) == BBM_ERR_ARG);
    bbm_setting_def e = def("style", BBM_SET_ENUM); strcpy(e.choices, "A|B|C"); e.def_i = 1; e.flags = BBM_SET_RESTART;
    bbm_setting_def f = def("scale", BBM_SET_FLOAT); f.min = 0.5; f.max = 2.0; f.def_f = 1.0;
    bbm_setting_def t = def("title", BBM_SET_TEXT); t.max = 8; strcpy(t.def_s, "Hi");
    bbm_setting_def bl = def("on", BBM_SET_BOOL); bl.def_i = 1;
    assert(!h->api.setting_declare(&a, &e) && !h->api.setting_declare(&a, &f) && !h->api.setting_declare(&a, &t) && !h->api.setting_declare(&a, &bl));
    bbm_setting_value v1 = {sizeof v1};
    assert(!h->api.setting_get(&a, "level", &v1) && v1.i == 3 && v1.type == BBM_SET_INT);
    assert(h->api.setting_get(&a, "missing", &v1) == BBM_ERR_NOTFOUND && h->api.setting_get(&b, "level", &v1) == BBM_ERR_NOTFOUND);
    v1 = (bbm_setting_value){sizeof v1, BBM_SET_INT}; v1.i = 99;
    assert(!h->api.setting_set(&a, "level", &v1)); /* clamped */
    v1.size = sizeof v1; assert(!h->api.setting_get(&a, "level", &v1) && v1.i == 10);
    v1 = (bbm_setting_value){sizeof v1, BBM_SET_FLOAT}; v1.f = NAN; assert(h->api.setting_set(&a, "scale", &v1) == BBM_ERR_ARG);
    v1 = (bbm_setting_value){sizeof v1, BBM_SET_ENUM}; v1.i = 3; assert(h->api.setting_set(&a, "style", &v1) == BBM_ERR_ARG);
    v1 = (bbm_setting_value){sizeof v1, BBM_SET_TEXT}; strcpy(v1.s, "far too long"); assert(h->api.setting_set(&a, "title", &v1) == BBM_ERR_ARG);
    v1 = (bbm_setting_value){sizeof v1, BBM_SET_INT}; assert(h->api.setting_set(&a, "style", &v1) == BBM_ERR_ARG); /* wrong type */
    /* manage capability */
    bbm_mod_info mi = {sizeof mi};
    assert(h->api.mods_info(&a, 0, &mi) == BBM_ERR_STATE && h->api.mod_request(&a, "ex.b", BBM_MODOP_UNLOAD) == BBM_ERR_STATE);
    assert(!h->api.mods_info(&menu, 0, &mi) && !strcmp(mi.id, "ex.a") && (mi.flags & BBM_MODF_SETTINGS) && mi.state == MODHOST_ACTIVE);
    assert(!h->api.mods_info(&menu, 2, &mi) && (mi.flags & BBM_MODF_SELF));
    assert(h->api.mods_info(&menu, 3, &mi) == BBM_ERR_NOTFOUND);
    assert(h->api.mod_request(&menu, "ex.menu", BBM_MODOP_UNLOAD) == BBM_ERR_ARG && h->api.mod_request(&menu, "nope", BBM_MODOP_RELOAD) == BBM_ERR_NOTFOUND);
    assert(!h->api.mod_request(&menu, "ex.b", BBM_MODOP_RELOAD));
    bbm_setting_def got = {sizeof got}; bbm_setting_value val = {sizeof val};
    assert(!h->api.settings_info(&menu, "ex.a", 0, &got, &val) && !strcmp(got.key, "level") && val.i == 10);
    assert(!h->api.settings_info(&menu, "ex.a", 4, &got, &val) && !strcmp(got.key, "on"));
    assert(h->api.settings_info(&menu, "ex.a", 5, &got, &val) == BBM_ERR_NOTFOUND && h->api.settings_info(&a, "ex.a", 0, &got, &val) == BBM_ERR_STATE);
    /* a manager write validates, persists, and tells the owner at the sync point */
    bbm_setting_value w = {sizeof w, BBM_SET_FLOAT}; w.f = 5.0;
    assert(!h->api.settings_write(&menu, "ex.a", "scale", &w));
    assert(sa.settings == 0); modhost_frame(h);
    assert(sa.settings == 1 && !strcmp(sa.setting.key, "scale") && sa.setting.value.f == 2.0);
    assert(!h->api.settings_write(&menu, "ex.a", "scale", &w)); modhost_frame(h); assert(sa.settings == 1); /* unchanged: no event */
    assert(h->api.settings_write(&menu, "ex.a", "style", &w) == BBM_ERR_ARG && h->api.settings_write(&a, "ex.a", "scale", &w) == BBM_ERR_STATE);
    /* values survive a reload and come back from the file */
    mh_res_release_all(&a); mh_settings_undeclare(&h->mods[0]);
    assert(!h->api.settings_info(&menu, "ex.a", 0, &got, &val) && (got.flags & BBM_SET_INACTIVE));
    assert(!h->api.setting_declare(&a, &d) && !h->api.setting_declare(&a, &f) && !h->api.setting_declare(&a, &e));
    v1 = (bbm_setting_value){sizeof v1}; assert(!h->api.setting_get(&a, "level", &v1) && v1.i == 10);
    assert(!h->api.setting_get(&a, "scale", &v1) && v1.f == 2.0);
    /* ...and from the file after the in-memory state is dropped (a new run) */
    mh_settings_free(&h->mods[0]);
    assert(!h->api.setting_declare(&a, &d) && !h->api.setting_declare(&a, &f) && !h->api.setting_declare(&a, &t));
    assert(!h->api.setting_get(&a, "level", &v1) && v1.i == 10 && !h->api.setting_get(&a, "scale", &v1) && v1.f == 2.0);
    assert(!h->api.setting_get(&a, "title", &v1) && !strcmp(v1.s, "Hi"));
    h->mods[0].inst = h->mods[1].inst = h->mods[2].inst = NULL;
    mh_res_release_all(&a); mh_res_release_all(&b); mh_res_release_all(&menu);
    for (int i = 0; i < 3; i++) mh_settings_free(&h->mods[i]);
    modhost_destroy(h);
    tc_rmtree(root);
    puts("ui layers + settings + manage: validation, arbitration, capture, fades, persistence, events: OK");
    return 0;
}
