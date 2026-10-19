/* SPDX-License-Identifier: GPL-2.0-or-later
 * Virtual overlay: resolve a game path to a mod's file at runtime, so asset
 * mods and file_override take effect without the launch-time link tree. Later
 * mods win; file_override (order 1000000) sits on top. Matches the layout
 * rules of scripts/mods.py (case-insensitive, dvdroot_ps4 or bare game
 * folders). */
#include <ctype.h>

#include "internal.h"
#include "native_menu.h"

static int vfs_find(modhost *h, const char *key) {
    for (size_t i = 0; i < h->nvfs; i++)
        if (!strcmp(h->vfs[i].key, key)) return (int)i;
    return -1;
}

static int vfs_add(modhost *h, const char *game_path, const char *source, struct bbm_mod *owner, int order) {
    char key[MH_PATH];
    mh_strlcpy(key, game_path, sizeof key);
    mh_lower(key);
    int idx = vfs_find(h, key);
    if (idx >= 0) {
        if (order < h->vfs[idx].order) return 0; /* existing wins */
        mh_strlcpy(h->vfs[idx].source, source, sizeof h->vfs[idx].source);
        h->vfs[idx].owner = owner;
        h->vfs[idx].order = order;
        return 0;
    }
    if (h->nvfs == h->capvfs) {
        size_t cap = h->capvfs ? h->capvfs * 2 : 64;
        mh_vfs_entry *n = realloc(h->vfs, cap * sizeof *n);
        if (!n) return -1;
        h->vfs = n;
        h->capvfs = cap;
    }
    mh_vfs_entry *v = &h->vfs[h->nvfs++];
    mh_strlcpy(v->key, key, sizeof v->key);
    mh_strlcpy(v->source, source, sizeof v->source);
    v->owner = owner;
    v->order = order;
    return 0;
}

/* Lets resolve skip the lock when there are no overrides at all (most file
 * opens). Always written under vfs_lock, read without it. */
static void publish(modhost *h) { __atomic_store_n(&h->vfs_any, h->nvfs != 0, __ATOMIC_RELEASE); }

int mh_vfs_add(modhost *h, const char *game_path, const char *source, struct bbm_mod *owner, int order) {
    plat_mutex_lock(h->vfs_lock);
    int rc = vfs_add(h, game_path, source, owner, order);
    publish(h);
    plat_mutex_unlock(h->vfs_lock);
    return rc;
}

void mh_vfs_remove_owner(modhost *h, struct bbm_mod *owner, const char *game_path) {
    plat_mutex_lock(h->vfs_lock);
    char key[MH_PATH] = "";
    if (game_path) { mh_strlcpy(key, game_path, sizeof key); mh_lower(key); }
    size_t w = 0;
    for (size_t i = 0; i < h->nvfs; i++) {
        int drop = (h->vfs[i].owner == owner) && (!game_path || !strcmp(h->vfs[i].key, key));
        if (!drop) h->vfs[w++] = h->vfs[i];
    }
    h->nvfs = w;
    publish(h);
    plat_mutex_unlock(h->vfs_lock);
}

struct walk_ctx {
    modhost *h;
    char rel[MH_PATH]; /* relative path under dvdroot_ps4, lower case */
    const char *root;
    int order;
};

static void walk_dir(modhost *h, const char *disk_dir, const char *rel_prefix, int order);

static void walk_cb(void *user, const char *name, int is_dir) {
    struct walk_ctx *c = user;
    char disk[MH_PATH], rel[MH_PATH];
    mh_path_join(disk, sizeof disk, c->root, name);
    if (c->rel[0]) snprintf(rel, sizeof rel, "%s/%s", c->rel, name);
    else mh_strlcpy(rel, name, sizeof rel);
    if (is_dir) {
        walk_dir(c->h, disk, rel, c->order);
    } else {
        char game[MH_PATH];
        snprintf(game, sizeof game, "dvdroot_ps4/%s", rel);
        vfs_add(c->h, game, disk, NULL, c->order); /* caller holds vfs_lock */
    }
}

