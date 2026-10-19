/* SPDX-License-Identifier: GPL-2.0-or-later
 * Patches and hooks. Every change to game code goes through here so it can be
 * recorded, conflict-checked and reverted.
 *
 * Inline hook layout (one executable block allocated within +-2 GB of the
 * target, owned by the HOST, never by the mod):
 *
 *   block+0   FF 25 02 00 00 00   jmp [rip+2]      <- the "gate"
 *   block+6   CC CC                padding
 *   block+8   <8-byte gate slot>   = detour, or trampoline when bypassed
 *   block+16  trampoline: relocated stolen instructions, then
 *             FF 25 00 00 00 00 <abs target+stolen>
 *
 *   target    E9 rel32 -> gate     (or a 14-byte absolute jump)
 *
 * Unloading a mod first flips every gate slot to the trampoline (one atomic
 * 8-byte store), so new calls skip the mod immediately. The host then waits
 * for the mod's in-flight counter to drain, restores the original bytes with
 * other threads suspended, and frees the block a few frames later. A jump
 * into freed mod code is therefore impossible. */
#include "internal.h"

#ifdef BBM_WITH_ZYDIS
#include <Zydis/Zydis.h>
#endif

#define GATE_SIZE 16
#define BLOCK_SIZE 128
#define DEFER_FRAMES 3

int mh_write_code(modhost *h, void *addr, const void *src, size_t n) {
    if (h->cfg.platform.write_code) return h->cfg.platform.write_code(h->cfg.platform.user, addr, src, n);
    return plat_write_code(addr, src, n);
}

int mh_read_safe(modhost *h, const void *addr, void *out, size_t n) {
    if (h->cfg.platform.read_safe) return h->cfg.platform.read_safe(h->cfg.platform.user, addr, out, n);
    return plat_read_safe(addr, out, n);
}

static int suspend(modhost *h) {
    return h->cfg.platform.suspend_world ? h->cfg.platform.suspend_world(h->cfg.platform.user)
                                         : plat_suspend_world();
}

static void resume(modhost *h) {
    if (h->cfg.platform.resume_world) h->cfg.platform.resume_world(h->cfg.platform.user);
    else plat_resume_world();
}

static int thread_in(modhost *h, uintptr_t lo, uintptr_t hi) {
    return h->cfg.platform.any_thread_in ? h->cfg.platform.any_thread_in(h->cfg.platform.user, lo, hi)
                                         : plat_any_thread_in(lo, hi);
}

/* Suspend the world with no thread inside [lo, hi). Retries for ~100 ms. */
static int suspend_clear_of(modhost *h, uintptr_t lo, uintptr_t hi, uintptr_t lo2, uintptr_t hi2) {
    for (int tries = 0; tries < 100; tries++) {
        if (suspend(h) != 0) return -1;
        if (!thread_in(h, lo, hi) && !(hi2 && thread_in(h, lo2, hi2))) return 0;
        resume(h);
        plat_sleep_ms(1);
    }
    return -1;
}

/* 0 when no thread is executing inside [lo, hi). Used before a mod's DLL is
 * closed: the in-flight counter cannot see a thread in a detour's first
 * instructions (before it increments) or last ones (after it decrements). */
int mh_quiesce_range(modhost *h, uintptr_t lo, uintptr_t hi) {
    if (!lo || hi <= lo) return 0;
    if (suspend_clear_of(h, lo, hi, 0, 0) != 0) return -1;
    resume(h);
    return 0;
}

static int overlaps(uintptr_t a, size_t an, uintptr_t b, size_t bn) { return a < b + bn && b < a + an; }

/* group: patches of one XML entry may overlap each other (later lines win, as
 * in GoldHEN/shadPS4); they revert in reverse order, so that is safe. */
