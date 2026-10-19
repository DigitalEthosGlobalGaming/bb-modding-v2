/* ModNet over an in-process loopback: handshake, delivery, mismatch, budget. */
#include "tcompat.h"
#include "modhost.h"

uint32_t mh_hash32(const char *a, const char *b); /* host/src/net.c */

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); fails++; } } while (0)

typedef struct node {
    modhost *h;
    uint64_t id;
    char log[16][160];
    int nlog;
} node;

typedef struct qitem { node *to; uint64_t from; uint8_t buf[1400]; size_t len; } qitem;
static qitem q[256];
static int qn;
static node *nodes[16];
static int nnodes;

static int net_send(void *user, uint64_t peer, const void *frame, size_t len, int reliable) {
    (void)reliable;
    node *from = user;
    for (int i = 0; i < nnodes; i++) {
        if (nodes[i]->id == peer && qn < 256) {
            q[qn].to = nodes[i]; q[qn].from = from->id; q[qn].len = len;
            memcpy(q[qn].buf, frame, len);
            qn++;
            return 0;
        }
    }
    return -1;
}
static void pump(void) {
    for (int i = 0; i < qn; i++) modhost_net_receive(q[i].to->h, q[i].from, q[i].buf, q[i].len);
    qn = 0;
}
static void notify(void *user, const char *id, const char *msg) {
    (void)id;
    node *n = user;
    if (n->nlog < 16) snprintf(n->log[n->nlog++], sizeof n->log[0], "%s", msg);
}
static int saw(node *n, const char *s) {
    for (int i = 0; i < n->nlog; i++) if (strstr(n->log[i], s)) return 1;
    return 0;
}

static char root[300], modso[512];
static uint8_t *eboot;

static node *mknode(const char *label, uint64_t id, const char *proto, int flood) {
    node *n = calloc(1, sizeof *n);
    n->id = id;
    char mods[400], cache[400];
    snprintf(mods, sizeof mods, "%s/%s/mods", root, label);
    snprintf(cache, sizeof cache, "%s/%s/cache", root, label);
    char toml[600];
    snprintf(toml, sizeof toml,
        "[mod]\nid = \"test.net\"\ndll = \"m.dll\"\n[multiplayer]\nscope = \"session\"\nprotocol = %s\n"
        "[test]\nname = \"%s\"\nflood = \"%d\"\n", proto, label, flood);
    tc_make_mod(mods, "net", modso, toml);
    modhost_config cfg = {0};
    cfg.mods_dir = mods; cfg.cache_dir = cache;
    cfg.eboot_base = (uintptr_t)eboot; cfg.eboot_size = 4096;
    cfg.net.user = n; cfg.net.send = net_send;
    cfg.ui.user = n; cfg.ui.notify = notify;
    n->h = modhost_create(&cfg);
    CHECK(n->h && modhost_scan(n->h) == 1);
    nodes[nnodes++] = n;
    return n;
}
/* Events are queued and handled at each host's next frame: run a few rounds of
 * "every host runs a frame, then the network delivers". */
static void frames(void) {
    for (int round = 0; round < 5; round++) {
        for (int i = 0; i < nnodes; i++) modhost_frame(nodes[i]->h);
        pump();
    }
}
static void join(node *a, node *b, uint64_t owner) {
    modhost_session_begin(a->h, a->id, owner, 0);
    modhost_session_begin(b->h, b->id, owner, 0);
    modhost_peer_join(a->h, b->id);
    modhost_peer_join(b->h, a->id);
    frames();
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    snprintf(modso, sizeof modso, "%s", argv[1]);
    tc_tmpdir(root, sizeof root, "bbmod-net");
    eboot = tc_alloc_rwx(4096);

    /* 1. Matching mod sets: handshake accepts, messages flow both ways. */
    node *A = mknode("A", 1, "1", 0), *B = mknode("B", 2, "1", 0);
    CHECK(modhost_modset_hash(A->h) == modhost_modset_hash(B->h) && modhost_modset_hash(A->h) != 0);
    join(A, B, 1);
    frames();
    CHECK(saw(A, "rx:hi-B"));
    CHECK(saw(B, "rx:hi-A"));
    CHECK(!saw(A, "mismatch") && !saw(B, "mismatch"));

    /* 2. Different protocol: both sides told, no data delivered. */
    node *C = mknode("C", 3, "1", 0), *D = mknode("D", 4, "2", 0);
    CHECK(modhost_modset_hash(C->h) != modhost_modset_hash(D->h));
    join(C, D, 3);
    frames();
    CHECK(saw(C, "mismatch") && saw(D, "mismatch"));
    CHECK(!saw(C, "rx:") && !saw(D, "rx:"));

    /* 3. Data that arrives before the handshake result must be dropped. */
    node *E = mknode("E", 5, "1", 0);
    modhost_session_begin(E->h, 5, 5, 0);
    modhost_peer_join(E->h, 6); /* our hello goes out; peer 6 never answers */
    modhost_frame(E->h);
    uint8_t f[18 + 6] = {'B', 'B', 'M', 'D', 1, BBM_NET_RELIABLE};
    uint32_t wire = mh_hash32("test.net", "chat");
    uint16_t lo = (uint16_t)(wire & 0xFFFF), plen = 6;
    uint32_t hi = wire >> 16, seq = 0;
    memcpy(f + 6, &lo, 2); memcpy(f + 8, &hi, 4); memcpy(f + 12, &seq, 4); memcpy(f + 16, &plen, 2);
    memcpy(f + 18, "inject", 6);
    modhost_net_receive(E->h, 6, f, sizeof f);
    modhost_frame(E->h);
    CHECK(!saw(E, "rx:inject"));
    qn = 0;

    /* 4. Budget: a flood is cut off near 16 KB, not delivered in full. */
    node *F = mknode("F", 7, "1", 20), *G = mknode("G", 8, "1", 0);
    join(F, G, 7);
    frames();
    int ok = -1, bud = -1;
    for (int i = 0; i < F->nlog; i++) sscanf(F->log[i], "flood ok=%d budget=%d", &ok, &bud);
    printf("flood: ok=%d budget=%d\n", ok, bud);
    CHECK(ok >= 10 && ok <= 14);
    CHECK(bud == 20 - ok);

    for (int i = 0; i < nnodes; i++) modhost_destroy(nodes[i]->h);
    tc_rmtree(root);
    if (fails) return 1;
    puts("modnet test passed");
    return 0;
}
