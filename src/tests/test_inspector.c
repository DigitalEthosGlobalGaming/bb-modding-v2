/* The inspector mod through its real UI (needs Zydis for the hooks):
 *   call logger  - args captured, call passes through, history kept;
 *   register watch - mid-function hook sees every register and preserves the
 *                  red zone and the flags;
 *   params       - a goods table in the heap is found, validated, decoded with
 *                  the generated layout and English names, edited live, and the
 *                  edit reverts on unload;
 *   live scan    - first scan + refine finds a changing heap value;
 *   value finder, struct view - as before. */
#include "tcompat.h"
#include "modhost.h"
#include <inttypes.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); fails++; } } while (0)
typedef __attribute__((sysv_abi)) int (*fn_t)(int);
typedef __attribute__((sysv_abi)) long (*fn2_t)(long, void *);

#define QN 32
static struct { char label[64]; char value[64]; } in_q[QN];
static int in_n;
static char btn_q[QN][64];
static int btn_n;
static struct { char label[64]; char item[64]; } combo_q[8];
static int combo_n;
static char cap[1 << 17];
static char logbuf[1 << 20];
static void logcb(void *u, const char *id, int lvl, const char *m) {
    (void)u; (void)id; (void)lvl;
    if (strlen(logbuf) + strlen(m) + 2 < sizeof logbuf) { strcat(logbuf, m); strcat(logbuf, "\n"); }
}
static char chk_q[8][96];
static int chk_n;
static void queue_check(const char *l) { snprintf(chk_q[chk_n++], 96, "%s", l); }
static void cap_reset(void) { cap[0] = 0; }
static void cap_add(const char *s) { if (strlen(cap) + strlen(s) + 2 < sizeof cap) { strcat(cap, s); strcat(cap, "\n"); } }
static void queue_input(const char *l, const char *v) { snprintf(in_q[in_n].label, 64, "%s", l); snprintf(in_q[in_n].value, 64, "%s", v); in_n++; }
static void queue_button(const char *l) { snprintf(btn_q[btn_n++], 64, "%s", l); }
static void queue_combo(const char *l, const char *item) { snprintf(combo_q[combo_n].label, 64, "%s", l); snprintf(combo_q[combo_n].item, 64, "%s", item); combo_n++; }

static int begin(void *u, const char *t, const char *id) { (void)u; (void)t; (void)id; return 1; }
static void end(void *u) { (void)u; }
static void text(void *u, const char *s) { (void)u; cap_add(s); }
static void colored(void *u, float r, float g, float b, float a, const char *s) { (void)u;(void)r;(void)g;(void)b;(void)a; cap_add(s); }
static int button(void *u, const char *l) {
    (void)u;
    cap_add(l); /* buttons are visible text too (row list) */
    for (int i = 0; i < btn_n; i++) if (!strcmp(btn_q[i], l)) { btn_q[i][0] = 1; btn_q[i][1] = 0; return 1; }
    return 0;
}
static int input(void *u, const char *l, char *buf, size_t n) {
    (void)u;
    for (int i = 0; i < in_n; i++) if (!strcmp(in_q[i].label, l) && in_q[i].value[0]) { snprintf(buf, n, "%s", in_q[i].value); in_q[i].value[0] = 0; return 1; }
    return 0;
}
static int header(void *u, const char *l) { (void)u; (void)l; return 1; }
static int checkbox(void *u, const char *l, int32_t *v) {
    (void)u;
    for (int i = 0; i < chk_n; i++) if (!strcmp(chk_q[i], l)) { chk_q[i][0] = 0; *v = 1; return 1; }
    return 0;
}
static int sliderint(void *u, const char *l, int32_t *v, int32_t lo, int32_t hi) { (void)u;(void)l;(void)lo;(void)hi;(void)v; return 0; }
static int combo(void *u, const char *l, int32_t *cur, const char *items) {
    (void)u;
    for (int q = 0; q < combo_n; q++) {
        if (strcmp(combo_q[q].label, l) || !combo_q[q].item[0]) continue;
        int idx = 0;
        for (const char *p = items; *p; p += strlen(p) + 1, idx++) {
            if (!strncmp(p, combo_q[q].item, strlen(combo_q[q].item))) { *cur = idx; combo_q[q].item[0] = 0; return 1; }
        }
    }
    return 0;
}

