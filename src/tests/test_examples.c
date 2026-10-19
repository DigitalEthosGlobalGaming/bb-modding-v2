/* The example mods against a 1.09-sized stand-in image:
 *   live-patches: clicking "Can't die" applies the real XML entry, a hot reload
 *   re-applies it (restore_state), unload restores every byte;
 *   devtools: every section draws without errors. */
#include "tcompat.h"
#include "native_menu.h"
static mh_menu_snapshot snapshot;
static bbm_menu_event pending;
static int has_pending;
static uint8_t keys[BBM_KEY_COUNT];
static const uint8_t *keyboard(void*u,int*n){(void)u;*n=BBM_KEY_COUNT;return keys;}
static bbm_status denied_patch(bbm_handle m,const char*path,const char*name,bbm_id*out){(void)m;(void)path;(void)name;(void)out;return BBM_ERR_NOTFOUND;}
static int publish(void*u,const mh_menu_snapshot*s){(void)u;snapshot=*s;return 1;}
static size_t drain(void*u,bbm_menu_event*out,size_t cap,uint64_t*drops){(void)u;*drops=0;if(!has_pending||!cap)return 0;*out=pending;has_pending=0;return 1;}
static mh_menu_item *patch(const char*key){for(size_t i=0;i<snapshot.count;i++)if(!strcmp(snapshot.items[i].owner,"example.live-patches")&&!strcmp(snapshot.items[i].node.key,key))return &snapshot.items[i];return NULL;}
static void activate(const char*key){mh_menu_item*p=patch(key);if(!p)abort();pending=(bbm_menu_event){.size=sizeof pending,.kind=BBM_MENU_ACTIVATED,.node=p->id,.revision=p->version};has_pending=1;}

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); fails++; } } while (0)

static const char *click; /* checkbox label to click on the next draw */
static int texts, errors;
static char all_text[4096];
static int begin(void *u, const char *t, const char *id) { (void)u; (void)t; (void)id; return 1; }
static void end(void *u) { (void)u; }
static void text(void *u, const char *s) {
    (void)u; texts++;
    if (strlen(all_text) + strlen(s) + 2 < sizeof all_text) { strcat(all_text, s); strcat(all_text, "\n"); }
}
static void colored(void *u, float r, float g, float b, float a, const char *s) {
    (void)u; (void)r; (void)g; (void)b; (void)a; errors++; fprintf(stderr, "panel error: %s\n", s);
}
static int checkbox(void *u, const char *label, int32_t *v) {
    (void)u;
    if (click && !strcmp(label, click)) { *v = !*v; click = NULL; return 1; }
    return 0;
}
static int header(void *u, const char *l) { (void)u; (void)l; return 1; }
static int button(void *u, const char *l) { (void)u; return !strcmp(l, "Read 64 bytes") || !strcmp(l, "Scan"); }
static int input(void *u, const char *l, char *buf, size_t cap) {
    (void)u;
    if (!strcmp(l, "address##peek") || !strcmp(l, "address##conv")) { snprintf(buf, cap, "0x400000"); return 1; }
    if (!strcmp(l, "pattern")) { snprintf(buf, cap, "DE AD ?? EF"); return 1; }
    return 0;
}

