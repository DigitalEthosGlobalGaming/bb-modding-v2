/* SPDX-License-Identifier: GPL-2.0-or-later
 * Mod settings registry (ABI 1.11): declarations and values live in the host so a
 * manager can edit any mod's settings, values survive reload, and every write is
 * validated. Persisted per mod in <mods>/../config/<id>.settings.toml. */
#include "internal.h"
#include <math.h>

#define SET_LIMIT 64

typedef struct mh_set {
    bbm_setting_def def; /* def.type == 0: value known from the file, not declared yet */
    bbm_setting_value val;
    char raw[128];       /* undeclared: the file's text for the key */
    int declared;
} mh_set;

struct mh_settings {
    mh_set v[SET_LIMIT];
    size_t n;
    int loaded;
};

static int valid_key(const char *s) {
    if (!s || !*s || strlen(s) >= 48) return 0;
    for (; *s; ++s)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
              *s == '_' || *s == '.' || *s == '-')) return 0;
    return 1;
}
static int printable_text(const char *s, size_t cap) {
    size_t n = 0;
    while (n < cap && s[n]) {
        unsigned char c = (unsigned char)s[n++];
        if (c < 32 || c == 127) return 0;
    }
    return n < cap;
}
static int enum_count(const char *choices) {
    int n = 1;
    if (!*choices) return 0;
    for (; *choices; ++choices) n += *choices == '|';
    return n;
}

/* Clamp or reject `v` against `d`; 0 = accepted (possibly clamped). */
static int normalize(const bbm_setting_def *d, bbm_setting_value *v) {
    if (v->size != sizeof *v || v->type != d->type) return -1;
    switch (d->type) {
    case BBM_SET_BOOL: v->i = v->i != 0; return 0;
    case BBM_SET_INT:
        if (v->i < (int64_t)d->min) v->i = (int64_t)d->min;
        if (v->i > (int64_t)d->max) v->i = (int64_t)d->max;
        return 0;
    case BBM_SET_FLOAT:
        if (!isfinite(v->f)) return -1;
        if (v->f < d->min) v->f = d->min;
        if (v->f > d->max) v->f = d->max;
        return 0;
    case BBM_SET_ENUM: return v->i >= 0 && v->i < enum_count(d->choices) ? 0 : -1;
    case BBM_SET_KEYBIND: return v->i >= 0 && v->i < BBM_KEY_COUNT ? 0 : -1;
    case BBM_SET_TEXT: {
        if (!printable_text(v->s, sizeof v->s)) return -1;
        size_t max = d->max >= 1 && d->max < 95 ? (size_t)d->max : 95;
        return strlen(v->s) <= max ? 0 : -1;
    }
    }
    return -1;
}
static bbm_setting_value default_value(const bbm_setting_def *d) {
    bbm_setting_value v = {sizeof v, d->type};
    v.i = d->def_i;
    v.f = d->def_f;
    mh_strlcpy(v.s, d->def_s, sizeof v.s);
    return v;
}
static int valid_def(const bbm_setting_def *d) {
    if (d->size != sizeof *d || !valid_key(d->key) || !d->label[0] || (d->flags & ~BBM_SET_RESTART)) return 0;
    if (d->type < BBM_SET_BOOL || d->type > BBM_SET_KEYBIND) return 0;
    if (!printable_text(d->label, sizeof d->label) || !printable_text(d->help, sizeof d->help) ||
        !printable_text(d->group, sizeof d->group) || !printable_text(d->choices, sizeof d->choices) ||
        !printable_text(d->def_s, sizeof d->def_s)) return 0;
    if (d->depends_key[0] && !valid_key(d->depends_key)) return 0;
    if (d->type == BBM_SET_INT || d->type == BBM_SET_FLOAT)
        if (!isfinite(d->min) || !isfinite(d->max) || d->min > d->max || !isfinite(d->step) || d->step < 0) return 0;
    if (d->type == BBM_SET_ENUM && enum_count(d->choices) < 2) return 0;
    bbm_setting_value v = default_value(d);
    if (d->type == BBM_SET_FLOAT && !isfinite(d->def_f)) return 0;
    return normalize(d, &v) == 0;
}

/* ---- Persistence ---------------------------------------------------------- */