static void walk_dir(modhost *h, const char *disk_dir, const char *rel_prefix, int order) {
    struct walk_ctx c = {h, "", disk_dir, order};
    mh_strlcpy(c.rel, rel_prefix, sizeof c.rel);
    plat_list_dir(disk_dir, walk_cb, &c);
}

/* Finds the dvdroot_ps4 inside a mod folder (direct, app0/, serial/, or bare
 * game folders), returns its disk path or "". */
static void dvdroot_of(const char *dir, char *out, size_t cap) {
    static const char *wrappers[] = {"", "app0", "CUSA03173", "CUSA00900", "CUSA00207", "CUSA00208",
                                     "CUSA00299", "CUSA01363", "CUSA03179", "CUSA03014", "CUSA03023", NULL};
    for (int i = 0; wrappers[i]; i++) {
        char base[MH_PATH];
        if (wrappers[i][0]) mh_path_join(base, sizeof base, dir, wrappers[i]);
        else mh_strlcpy(base, dir, sizeof base);
        char dv[MH_PATH];
        mh_path_join(dv, sizeof dv, base, "dvdroot_ps4");
        int d = 0;
        if (plat_stat(dv, NULL, NULL, &d) == 0 && d) { mh_strlcpy(out, dv, cap); return; }
    }
    out[0] = 0;
}

/* Top-level folders of the game's dvdroot_ps4 (same list as scripts/mods.py):
 * a mod without a dvdroot_ps4 folder contributes only these, never its DLL,
 * mod.toml or source files. */
static const char *game_folders[] = {"action", "chr", "event", "facegen", "font", "map", "menu", "movie",
                                     "msg", "mtd", "obj", "other", "param", "paramdef", "parts", "remo",
                                     "script", "sfx", "shader", "sound", NULL};

struct bare_ctx { modhost *h; const char *dir; int order; };
static void bare_cb(void *user, const char *name, int is_dir) {
    struct bare_ctx *c = user;
    if (!is_dir) return;
    char lower[MH_NAME];
    mh_strlcpy(lower, name, sizeof lower);
    mh_lower(lower);
    for (int i = 0; game_folders[i]; i++) {
        if (strcmp(lower, game_folders[i])) continue;
        char disk[MH_PATH];
        mh_path_join(disk, sizeof disk, c->dir, name);
        walk_dir(c->h, disk, lower, c->order);
        return;
    }
}

void mh_vfs_rebuild_assets(modhost *h) {
    plat_mutex_lock(h->vfs_lock);
    /* Drop folder-sourced entries (owner == NULL); keep file_override entries. */
    size_t w = 0;
    for (size_t i = 0; i < h->nvfs; i++)
        if (h->vfs[i].owner) h->vfs[w++] = h->vfs[i];
    h->nvfs = w;

    /* Apply in load order so later mods win; order index rises per mod. */
    for (size_t i = 0; i < h->nmods; i++) {
        mh_entry *e = &h->mods[i];
        if (e->state != MODHOST_ACTIVE) continue;
        char dv[MH_PATH];
        dvdroot_of(e->dir, dv, sizeof dv);
        if (dv[0]) {
            walk_dir(h, dv, "", (int)i);
        } else {
            struct bare_ctx c = {h, e->dir, (int)i};
            plat_list_dir(e->dir, bare_cb, &c);
        }
    }
    publish(h);
    plat_mutex_unlock(h->vfs_lock);
}

/* Any thread (the game's file I/O). */
int modhost_vfs_resolve(modhost *h, const char *game_path, char *out, size_t cap) {
    /* Framework-composed native movie: one owner even with several UI mods.
     * Resolve before the ordinary layers to keep code and widget layout paired. */
    if(mh_menu_bridge_resolve(h,game_path,out,cap))return 1;
    if (!__atomic_load_n(&h->vfs_any, __ATOMIC_ACQUIRE)) return 0; /* no overrides: the common case */
    char key[MH_PATH];
    mh_strlcpy(key, game_path, sizeof key);
    mh_lower(key);
    plat_mutex_lock(h->vfs_lock);
    int idx = vfs_find(h, key);
    if (idx >= 0) mh_strlcpy(out, h->vfs[idx].source, cap);
    plat_mutex_unlock(h->vfs_lock);
    return idx >= 0;
}