static mh_patch *patch_overlap_in(modhost *h, uintptr_t addr, size_t n, const void *group) {
    for (mh_patch *p = h->patches; p; p = p->next_global)
        if (overlaps(p->addr, p->n, addr, n) && !(group && p->group == group)) return p;
    return NULL;
}
static mh_patch *patch_overlap(modhost *h, uintptr_t addr, size_t n) { return patch_overlap_in(h, addr, n, NULL); }

static mh_hook *hook_overlap(modhost *h, uintptr_t addr, size_t n) {
    for (mh_hook *k = h->hooks; k; k = k->next_global)
        if (!k->is_slot && overlaps(k->target, k->stolen, addr, n)) return k;
    return NULL;
}

/* ---- Patches ------------------------------------------------------------ */

bbm_status mh_patch_apply(struct bbm_mod *m, uintptr_t addr, const void *bytes, size_t n,
                          const void *group, mh_patch **out) {
    modhost *h = m->host;
    if (!addr || !bytes || !n || n > (1u << 20)) return BBM_ERR_ARG;
    mh_patch *other = patch_overlap_in(h, addr, n, group);
    if (other || hook_overlap(h, addr, n)) {
        mh_set_error(m, "patch at %p overlaps %s", (void *)addr,
                     other ? (other->owner ? other->owner->entry->m.id : "a patch") : "a hook");
        return BBM_ERR_CONFLICT;
    }
    mh_patch *p = calloc(1, sizeof *p);
    if (!p) return BBM_ERR_NOMEM;
    p->addr = addr;
    p->n = n;
    p->orig = malloc(n);
    p->bytes = malloc(n);
    p->owner = m;
    p->group = group;
    if (!p->orig || !p->bytes) { free(p->orig); free(p->bytes); free(p); return BBM_ERR_NOMEM; }
    memcpy(p->bytes, bytes, n);
    if (mh_read_safe(h, (void *)addr, p->orig, n) != 0) {
        mh_set_error(m, "patch target %p is not mapped", (void *)addr);
        free(p->orig); free(p->bytes); free(p);
        return BBM_ERR_ARG;
    }
    if (mh_write_code(h, (void *)addr, bytes, n) != 0) {
        mh_set_error(m, "could not write %zu bytes at %p", n, (void *)addr);
        free(p->orig); free(p->bytes); free(p);
        return BBM_ERR;
    }
    p->next_global = h->patches;
    h->patches = p;
    *out = p;
    return BBM_OK;
}

void mh_patch_revert(modhost *h, mh_patch *p) {
    mh_write_code(h, (void *)p->addr, p->orig, p->n);
    for (mh_patch **pp = &h->patches; *pp; pp = &(*pp)->next_global) {
        if (*pp == p) { *pp = p->next_global; break; }
    }
    free(p->orig);
    free(p->bytes);
    free(p);
}

/* ---- Hooks -------------------------------------------------------------- */

static void link_hook(modhost *h, mh_hook *k) {
    k->next_global = h->hooks;
    h->hooks = k;
}

static void unlink_hook(modhost *h, mh_hook *k) {
    for (mh_hook **pp = &h->hooks; *pp; pp = &(*pp)->next_global) {
        if (*pp == k) { *pp = k->next_global; break; }
    }
}

bbm_status mh_hook_slot(struct bbm_mod *m, void **slot, void *detour, void **orig, mh_hook **out) {
    modhost *h = m->host;
    if (!slot || !detour) return BBM_ERR_ARG;
    for (mh_hook *k = h->hooks; k; k = k->next_global) {
        if (k->is_slot && k->target == (uintptr_t)slot) {
            mh_set_error(m, "slot %p already hooked by %s", (void *)slot, k->owner->entry->m.id);
            return BBM_ERR_CONFLICT;
        }
    }
    mh_hook *k = calloc(1, sizeof *k);
    if (!k) return BBM_ERR_NOMEM;
    k->is_slot = 1;
    k->target = (uintptr_t)slot;
    k->detour = detour;
    k->owner = m;
    if (mh_read_safe(h, slot, &k->slot_original, sizeof k->slot_original) != 0) {
        free(k);
        mh_set_error(m, "slot %p is not mapped", (void *)slot);
        return BBM_ERR_ARG;
    }
    k->trampoline = k->slot_original;
    if (orig) *orig = (void *)k->slot_original;
    uintptr_t v = (uintptr_t)detour;
    if (mh_write_code(h, slot, &v, sizeof v) != 0) { free(k); return BBM_ERR; }
    link_hook(h, k);
    *out = k;
    return BBM_OK;
}

