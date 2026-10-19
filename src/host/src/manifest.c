/* SPDX-License-Identifier: GPL-2.0-or-later
 * mod.toml reader. Supports the subset the manifest needs:
 *   [section] headers, key = "string" | 'string' | number | true/false |
 *   ["array", "of", "strings"], and # comments. */
#include <ctype.h>

#include "internal.h"

void mh_strlcpy(char *dst, const char *src, size_t cap) {
    if (!cap) return;
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

void mh_path_join(char *out, size_t cap, const char *a, const char *b) {
    size_t n = strlen(a);
    int sep = n && (a[n - 1] == '/' || a[n - 1] == '\\');
    snprintf(out, cap, sep ? "%s%s" : "%s/%s", a, b);
}

void mh_lower(char *s) {
    for (; *s; s++) {
        *s = (char)tolower((unsigned char)*s);
        if (*s == '\\') *s = '/';
    }
}

int mh_parse_version(const char *s, bbm_version *v) {
    unsigned a = 0, b = 0, c = 0;
    int n = sscanf(s, "%u.%u.%u", &a, &b, &c);
    if (n < 1) return -1;
    v->major = (uint16_t)a;
    v->minor = (uint16_t)b;
    v->patch = (uint16_t)c;
    v->reserved = 0;
    return 0;
}

static char *skip_ws(char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* Parses one value at p into out (strings unquoted). Arrays are returned raw. */
static int read_value(char *p, char *out, size_t cap) {
    p = skip_ws(p);
    if (*p == '"' || *p == '\'') {
        char q = *p++;
        size_t n = 0;
        while (*p && *p != q) {
            if (q == '"' && *p == '\\' && p[1]) p++;
            if (n + 1 < cap) out[n++] = *p;
            p++;
        }
        out[n] = 0;
        return *p == q ? 0 : -1;
    }
    size_t n = 0;
    while (*p && *p != '#' && *p != '\r' && *p != '\n') {
        if (n + 1 < cap) out[n++] = *p;
        p++;
    }
    while (n && (out[n - 1] == ' ' || out[n - 1] == '\t')) n--;
    out[n] = 0;
    return 0;
}

static int read_array(char *p, char items[][MH_NAME], int max) {
    p = skip_ws(p);
    if (*p != '[') return -1;
    p++;
    int n = 0;
    while (*p && *p != ']') {
        p = skip_ws(p);
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            size_t k = 0;
            char buf[MH_NAME];
            while (*p && *p != q) {
                if (k + 1 < sizeof buf) buf[k++] = *p;
                p++;
            }
            buf[k] = 0;
            if (*p == q) p++;
            if (n < max) mh_strlcpy(items[n++], buf, MH_NAME);
        } else if (*p == ',') {
            p++;
        } else if (*p) {
            p++;
        }
    }
    return n;
}

typedef int (*kv_cb)(void *user, const char *section, const char *key, char *raw);

static int walk(const char *text, kv_cb cb, void *user, char *err, size_t cap) {
    char *copy = strdup(text);
    if (!copy) return -1;
    char section[64] = "";
    int line_no = 0, rc = 0;
    for (char *line = strtok(copy, "\n"); line; line = strtok(NULL, "\n")) {
        line_no++;
        char *p = skip_ws(line);
        if (!*p || *p == '#' || *p == '\r') continue;
        if (*p == '[') {
            char *end = strchr(p, ']');
            if (!end) { if (err) snprintf(err, cap, "line %d: bad section", line_no); rc = -1; break; }
            *end = 0;
            mh_strlcpy(section, skip_ws(p + 1), sizeof section);
            continue;
        }
        char *eq = strchr(p, '=');
        if (!eq) { if (err) snprintf(err, cap, "line %d: expected key = value", line_no); rc = -1; break; }
        char key[64];
        size_t kn = (size_t)(eq - p);
        while (kn && (p[kn - 1] == ' ' || p[kn - 1] == '\t')) kn--;
        if (kn >= sizeof key) kn = sizeof key - 1;
        memcpy(key, p, kn);
        key[kn] = 0;
        if (cb(user, section, key, eq + 1)) { rc = 1; break; }
    }
    free(copy);
    return rc < 0 ? -1 : 0;
}

static int truthy(const char *v) { return !strcmp(v, "true") || !strcmp(v, "1"); }

static int manifest_kv(void *user, const char *sec, const char *key, char *raw) {
    mh_manifest *m = user;
    char v[MH_PATH];
    if (!strcmp(sec, "load") && (!strcmp(key, "after") || !strcmp(key, "conflicts"))) {
        if (!strcmp(key, "after")) m->n_after = read_array(raw, m->after, MH_MAX_DEPS);
        else m->n_conflicts = read_array(raw, m->conflicts, MH_MAX_DEPS);
        return 0;
    }
    if (read_value(raw, v, sizeof v)) return 0;
    if (!strcmp(sec, "mod")) {
        if (!strcmp(key, "id")) mh_strlcpy(m->id, v, sizeof m->id);
        else if (!strcmp(key, "name")) mh_strlcpy(m->name, v, sizeof m->name);
        else if (!strcmp(key, "version")) mh_parse_version(v, &m->version);
        else if (!strcmp(key, "abi")) mh_strlcpy(m->abi, v, sizeof m->abi);
        else if (!strcmp(key, "dll")) mh_strlcpy(m->dll, v, sizeof m->dll);
        else if (!strcmp(key, "runtime")) mh_strlcpy(m->runtime, v, sizeof m->runtime);
    } else if (!strcmp(sec, "load")) {
        if (!strcmp(key, "hot_reload")) m->hot_reload = truthy(v);
        else if (!strcmp(key, "enabled")) m->enabled = truthy(v);
    } else if (!strcmp(sec, "multiplayer")) {
        if (!strcmp(key, "scope")) {
            if (!strcmp(v, "session")) m->scope = MODHOST_SCOPE_SESSION;
            else if (!strcmp(v, "host-authoritative")) m->scope = MODHOST_SCOPE_HOST_AUTHORITATIVE;
            else m->scope = MODHOST_SCOPE_CLIENT;
        } else if (!strcmp(key, "protocol")) {
            m->protocol = (uint32_t)strtoul(v, NULL, 10);
        }
    }
    return 0;
}

int mh_manifest_parse(const char *text, mh_manifest *m, char *err, size_t cap) {
    memset(m, 0, sizeof *m);
    m->hot_reload = 1;
    m->enabled = 1;
    m->protocol = 1;
    mh_strlcpy(m->abi, "1.0", sizeof m->abi);
    mh_strlcpy(m->runtime, "native", sizeof m->runtime);
    if (walk(text, manifest_kv, m, err, cap)) return -1;
    if (!m->id[0]) {
        if (err) snprintf(err, cap, "[mod] id is required");
        return -1;
    }
    for (const char *p = m->id; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '.' && *p != '-' && *p != '_') {
            if (err) snprintf(err, cap, "[mod] id may only use letters, digits, . - _");
            return -1;
        }
    }
    if (!m->name[0]) mh_strlcpy(m->name, m->id, sizeof m->name);
    return 0;
}