static void path_for(mh_entry *e, modhost *h, char *out, size_t cap) {
    snprintf(out, cap, "%s/../config/%s.settings.toml", h->mods_dir, e->m.id);
}
static void unquote(char *s) {
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        s[n - 1] = 0;
        char *w = s;
        for (char *r = s + 1; *r; ++r) {
            if (*r == '\\' && r[1]) ++r;
            *w++ = *r;
        }
        *w = 0;
    }
}
static mh_set *find_set(struct mh_settings *s, const char *key) {
    for (size_t i = 0; i < s->n; ++i) if (!strcmp(s->v[i].def.key, key)) return &s->v[i];
    return NULL;
}
static struct mh_settings *ensure(mh_entry *e) {
    if (!e->settings) e->settings = calloc(1, sizeof *e->settings);
    return e->settings;
}
static void load_file(modhost *h, mh_entry *e, struct mh_settings *s) {
    s->loaded = 1;
    char path[MH_PATH];
    path_for(e, h, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char line[256];
    int in_settings = 0;
    while (fgets(line, sizeof line, f) && s->n < SET_LIMIT) {
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        size_t len = strlen(p);
        while (len && (p[len - 1] == '\n' || p[len - 1] == '\r' || p[len - 1] == ' ')) p[--len] = 0;
        if (!*p || *p == '#') continue;
        if (*p == '[') { in_settings = !strcmp(p, "[settings]"); continue; }
        if (!in_settings) continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = p, *val = eq + 1;
        size_t kn = strlen(key);
        while (kn && (key[kn - 1] == ' ' || key[kn - 1] == '\t')) key[--kn] = 0;
        while (*val == ' ' || *val == '\t') ++val;
        if (!valid_key(key) || find_set(s, key)) continue;
        mh_set *x = &s->v[s->n++];
        memset(x, 0, sizeof *x);
        mh_strlcpy(x->def.key, key, sizeof x->def.key);
        mh_strlcpy(x->raw, val, sizeof x->raw);
    }
    fclose(f);
}
static void save_file(modhost *h, mh_entry *e, struct mh_settings *s) {
    char path[MH_PATH], tmp[MH_PATH + 8], dir[MH_PATH];
    path_for(e, h, path, sizeof path);
    snprintf(dir, sizeof dir, "%s/../config", h->mods_dir);
    plat_mkdirs(dir);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { mh_log(h, e->m.id, BBM_LOG_WARN, "cannot write settings file"); return; }
    fprintf(f, "# bbmod settings for %s\n[settings]\n", e->m.id);
    for (size_t i = 0; i < s->n; ++i) {
        mh_set *x = &s->v[i];
        if (!x->declared) { if (x->raw[0]) fprintf(f, "%s = %s\n", x->def.key, x->raw); continue; }
        switch (x->def.type) {
        case BBM_SET_BOOL: fprintf(f, "%s = %s\n", x->def.key, x->val.i ? "true" : "false"); break;
        case BBM_SET_FLOAT: fprintf(f, "%s = %.9g\n", x->def.key, x->val.f); break;
        case BBM_SET_TEXT: {
            fprintf(f, "%s = \"", x->def.key);
            for (const char *c = x->val.s; *c; ++c) { if (*c == '"' || *c == '\\') fputc('\\', f); fputc(*c, f); }
            fputs("\"\n", f);
            break;
        }
        default: fprintf(f, "%s = %lld\n", x->def.key, (long long)x->val.i);
        }
    }
    if (fclose(f)) { plat_remove(tmp); return; }
    plat_remove(path);
    if (rename(tmp, path)) plat_remove(tmp);
}
/* Parse file text for a declared setting; leaves `v` untouched on failure. */
static void apply_raw(const bbm_setting_def *d, const char *raw, bbm_setting_value *v) {
    bbm_setting_value t = *v;
    char buf[128];
    mh_strlcpy(buf, raw, sizeof buf);
    if (d->type == BBM_SET_TEXT) { unquote(buf); mh_strlcpy(t.s, buf, sizeof t.s); }
    else if (d->type == BBM_SET_BOOL) t.i = !strcmp(buf, "true") || !strcmp(buf, "1");
    else if (d->type == BBM_SET_FLOAT) t.f = strtod(buf, NULL);
    else t.i = strtoll(buf, NULL, 10);
    if (normalize(d, &t) == 0) *v = t;
}

/* ---- Mod API --------------------------------------------------------------- */

static bbm_status declare(bbm_handle m, const bbm_setting_def *d) {
    if (!m || !m->host || !d || m->faulted || m->bypassed || !valid_def(d)) return BBM_ERR_ARG;
    modhost *h = m->host;
    plat_mutex_lock(h->run_lock);
    mh_entry *e = m->entry;
    struct mh_settings *s = ensure(e);
    bbm_status rc = BBM_OK;
    if (!s) rc = BBM_ERR_NOMEM;
    else {
        if (!s->loaded) load_file(h, e, s);
        mh_set *x = find_set(s, d->key);
        if (!x && s->n >= SET_LIMIT) rc = BBM_ERR_STATE;
        else if (x && x->declared) rc = BBM_ERR_CONFLICT; /* undeclared again only after reload */
        else {
            int fresh = !x;
            if (!x) { x = &s->v[s->n++]; memset(x, 0, sizeof *x); }
            char raw[128];
            mh_strlcpy(raw, x->raw, sizeof raw);
            x->def = *d;
            x->val = default_value(d);
            if (raw[0]) apply_raw(d, raw, &x->val);
            (void)fresh;
            x->raw[0] = 0;
            x->declared = 1;
        }
    }
    plat_mutex_unlock(h->run_lock);
    return rc;
}

/* Called when a mod unloads: declarations go, values stay (re-encoded as file text). */
void mh_settings_undeclare(mh_entry *e) {
    struct mh_settings *s = e->settings;
    if (!s) return;
    for (size_t i = 0; i < s->n; ++i) {
        mh_set *x = &s->v[i];
        if (!x->declared) continue;
        switch (x->def.type) {
        case BBM_SET_BOOL: snprintf(x->raw, sizeof x->raw, "%s", x->val.i ? "true" : "false"); break;
        case BBM_SET_FLOAT: snprintf(x->raw, sizeof x->raw, "%.9g", x->val.f); break;
        case BBM_SET_TEXT: {
            size_t w = 0;
            x->raw[w++] = '"';
            for (const char *c = x->val.s; *c && w < sizeof x->raw - 3; ++c) {
                if (*c == '"' || *c == '\\') x->raw[w++] = '\\';
                x->raw[w++] = *c;
            }
            x->raw[w++] = '"';
            x->raw[w] = 0;
            break;
        }
        default: snprintf(x->raw, sizeof x->raw, "%lld", (long long)x->val.i);
        }
        x->declared = 0;
    }
}
void mh_settings_free(mh_entry *e) { free(e->settings); e->settings = NULL; }

static bbm_status get(bbm_handle m, const char *key, bbm_setting_value *out) {
    if (!m || !m->host || !key || !out || out->size != sizeof *out) return BBM_ERR_ARG;
    plat_mutex_lock(m->host->run_lock);
    mh_set *x = m->entry->settings ? find_set(m->entry->settings, key) : NULL;
    bbm_status rc = x && x->declared ? BBM_OK : BBM_ERR_NOTFOUND;
    if (!rc) *out = x->val;
    plat_mutex_unlock(m->host->run_lock);
    return rc;
}

static bbm_status store(modhost *h, mh_entry *e, const char *key, const bbm_setting_value *in, int notify) {
    struct mh_settings *s = e->settings;
    mh_set *x = s ? find_set(s, key) : NULL;
    if (!x || !x->declared) return BBM_ERR_NOTFOUND;
    bbm_setting_value v = *in;
    if (normalize(&x->def, &v)) return BBM_ERR_ARG;
    if (v.i == x->val.i && v.f == x->val.f && !strcmp(v.s, x->val.s)) return BBM_OK;
    x->val = v;
    save_file(h, e, s);
    if (notify && e->inst) {
        bbm_setting_event ev = {sizeof ev};
        mh_strlcpy(ev.key, key, sizeof ev.key);
        ev.value = v;
        struct { char id[MH_NAME]; bbm_setting_event ev; } blob;
        memset(&blob, 0, sizeof blob);
        mh_strlcpy(blob.id, e->m.id, sizeof blob.id);
        blob.ev = ev;
        mh_enqueue(h, MH_IN_SETTING, 0, 0, 0, 0, &blob, sizeof blob);
    }
    return BBM_OK;
}
static bbm_status set(bbm_handle m, const char *key, const bbm_setting_value *in) {
    if (!m || !m->host || !key || !in) return BBM_ERR_ARG;
    plat_mutex_lock(m->host->run_lock);
    bbm_status rc = store(m->host, m->entry, key, in, 0);
    plat_mutex_unlock(m->host->run_lock);
    return rc;
}

/* ---- Manage capability --------------------------------------------------------- */

static int may_manage(bbm_handle m) { return m && m->host && m->entry && m->entry->m.manage; }

static bbm_status settings_info(bbm_handle m, const char *id, uint32_t index, bbm_setting_def *d, bbm_setting_value *v) {
    if (!may_manage(m)) return BBM_ERR_STATE;
    if (!id || !d || !v || d->size != sizeof *d || v->size != sizeof *v) return BBM_ERR_ARG;
    plat_mutex_lock(m->host->run_lock);
    mh_entry *e = mh_find(m->host, id);
    bbm_status rc = BBM_ERR_NOTFOUND;
    if (e && e->settings) {
        uint32_t seen = 0;
        for (size_t i = 0; i < e->settings->n; ++i) {
            mh_set *x = &e->settings->v[i];
            if (!x->def.type) continue; /* file-only, never declared this run */
            if (seen++ != index) continue;
            *d = x->def;
            *v = x->val;
            d->flags &= ~BBM_SET_INACTIVE;
            if (!x->declared) d->flags |= BBM_SET_INACTIVE;
            rc = BBM_OK;
            break;
        }
    }
    plat_mutex_unlock(m->host->run_lock);
    return rc;
}
static bbm_status settings_write(bbm_handle m, const char *id, const char *key, const bbm_setting_value *in) {
    if (!may_manage(m)) return BBM_ERR_STATE;
    if (!id || !key || !in) return BBM_ERR_ARG;
    plat_mutex_lock(m->host->run_lock);
    mh_entry *e = mh_find(m->host, id);
    bbm_status rc = e ? store(m->host, e, key, in, 1) : BBM_ERR_NOTFOUND;
    plat_mutex_unlock(m->host->run_lock);
    return rc;
}

static bbm_status mods_info(bbm_handle m, uint32_t index, bbm_mod_info *out) {
    if (!may_manage(m)) return BBM_ERR_STATE;
    if (!out || out->size != sizeof *out) return BBM_ERR_ARG;
    modhost *h = m->host;
    plat_mutex_lock(h->run_lock);
    bbm_status rc = BBM_ERR_NOTFOUND;
    if (index < h->nmods) {
        mh_entry *e = &h->mods[index];
        memset(out, 0, sizeof *out);
        out->size = sizeof *out;
        out->state = (uint32_t)e->state;
        out->scope = (uint32_t)e->m.scope;
        out->version = e->m.version;
        out->flags = (e->has_code ? BBM_MODF_HAS_CODE : 0) | (e->m.hot_reload ? BBM_MODF_HOT_RELOAD : 0) |
                     (e == m->entry ? BBM_MODF_SELF : 0) | (e->settings && e->settings->n ? BBM_MODF_SETTINGS : 0);
        mh_strlcpy(out->id, e->m.id, sizeof out->id);
        mh_strlcpy(out->name, e->m.name, sizeof out->name);
        mh_strlcpy(out->last_error, e->last_error, sizeof out->last_error);
        if (e->has_manifest) {
            mh_manifest_get(e->manifest_path, "mod.author", out->author, sizeof out->author);
            mh_manifest_get(e->manifest_path, "mod.description", out->description, sizeof out->description);
        }
        size_t at = 0;
        for (int i = 0; i < e->m.n_after && at + 2 < sizeof out->after; ++i)
            at += (size_t)snprintf(out->after + at, sizeof out->after - at, "%s%s", i ? ", " : "", e->m.after[i]);
        at = 0;
        for (int i = 0; i < e->m.n_conflicts && at + 2 < sizeof out->conflicts; ++i)
            at += (size_t)snprintf(out->conflicts + at, sizeof out->conflicts - at, "%s%s", i ? ", " : "", e->m.conflicts[i]);
        rc = BBM_OK;
    }
    plat_mutex_unlock(h->run_lock);
    return rc;
}
static bbm_status mod_request(bbm_handle m, const char *id, uint32_t op) {
    if (!may_manage(m)) return BBM_ERR_STATE;
    if (!id || op < BBM_MODOP_LOAD || op > BBM_MODOP_RELOAD) return BBM_ERR_ARG;
    modhost *h = m->host;
    plat_mutex_lock(h->run_lock);
    mh_entry *e = mh_find(h, id);
    bbm_status rc = !e ? BBM_ERR_NOTFOUND : (e == m->entry && op != BBM_MODOP_LOAD) ? BBM_ERR_ARG : BBM_OK;
    plat_mutex_unlock(h->run_lock);
    if (!rc) mh_request(h, op == BBM_MODOP_LOAD ? MH_REQ_LOAD : op == BBM_MODOP_UNLOAD ? MH_REQ_UNLOAD : MH_REQ_RELOAD, id);
    return rc;
}

void mh_settings_fill(bbm_host *api) {
    api->setting_declare = declare;
    api->setting_get = get;
    api->setting_set = set;
    api->mods_info = mods_info;
    api->mod_request = mod_request;
    api->settings_info = settings_info;
    api->settings_write = settings_write;
}