void mh_hook_bypass(mh_hook *k, int bypass) {
    uintptr_t v = bypass ? k->trampoline : (uintptr_t)k->detour;
    if (k->is_slot) {
        /* Slot pages may be read-only; write through the host. */
        mh_write_code(k->owner->host, (void *)k->target, &v, sizeof v);
    } else {
        __atomic_store_n(k->gate_slot, v, __ATOMIC_RELEASE);
    }
}

static void defer_free(modhost *h, void *p, size_t n) {
    if (h->ndeferred >= sizeof h->deferred / sizeof h->deferred[0]) {
        mh_deferred_flush(h);
    }
    h->deferred[h->ndeferred].p = p;
    h->deferred[h->ndeferred].n = n;
    h->deferred[h->ndeferred].free_at_frame = h->frame_no + DEFER_FRAMES;
    h->ndeferred++;
}

void mh_deferred_tick(modhost *h) {
    size_t w = 0;
    for (size_t i = 0; i < h->ndeferred; i++) {
        if (h->deferred[i].free_at_frame <= h->frame_no) plat_free_exec(h->deferred[i].p, h->deferred[i].n);
        else h->deferred[w++] = h->deferred[i];
    }
    h->ndeferred = w;
}

void mh_deferred_flush(modhost *h) {
    for (size_t i = 0; i < h->ndeferred; i++) plat_free_exec(h->deferred[i].p, h->deferred[i].n);
    h->ndeferred = 0;
}

void mh_hook_remove(modhost *h, mh_hook *k) {
    if (k->is_slot) {
        mh_write_code(h, (void *)k->target, &k->slot_original, sizeof k->slot_original);
        unlink_hook(h, k);
        free(k);
        return;
    }
    mh_hook_bypass(k, 1);
    uintptr_t blo = (uintptr_t)k->block, bhi = blo + k->block_size;
    if (suspend_clear_of(h, k->target, k->target + k->stolen, 0, 0) == 0) {
        mh_write_code(h, (void *)k->target, k->orig, k->stolen);
        resume(h);
    } else {
        /* A thread sits in the patched bytes for 100 ms: leave the gate in
         * bypass (still correct, just one extra jump) and report it. */
        mh_log(h, k->owner->entry->m.id, BBM_LOG_WARN,
               "hook at %p left in bypass: a thread stayed inside it", (void *)k->target);
        unlink_hook(h, k);
        free(k);
        return;
    }
    (void)bhi;
    defer_free(h, k->block, k->block_size);
    unlink_hook(h, k);
    free(k);
}

#ifdef BBM_WITH_ZYDIS

static void put_abs_jmp(uint8_t *p, uintptr_t to) {
    p[0] = 0xFF;
    p[1] = 0x25;
    memset(p + 2, 0, 4);
    memcpy(p + 6, &to, 8);
}

static int fits32(int64_t v) { return v >= INT32_MIN && v <= INT32_MAX; }

