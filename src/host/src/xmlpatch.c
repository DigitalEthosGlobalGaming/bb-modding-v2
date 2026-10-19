/* SPDX-License-Identifier: GPL-2.0-or-later
 * shadPS4 / GoldHEN XML patches, applied at runtime as one revertible group.
 * Mirrors scripts/patches.py: AppVer "01.09" (or none), AppElf eboot.bin,
 * Line types bytes, bytes16/32/64, float32/64, utf8, utf16 - plus "mask"
 * lines (pattern search) which the launch-time loader cannot do. */
#include <ctype.h>

#include "internal.h"

#define MH_APP_VER "01.09"

static const char *find_ci(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) return hay;
    }
    return NULL;
}

/* Attribute value inside one tag [tag, end). */
static int attr(const char *tag, const char *end, const char *name, char *out, size_t cap) {
    char pat[64];
    snprintf(pat, sizeof pat, " %s=", name);
    const char *p = tag;
    while ((p = find_ci(p, pat)) && p < end) {
        p += strlen(pat);
        char q = *p;
        if (q != '"' && q != '\'') return -1;
        const char *e = strchr(p + 1, q);
        if (!e || e > end) return -1;
        size_t n = (size_t)(e - p - 1);
        if (n >= cap) n = cap - 1;
        memcpy(out, p + 1, n);
        out[n] = 0;
        /* Unescape the few entities patch files use. */
        char *w = out;
        for (char *r = out; *r;) {
            if (!strncmp(r, "&amp;", 5)) { *w++ = '&'; r += 5; }
            else if (!strncmp(r, "&lt;", 4)) { *w++ = '<'; r += 4; }
            else if (!strncmp(r, "&gt;", 4)) { *w++ = '>'; r += 4; }
            else if (!strncmp(r, "&quot;", 6)) { *w++ = '"'; r += 6; }
            else *w++ = *r++;
        }
        *w = 0;
        return 0;
    }
    return -1;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)tolower((unsigned char)c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static size_t parse_hex_bytes(const char *s, uint8_t *out, size_t cap) {
    size_t n = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s && n < cap) {
        if (*s == ' ') { s++; continue; }
        int a = hexval(s[0]), b = s[1] ? hexval(s[1]) : -1;
        if (a < 0 || b < 0) return 0;
        out[n++] = (uint8_t)(a * 16 + b);
        s += 2;
    }
    return n;
}

/* Encodes one Line value into bytes. Returns length or 0. */
static size_t encode(const char *type, const char *value, uint8_t *out, size_t cap) {
    if (!strcmp(type, "bytes")) return parse_hex_bytes(value, out, cap);
    if (!strncmp(type, "bytes", 5)) {
        int bits = atoi(type + 5);
        if (bits != 16 && bits != 32 && bits != 64) return 0;
        uint64_t v = strtoull(value, NULL, 0);
        memcpy(out, &v, (size_t)bits / 8); /* little endian, like the PS4 */
        return (size_t)bits / 8;
    }
    if (!strcmp(type, "float32")) { float f = strtof(value, NULL); memcpy(out, &f, 4); return 4; }
    if (!strcmp(type, "float64")) { double d = strtod(value, NULL); memcpy(out, &d, 8); return 8; }
    if (!strcmp(type, "utf8")) {
        size_t n = strlen(value);
        if (n + 1 > cap) return 0;
        memcpy(out, value, n + 1);
        return n + 1;
    }
    if (!strcmp(type, "utf16")) {
        size_t n = 0;
        for (const unsigned char *p = (const unsigned char *)value; *p && n + 4 <= cap; p++) {
            out[n++] = *p; /* ASCII subset, as patch files use */
            out[n++] = 0;
        }
        out[n++] = 0;
        out[n++] = 0;
        return n;
    }
    return 0;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (n >= 0 && n < (8 << 20)) ? malloc((size_t)n + 1) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    if (buf) buf[n] = 0;
    fclose(f);
    return buf;
}

static int group_push(mh_group *g, mh_patch *p) {
    mh_patch **np = realloc(g->patches, (g->n + 1) * sizeof *np);
    if (!np) return -1;
    g->patches = np;
    g->patches[g->n++] = p;
    return 0;
}