static uint64_t sum(const uint8_t *p, size_t n) {
    uint64_t s = 0;
    for (size_t i = 0; i < n; i += 4) s = s * 1099511628211ull + p[i] + ((uint64_t)p[i + 1] << 8);
    return s;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: test_examples <live_patches.so> <devtools.so> <Bloodborne.xml>\n"); return 2; }
    char root[300], mods[400], cache[400], toml[1500],xml[1200];
    tc_tmpdir(root, sizeof root, "bbmod-ex");
    snprintf(mods, sizeof mods, "%s/mods", root);
    snprintf(cache, sizeof cache, "%s/cache", root);
    if(snprintf(xml,sizeof xml,"%s",argv[3])>=(int)sizeof xml)return 2;
    for(char*p=xml;*p;p++)if(*p=='\\')*p='/';
    snprintf(toml, sizeof toml, "[mod]\nid = \"example.live-patches\"\nabi = \"1.9\"\ndll = \"m.dll\"\n[patches]\nxml = \"%s\"\n", xml);
    tc_make_mod(mods, "live", argv[1], toml);
    tc_make_mod(mods, "dev", argv[2], "[mod]\nid = \"example.devtools\"\nabi = \"1.1\"\ndll = \"m.dll\"\n");

    size_t size = 0x6000000;
    uint8_t *img = tc_alloc_rwx(size);
    for (size_t i = 0; i < size; i += 4096) img[i] = (uint8_t)(i >> 12);
    img[0x1234] = 0xDE; img[0x1235] = 0xAD; img[0x1236] = 0x00; img[0x1237] = 0xEF;

    modhost_config cfg = {0};
    cfg.mods_dir = mods; cfg.cache_dir = cache;
    cfg.eboot_base = (uintptr_t)img; cfg.eboot_size = size; cfg.ps4_eboot_base = 0x400000;
    cfg.ui.begin_panel = begin; cfg.ui.end_panel = end; cfg.ui.text = text; cfg.ui.text_colored = colored;
    cfg.ui.checkbox = checkbox; cfg.ui.collapsing_header = header; cfg.ui.button = button; cfg.ui.input_text = input;
    cfg.input.keyboard=keyboard;
    modhost *h = modhost_create(&cfg);
    mh_menu_sink native={NULL,publish,drain,NULL,BBM_MENU_CAP_LISTS|BBM_MENU_CAP_LIVE};mh_menu_bind(h,&native);
    CHECK(modhost_scan(h) == 2);
    for (size_t i = 0; i < 2; i++) { modhost_mod_view v; modhost_mod_view_at(h, i, &v); CHECK(v.state == MODHOST_ACTIVE); }

    uint64_t s0 = sum(img, size);
    modhost_frame(h);CHECK(snapshot.count==10);CHECK(patch("patch-0")&&patch("status"));
    for(size_t i=0;i<h->nmods;i++)if(!strcmp(h->mods[i].m.id,"example.live-patches")){
        for(size_t j=0;j<h->mods[i].inst->nres;j++)CHECK(h->mods[i].inst->res[j].kind!=MH_RES_PANEL);
    }
    activate("patch-0");modhost_frame(h);
    if(!patch("patch-0")->node.value)fprintf(stderr,"Native patch failure: %s | %s\n",patch("patch-0")->node.help,patch("status")->node.label);
    CHECK(patch("patch-0")->node.value==1);
    uint64_t s1 = sum(img, size);
    CHECK(s1 != s0);                                    /* the real XML entry went in */
    CHECK(modhost_reload_now(h, "example.live-patches") == 0);
    modhost_frame(h);CHECK(patch("patch-0")->node.value==1);
    CHECK(sum(img, size) == s1);                        /* re-applied after hot reload */
    activate("patch-0");modhost_frame(h);CHECK(!patch("patch-0")->node.value&&sum(img,size)==s0);
    /* SDL scancodes: Ctrl+F1 still toggles the native row and owned patch. */
    keys[224]=keys[58]=1;modhost_frame(h);modhost_frame(h);CHECK(patch("patch-0")->node.value&&sum(img,size)==s1);
    memset(keys,0,sizeof keys);modhost_frame(h);
    /* A failed patch request must revert the UI's optimistic toggle to the
     * actual owned state and expose a failure without touching game memory. */
    __typeof__(h->api.patch_xml) original_patch=h->api.patch_xml;h->api.patch_xml=denied_patch;
    activate("patch-1");modhost_frame(h);
    CHECK(!patch("patch-1")->node.value&&strstr(patch("patch-1")->node.help,"Failed:"));
    CHECK(strstr(patch("status")->node.label,"Failed:")&&sum(img,size)==s1);
    h->api.patch_xml=original_patch;
    CHECK(modhost_unload_now(h, "example.live-patches") == 0);
    CHECK(sum(img, size) == s0);                        /* every byte restored */

    all_text[0] = 0;
    modhost_draw_panels(h);                             /* devtools: all sections open */
    modhost_draw_panels(h);
    CHECK(texts>0);
    CHECK(strstr(all_text, "eboot at host") != NULL);
    CHECK(strstr(all_text, "PS4 0x400000") != NULL);
    CHECK(strstr(all_text, "found at PS4 0x401234") != NULL);
    CHECK(errors == 0);

    modhost_destroy(h);
    tc_free_rwx(img, size);
    tc_rmtree(root);
    if (fails) return 1;
    puts("example mods test passed");
    return 0;
}