bbm_status mh_hook_inline(struct bbm_mod *m, uintptr_t target, void *detour, void **tramp,
                          mh_hook **out) {
    modhost *h = m->host;
    if (!target || !detour) return BBM_ERR_ARG;

    uint8_t code[64];
    if (mh_read_safe(h, (void *)target, code, sizeof code) != 0) {
        mh_set_error(m, "hook target %p is not mapped", (void *)target);
        return BBM_ERR_ARG;
    }

    uint8_t *block = plat_alloc_exec_near(target, BLOCK_SIZE);
    int near = block != NULL;
    if (!block) block = plat_alloc_exec_near(0, BLOCK_SIZE);
    if (!block) return BBM_ERR_NOMEM;
    uintptr_t gate = (uintptr_t)block;
    near = near && fits32((int64_t)gate - (int64_t)(target + 5));
    size_t need = near ? 5 : 14;

    ZydisDecoder dec;
    ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    uint8_t *tr = block + GATE_SIZE;
    size_t stolen = 0, out_len = 0;
    while (stolen < need) {
        ZydisDecodedInstruction ins;
        if (ZYAN_FAILED(ZydisDecoderDecodeInstruction(&dec, NULL, code + stolen, sizeof code - stolen, &ins))) {
            mh_set_error(m, "cannot decode instruction at %p", (void *)(target + stolen));
            goto fail;
        }
        uint8_t buf[16];
        memcpy(buf, code + stolen, ins.length);
        uintptr_t old_ip = target + stolen, new_ip = (uintptr_t)tr + out_len;
        if (ins.attributes & ZYDIS_ATTRIB_IS_RELATIVE) {
            int done = 0;
            if (ins.raw.disp.size == 32) {
                int64_t abs = (int64_t)old_ip + ins.length + ins.raw.disp.value;
                int64_t nd = abs - (int64_t)(new_ip + ins.length);
                if (!fits32(nd)) { mh_set_error(m, "RIP-relative operand out of range after relocation"); goto fail; }
                int32_t v = (int32_t)nd;
                memcpy(buf + ins.raw.disp.offset, &v, 4);
                done = 1;
            }
            for (int i = 0; i < 2 && !done; i++) {
                if (!ins.raw.imm[i].is_relative) continue;
                if (ins.raw.imm[i].size != 32) {
                    mh_set_error(m, "short branch in the first %zu bytes of %p is not supported", need, (void *)target);
                    goto fail;
                }
                int64_t abs = (int64_t)old_ip + ins.length + ins.raw.imm[i].value.s;
                int64_t nd = abs - (int64_t)(new_ip + ins.length);
                if (!fits32(nd)) { mh_set_error(m, "branch out of range after relocation"); goto fail; }
                int32_t v = (int32_t)nd;
                memcpy(buf + ins.raw.imm[i].offset, &v, 4);
                done = 1;
            }
            if (!done) { mh_set_error(m, "unsupported relative instruction at %p", (void *)old_ip); goto fail; }
        }
        if (out_len + ins.length + 14 > BLOCK_SIZE - GATE_SIZE) { mh_set_error(m, "trampoline too large"); goto fail; }
        memcpy(tr + out_len, buf, ins.length);
        out_len += ins.length;
        stolen += ins.length;
        int ends = ins.mnemonic == ZYDIS_MNEMONIC_RET || ins.mnemonic == ZYDIS_MNEMONIC_INT3 ||
                   (ins.mnemonic == ZYDIS_MNEMONIC_JMP);
        if (ends && stolen < need) {
            mh_set_error(m, "function at %p is shorter than the %zu-byte hook jump", (void *)target, need);
            goto fail;
        }
    }

    if (patch_overlap(h, target, stolen) || hook_overlap(h, target, stolen)) {
        mh_set_error(m, "hook at %p overlaps an existing patch or hook", (void *)target);
        plat_free_exec(block, BLOCK_SIZE);
        return BBM_ERR_CONFLICT;
    }

    put_abs_jmp(tr + out_len, target + stolen);

    /* Gate: jmp [rip+2] ; int3 int3 ; slot */
    static const uint8_t gate_code[8] = {0xFF, 0x25, 0x02, 0x00, 0x00, 0x00, 0xCC, 0xCC};
    memcpy(block, gate_code, sizeof gate_code);
    volatile uintptr_t *slot = (volatile uintptr_t *)(block + 8);
    *slot = (uintptr_t)detour;

    uint8_t patch[14];
    if (near) {
        int32_t rel = (int32_t)((int64_t)gate - (int64_t)(target + 5));
        patch[0] = 0xE9;
        memcpy(patch + 1, &rel, 4);
    } else {
        put_abs_jmp(patch, gate);
    }

    mh_hook *k = calloc(1, sizeof *k);
    if (!k) goto fail;
    k->target = target;
    k->stolen = stolen;
    memcpy(k->orig, code, stolen);
    k->block = block;
    k->block_size = BLOCK_SIZE;
    k->gate_slot = slot;
    k->trampoline = (uintptr_t)tr;
    k->detour = detour;
    k->owner = m;

    uint8_t full[32];
    memcpy(full, patch, need);
    for (size_t i = need; i < stolen; i++) full[i] = 0xCC; /* never executed */

    /* Publish the trampoline BEFORE the target is patched: another thread may
     * enter the detour the instant the jump is written, and the detour calls
     * the original through this pointer. */
    if (tramp) *tramp = (void *)k->trampoline;
    if (suspend_clear_of(h, target, target + stolen, 0, 0) != 0) {
        if (tramp) *tramp = NULL;
        free(k);
        mh_set_error(m, "a thread stayed inside %p while hooking", (void *)target);
        plat_free_exec(block, BLOCK_SIZE);
        return BBM_ERR_STATE;
    }
    int wrc = mh_write_code(h, (void *)target, full, stolen);
    resume(h);
    if (wrc != 0) {
        if (tramp) *tramp = NULL;
        free(k);
        plat_free_exec(block, BLOCK_SIZE);
        return BBM_ERR;
    }
    link_hook(h, k);
    *out = k;
    return BBM_OK;

fail:
    plat_free_exec(block, BLOCK_SIZE);
    return BBM_ERR_UNSUPPORTED;
}