static void group_revert(modhost *h, mh_group *g) {
    for (size_t i = g->n; i-- > 0;) mh_patch_revert(h, g->patches[i]);
    free(g->patches);
    free(g);
}

bbm_status mh_patch_xml(struct bbm_mod *m, const char *path, const char *want_name, mh_group **out) {
    modhost *h = m->host;
    char full[MH_PATH];
    if (path[0] == '/' || (path[0] && path[1] == ':')) mh_strlcpy(full, path, sizeof full);
    else mh_path_join(full, sizeof full, m->entry->dir, path);
    char *xml = read_file(full);
    if (!xml) { mh_set_error(m, "cannot read %s", full); return BBM_ERR_NOTFOUND; }

    mh_group *g = calloc(1, sizeof *g);
    if (!g) { free(xml); return BBM_ERR_NOMEM; }
    bbm_status rc = BBM_OK;
    int applied_meta = 0;

    for (const char *meta = xml; (meta = find_ci(meta, "<Metadata")); ) {
        const char *tag_end = strchr(meta, '>');
        const char *meta_end = find_ci(meta, "</Metadata>");
        if (!tag_end || !meta_end) break;
        char name[256] = "", appver[32] = "", enabled[16] = "", elf[64] = "";
        attr(meta, tag_end, "Name", name, sizeof name);
        attr(meta, tag_end, "AppVer", appver, sizeof appver);
        attr(meta, tag_end, "isEnabled", enabled, sizeof enabled);
        attr(meta, tag_end, "AppElf", elf, sizeof elf);
        int take = want_name ? !strcmp(name, want_name) : !strcmp(enabled, "true");
        if (appver[0] && strcmp(appver, MH_APP_VER)) take = 0;
        if (elf[0] && strcmp(elf, "eboot.bin")) take = 0;
        if (!take) { meta = meta_end; continue; }
        applied_meta++;

        for (const char *line = tag_end; (line = find_ci(line, "<Line")) && line < meta_end; line++) {
            const char *le = strchr(line, '>');
            if (!le) break;
            char type[32] = "", addr_s[512] = "", value[1024] = "", offset_s[32] = "";
            attr(line, le, "Type", type, sizeof type);
            attr(line, le, "Address", addr_s, sizeof addr_s);
            attr(line, le, "Value", value, sizeof value);
            attr(line, le, "Offset", offset_s, sizeof offset_s);
            uint8_t bytes[1024];
            uintptr_t at;
            size_t n;
            if (!strncmp(type, "mask", 4)) {
                /* GoldHEN mask: Address = pattern, Value = bytes, Offset = +/-N */
                uintptr_t hit;
                if (mh_scan(h, addr_s, 0, 0, &hit) != BBM_OK) {
                    mh_set_error(m, "%s: pattern not found in \"%s\"", full, name);
                    rc = BBM_ERR_NOTFOUND;
                    goto done;
                }
                at = hit + (uintptr_t)(intptr_t)strtol(offset_s[0] ? offset_s : "0", NULL, 0);
                n = parse_hex_bytes(value, bytes, sizeof bytes);
            } else {
                uint64_t ps4 = strtoull(addr_s, NULL, 0);
                at = h->api.ps4_to_host(m, ps4);
                n = encode(type, value, bytes, sizeof bytes);
            }
            if (!n || !at) {
                mh_set_error(m, "%s: unsupported line (Type=%s) in \"%s\"", full, type, name);
                rc = BBM_ERR_UNSUPPORTED;
                goto done;
            }
            mh_patch *p;
            rc = mh_patch_apply(m, at, bytes, n, g, &p);
            if (rc != BBM_OK) goto done;
            if (group_push(g, p)) { mh_patch_revert(h, p); rc = BBM_ERR_NOMEM; goto done; }
        }
        meta = meta_end;
    }
    if (!applied_meta) {
        mh_set_error(m, "%s: no matching patch entry%s%s", full, want_name ? " named " : "",
                     want_name ? want_name : "");
        rc = BBM_ERR_NOTFOUND;
    }

done:
    free(xml);
    if (rc != BBM_OK) { group_revert(h, g); return rc; }
    *out = g;
    return BBM_OK;
}

void mh_group_free(modhost *h, mh_group *g) { group_revert(h, g); }