static modhost *h;
/* Run frames until the log contains `needle` (results are logged on the game thread). */
static int log_until(const char *needle) {
    for (int i = 0; i < 400; i++) {
        modhost_frame(h);
        if (strstr(logbuf, needle)) return 1;
        tc_sleep_us(25000);
    }
    fprintf(stderr, "timed out waiting for log \"%s\"\n", needle);
    return 0;
}
/* Draw until `needle` shows up (the worker thread finishes), up to ~20 s. */
static int draw_until(const char *needle) {
    for (int i = 0; i < 400; i++) {
        cap_reset();
        modhost_frame(h);
        modhost_draw_panels(h);
        if (strstr(cap, needle)) return 1;
        tc_sleep_us(50000);
    }
    fprintf(stderr, "timed out waiting for \"%s\"\n", needle);
    return 0;
}

static char cli_reply[8192];
static int cli_command(const char *dir, const char *command, int stale) {
    char path[700], req[700], response[700], session[256] = {0}, text[4600];
    snprintf(path, sizeof path, "%s/session.txt", dir);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fread(session, 1, sizeof session - 1, f); fclose(f);
    snprintf(text, sizeof text, "%s\n%s", stale ? "old-session" : session, command);
    snprintf(path, sizeof path, "%s/test.tmp", dir);
    snprintf(req, sizeof req, "%s/test.req", dir);
    snprintf(response, sizeof response, "%s/test.res", dir);
    remove(response);
    if (tc_write(path, text) || rename(path, req)) return 0;
    for (int i = 0; i < 100; i++) {
        modhost_frame(h);
        f = fopen(response, "rb");
        if (f) {
            memset(cli_reply, 0, sizeof cli_reply);
            fread(cli_reply, 1, sizeof cli_reply - 1, f); fclose(f); remove(response);
            return 1;
        }
        tc_sleep_us(20000);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    char root[300], mods[400], cache[400], s[256];
    tc_tmpdir(root, sizeof root, "bbmod-insp");
    snprintf(mods, sizeof mods, "%s/mods", root);
    snprintf(cache, sizeof cache, "%s/cache", root);
    char control[400], manifest[1600];
    snprintf(control, sizeof control, "%s/control", root);
    snprintf(manifest, sizeof manifest, "[mod]\nid = \"example.inspector\"\nabi = \"1.1\"\ndll = \"m.dll\"\n[scan]\nprocess_memory = true\n[control]\ndir = \"%s\"\n", control);
    tc_make_mod(mods, "insp", argv[1], manifest);

    size_t size = 0x100000;
    uint8_t *e = tc_alloc_rwx(size);
    static const uint8_t good[] = {0x55, 0x48, 0x89, 0xE5, 0x89, 0xF8, 0x83, 0xC0, 0x05,
        0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90, 0x5D, 0xC3};
    memcpy(e + 0x200, good, sizeof good);          /* f(x) = x + 5 at PS4 0x400200 */
    /* g(x, p) at 0x400300: stores x in the red zone, sets ZF, writes p[0x24] = x
     * (the watched instruction at 0x400309), then returns red-zone x + 0x100 if
     * ZF survived. Returns x + 0x100 only if the watch thunk preserved both. */
    static const uint8_t g[] = {
        0x48, 0x89, 0x7C, 0x24, 0xF8,   /* 300 mov [rsp-8], rdi   (red zone) */
        0x31, 0xC9,                     /* 305 xor ecx, ecx                  */
        0x39, 0xFF,                     /* 307 cmp edi, edi       (ZF = 1)   */
        0x89, 0x7E, 0x24,               /* 309 mov [rsi+0x24], edi  <- watch */
        0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
        0x0F, 0x94, 0xC1,               /* 314 sete cl                       */
        0x48, 0x8B, 0x44, 0x24, 0xF8,   /* 317 mov rax, [rsp-8]              */
        0x48, 0xC1, 0xE1, 0x08,         /* 31c shl rcx, 8                    */
        0x48, 0x01, 0xC8,               /* 320 add rax, rcx                  */
        0xC3 };
    memcpy(e + 0x300, g, sizeof g);
    e[0x1000] = 0x44; e[0x1001] = 0x43; e[0x1002] = 0x42; e[0x1003] = 0x41;
    for (size_t i = 0x2000; i < 0x2040; i++) e[i] = (uint8_t)i;
    fn_t f = (fn_t)(void *)(e + 0x200);
    fn2_t f2 = (fn2_t)(void *)(e + 0x300);
    CHECK(f2(7, e + 0x3000) == 7 + 0x100);         /* sanity before hooking */

    /* A goods param table in the heap, as the game would have it loaded. */
    size_t tsz = 0x40 + 2 * 24 + 2 * 108;
    uint8_t *tab = tc_alloc_rwx(4096);
    memset(tab, 0, 4096);
    uint16_t cnt = 2; memcpy(tab + 0x0A, &cnt, 2);
    memcpy(tab + 0x0C, "EQUIP_PARAM_GOODS_ST", 20);
    int32_t ids[2] = {1000, 1100};
    for (int i = 0; i < 2; i++) {
        uint64_t data = 0x40 + 2 * 24 + (uint64_t)i * 108;
        memcpy(tab + 0x40 + i * 24, &ids[i], 4);
        memcpy(tab + 0x40 + i * 24 + 8, &data, 8);
    }
    uint8_t *row0 = tab + 0x40 + 2 * 24;
    int16_t maxnum = 20; memcpy(row0 + 54, &maxnum, 2);   /* maxNum (s16 @ +54) */
    row0[68] |= 0x80;                                     /* isConsume (bit 7 @ +68) */
    (void)tsz;

    /* A "live" value in the heap for the scan. */
    volatile uint32_t *hp = (volatile uint32_t *)(tab + 0x800);
    *hp = 987654;

    modhost_config cfg = {0};
    cfg.mods_dir = mods; cfg.cache_dir = cache;
    cfg.eboot_base = (uintptr_t)e; cfg.eboot_size = size; cfg.ps4_eboot_base = 0x400000;
    cfg.ui.begin_panel = begin; cfg.ui.end_panel = end; cfg.ui.text = text; cfg.ui.text_colored = colored;
    cfg.ui.button = button; cfg.ui.input_text = input; cfg.ui.collapsing_header = header;
    cfg.ui.checkbox = checkbox; cfg.ui.slider_int = sliderint; cfg.ui.combo = combo;
    cfg.log = logcb;
    h = modhost_create(&cfg);
    CHECK(modhost_scan(h) == 1);
    modhost_mod_view v; modhost_mod_view_at(h, 0, &v);
    CHECK(v.state == MODHOST_ACTIVE);

    CHECK(cli_command(control, "status", 0));
    CHECK(strstr(cli_reply, "OK\n") && strstr(cli_reply, "busy=false"));
    CHECK(cli_command(control, "read 0x402100 4", 0));
    CHECK(strstr(cli_reply, "00000000") != NULL);
    CHECK(cli_command(control, "patch 0x402100 ffffffff 01020304", 0));
    CHECK(strstr(cli_reply, "ERR\n") != NULL && e[0x2100] == 0);
    CHECK(cli_command(control, "patch 0x402100 00000000 01020304", 1));
    CHECK(strstr(cli_reply, "stale session") != NULL && e[0x2100] == 0);
    CHECK(cli_command(control, "patch 0x402100 00000000 01020304", 0));
    CHECK(e[0x2100] == 1 && e[0x2103] == 4);
    unsigned long long patch_id = 0;
    CHECK(sscanf(cli_reply, "OK\npatch_id=%llu", &patch_id) == 1);
    snprintf(s, sizeof s, "release %llu", patch_id);
    CHECK(cli_command(control, s, 0));
    CHECK(e[0x2100] == 0);
    CHECK(cli_command(control, "read 0x402100 9999999", 0));
    CHECK(strstr(cli_reply, "ERR\n") != NULL);
    /* Leave a CLI patch installed; unload must revert it automatically. */
    CHECK(cli_command(control, "patch 0x402100 00000000 01020304", 0));

    /* ---- call logger ---- */
    queue_input("address##log", "0x400200"); queue_input("label##log", "testfn"); queue_button("Log calls");
    cap_reset(); modhost_draw_panels(h);
    CHECK(strstr(cap, "hook failed") == NULL);
    CHECK(strstr(logbuf, "call logger: hooked testfn at 0x400200") != NULL);
    queue_check("log testfn calls to file##lg0");
    cap_reset(); modhost_draw_panels(h);
    CHECK(f(7) == 12); modhost_frame(h);
    CHECK(f(8) == 13); modhost_frame(h);
    CHECK(f(100) == 105);
    modhost_frame(h);
    CHECK(strstr(logbuf, "call testfn #3: rdi=0x64") != NULL);
    cap_reset(); modhost_draw_panels(h);
    CHECK(strstr(cap, "testfn") && strstr(cap, "3 calls"));
    CHECK(strstr(cap, "last rdi=0x64") != NULL);

    /* ---- register watch, mid-function ---- */
    queue_input("address##watch", "0x400309"); queue_input("label##watch", "store"); queue_button("Watch");
    cap_reset(); modhost_draw_panels(h);
    CHECK(strstr(cap, "hook failed") == NULL);
    uint8_t *obj = e + 0x3000;
    queue_check("log store hits to file##lw0");
    cap_reset(); modhost_draw_panels(h);
    CHECK(f2(42, obj) == 42 + 0x100);              /* red zone and flags survived */
    CHECK(*(int32_t *)(obj + 0x24) == 42);         /* the store itself still ran */
    cap_reset(); modhost_draw_panels(h);
    CHECK(strstr(cap, "1 hits") != NULL);
    snprintf(s, sizeof s, "rsi=%#" PRIxPTR "", (uintptr_t)obj);
    CHECK(strstr(cap, s) != NULL);                 /* the object pointer */
    CHECK(strstr(cap, "rdi=0x2a") != NULL);        /* the value being written */
    modhost_frame(h);
    snprintf(s, sizeof s, "watch store hit #1: rax=");
    CHECK(strstr(logbuf, s) != NULL);
    snprintf(s, sizeof s, "rsi=%#" PRIxPTR "", (uintptr_t)obj);
    CHECK(strstr(strstr(logbuf, "watch store hit #1") ? strstr(logbuf, "watch store hit #1") : logbuf, s) != NULL);

    int16_t now;
    /* ---- params ---- */
    queue_button("Locate params");
    CHECK(draw_until("param table(s)"));
    CHECK(strstr(cap, "found 0 param") == NULL);
    snprintf(s, sizeof s, "EQUIP_PARAM_GOODS_ST at h0x%" PRIxPTR "", (uintptr_t)tab);
    CHECK(strstr(cap, s) != NULL);                 /* found in the heap, at the right base */
    CHECK(strstr(cap, "stride 108") && strstr(cap, "verified (Openborne + memory)"));
    CHECK(strstr(cap, "1000 Blood Vial") && strstr(cap, "1100 Antidote"));
    snprintf(s, sizeof s, "EquipParamGoods (EQUIP_PARAM_GOODS_ST) at h0x%" PRIxPTR ": 2 rows, stride 108, layout ok", (uintptr_t)tab);
    CHECK(strstr(logbuf, s) != NULL);
    queue_button("1000 Blood Vial##row0");
    cap_reset(); modhost_draw_panels(h);
    cap_reset(); modhost_draw_panels(h);
    CHECK(strstr(cap, "maxNum = 20") != NULL);
    CHECK(strstr(cap, "isConsume = 1") != NULL);
    queue_input("##e54", "99"); queue_button("Set##s54");
    cap_reset(); modhost_draw_panels(h);
    cap_reset(); modhost_draw_panels(h);
    memcpy(&now, row0 + 54, 2);
    CHECK(now == 99);                              /* live edit */
    CHECK(strstr(cap, "maxNum = 99 *") != NULL);
    CHECK(strstr(cap, "1 edit(s)") != NULL);
    CHECK(strstr(logbuf, "param edit: EquipParamGoods row 1000 maxNum: 20 -> 99") != NULL);
    queue_button("Dump row to log");
    cap_reset(); modhost_draw_panels(h);
    CHECK(strstr(logbuf, "param EquipParamGoods row 1000 \"Blood Vial\": refId=") != NULL);
    CHECK(strstr(logbuf, "maxNum=99") != NULL);
    queue_button("Dump table to log");
    cap_reset(); modhost_draw_panels(h);
    CHECK(strstr(logbuf, "1100 \"Antidote\":") != NULL);
    queue_input("##e54", "70000"); queue_button("Set##s54");   /* does not fit s16 */
    cap_reset(); modhost_draw_panels(h);
    CHECK(strstr(cap, "does not fit maxNum") != NULL);
    memcpy(&now, row0 + 54, 2);
    CHECK(now == 99);                              /* rejected, unchanged */

    /* ---- live memory scan ---- */
    queue_combo("type##live", "u32"); queue_input("value##live", "987654"); queue_button("First scan");
    CHECK(draw_until(" matches"));
    *hp = 987000;                                  /* "took damage" */
    queue_input("value##live", "987000"); queue_button("= value");
    CHECK(draw_until(" left\n"));          /* the status line "N left", not the hint text */
    snprintf(s, sizeof s, "h0x%" PRIxPTR " = 987000", (uintptr_t)hp);
    CHECK(strstr(cap, s) != NULL);
    CHECK(log_until("live scan #2"));
    CHECK(strstr(logbuf, s) != NULL);              /* the candidate listed in the log */
    *hp = 986500;
    queue_button("decreased");
    cap_reset(); modhost_draw_panels(h);   /* deliver the click */
    CHECK(log_until("live scan #3"));
    cap_reset(); modhost_draw_panels(h);
    snprintf(s, sizeof s, "h0x%" PRIxPTR " = 986500", (uintptr_t)hp);
    CHECK(strstr(cap, s) != NULL);

    /* ---- value finder + struct view ---- */
    queue_input("value##find", "1094861636"); queue_button("Find");
    cap_reset(); modhost_draw_panels(h);
    CHECK(strstr(cap, "1 match") && strstr(cap, "0x401000"));
    queue_input("address##sv", "0x402000"); queue_button("Go##sv");
    modhost_draw_panels(h); modhost_draw_panels(h);
    e[0x2005] = 0xEE;
    cap_reset(); modhost_draw_panels(h);
    CHECK(strstr(cap, "[ee]") != NULL);
    queue_button("Dump to log##sv");
    cap_reset(); modhost_draw_panels(h);
    CHECK(strstr(logbuf, "struct dump at 0x402000") != NULL);
    CHECK(strstr(logbuf, "+000 00 01 02 03 04 ee 06") != NULL);

    /* ---- unload: hooks gone, functions intact, param edit reverted ---- */
    CHECK(modhost_unload_now(h, "example.inspector") == 0);
    CHECK(e[0x2100] == 0 && e[0x2103] == 0);
    CHECK(strstr(logbuf, "inspector unloading") != NULL);
    CHECK(f(7) == 12 && memcmp(e + 0x200, good, sizeof good) == 0);
    CHECK(memcmp(e + 0x300, g, sizeof g) == 0);
    memcpy(&now, row0 + 54, 2);
    CHECK(now == 20);

    modhost_destroy(h);
    tc_free_rwx(tab, 4096);
    tc_free_rwx(e, size);
    tc_rmtree(root);
    if (fails) return 1;
    puts("inspector test passed");
    return 0;
}