#else

bbm_status mh_hook_inline(struct bbm_mod *m, uintptr_t target, void *detour, void **tramp,
                          mh_hook **out) {
    (void)target; (void)detour; (void)tramp; (void)out;
    mh_set_error(m, "inline hooks need a host built with Zydis (BBM_WITH_ZYDIS)");
    return BBM_ERR_UNSUPPORTED;
}

#endif

/* ---- Pattern scan --------------------------------------------------------- */

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bbm_status mh_scan(modhost *h, const char *pattern, uintptr_t start, size_t len, uintptr_t *out) {
    uint8_t bytes[256], mask[256];
    size_t n = 0;
    for (const char *p = pattern; *p;) {
        if (*p == ' ') { p++; continue; }
        if (n >= sizeof bytes) return BBM_ERR_ARG;
        if (*p == '?') {
            mask[n] = 0;
            bytes[n++] = 0;
            p++;
            if (*p == '?') p++;
            continue;
        }
        int a = hexval(p[0]), b = p[1] ? hexval(p[1]) : -1;
        if (a < 0 || b < 0) return BBM_ERR_ARG;
        bytes[n] = (uint8_t)(a * 16 + b);
        mask[n++] = 1;
        p += 2;
    }
    if (!n) return BBM_ERR_ARG;
    uintptr_t base = h->cfg.eboot_base, end = base + h->cfg.eboot_size;
    if (!start) { start = base; len = h->cfg.eboot_size; }
    /* start inside the image with len 0 means "from start to the end of the image"
     * (lets a caller enumerate every match: scan, then scan again past the hit). */
    else if (!len && start >= base && start < end) len = end - start;
    if (!start || len < n) return BBM_ERR_ARG;
    const uint8_t *mem = (const uint8_t *)start;
    for (size_t i = 0; i + n <= len; i++) {
        size_t j = 0;
        while (j < n && (!mask[j] || mem[i + j] == bytes[j])) j++;
        if (j == n) { *out = start + i; return BBM_OK; }
    }
    return BBM_ERR_NOTFOUND;
}