static char *read_all(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > (1 << 20)) { fclose(f); return NULL; }
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    if (buf) buf[n] = 0;
    fclose(f);
    return buf;
}

int mh_manifest_load(const char *path, mh_manifest *out, char *err, size_t cap) {
    char *text = read_all(path);
    if (!text) {
        if (err) snprintf(err, cap, "cannot read %s", path);
        return -1;
    }
    int rc = mh_manifest_parse(text, out, err, cap);
    free(text);
    return rc;
}

struct get_q { const char *section, *key; char *out; size_t cap; int found; };

static int get_kv(void *user, const char *sec, const char *key, char *raw) {
    struct get_q *q = user;
    if (strcmp(sec, q->section) || strcmp(key, q->key)) return 0;
    read_value(raw, q->out, q->cap);
    q->found = 1;
    return 1;
}

int mh_manifest_get(const char *path, const char *dotted, char *out, size_t cap) {
    char section[64] = "", key[64];
    const char *dot = strrchr(dotted, '.');
    if (dot) {
        size_t n = (size_t)(dot - dotted);
        if (n >= sizeof section) n = sizeof section - 1;
        memcpy(section, dotted, n);
        section[n] = 0;
        mh_strlcpy(key, dot + 1, sizeof key);
    } else {
        mh_strlcpy(key, dotted, sizeof key);
    }
    char *text = read_all(path);
    if (!text) return -1;
    struct get_q q = {section, key, out, cap, 0};
    walk(text, get_kv, &q, NULL, 0);
    free(text);
    return q.found ? 0 : -1;
}
