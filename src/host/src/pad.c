/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"
#include <math.h>
static const uint8_t off[11] = {5};
static int captured(modhost *h) {
    return h->cfg.pad.captured && h->cfg.pad.captured(h->cfg.pad.user);
}
static bbm_status info_locked(modhost *h, bbm_pad_info *out) {
    memset(out, 0, sizeof *out);
    if (!h->cfg.pad.info || h->cfg.pad.info(h->cfg.pad.user, out)) return BBM_ERR_UNSUPPORTED;
    if (captured(h)) out->features |= BBM_PAD_CAPTURED;
    if (h->pad_failed) out->features |= BBM_PAD_SEND_FAILED;
    return BBM_OK;
}
static bbm_status info(bbm_handle m, bbm_pad_info *out) {
    if (!m || !out) return BBM_ERR_ARG;
    modhost *h = m->host;
    plat_mutex_lock(h->run_lock);
    bbm_status rc = info_locked(h, out);
    plat_mutex_unlock(h->run_lock);
    return rc;
}
static bbm_status trigger(bbm_handle m, uint32_t which, const uint8_t effect[11]) {
    if (!m || which > 1 || !effect) return BBM_ERR_ARG;
    modhost *h = m->host;
    plat_mutex_lock(h->run_lock);
    bbm_pad_info p;
    bbm_status rc = BBM_OK;
    if (m->bypassed || m->faulted) rc = BBM_ERR_STATE;
    else if (info_locked(h, &p) || !(p.features & BBM_PAD_HAS_TRIGGERS) || !h->cfg.pad.send_triggers) rc = BBM_ERR_UNSUPPORTED;
    else if (h->trigger_owner[which] && h->trigger_owner[which] != m) rc = BBM_ERR_CONFLICT;
    else { h->trigger_owner[which] = m; memcpy(h->trigger_desired[which], effect, 11); }
    plat_mutex_unlock(h->run_lock);
    return rc;
}
static bbm_status release(bbm_handle m, uint32_t which) {
    if (!m || which > 1) return BBM_ERR_ARG;
    modhost *h = m->host;
    plat_mutex_lock(h->run_lock);
    bbm_status rc = BBM_OK;
    if (h->trigger_owner[which] && h->trigger_owner[which] != m) rc = BBM_ERR_CONFLICT;
    else if (h->trigger_owner[which]) {
        h->trigger_owner[which] = NULL;
        h->pad_urgent = 1;
        mh_pad_tick(h, plat_now_ms());
    }
    plat_mutex_unlock(h->run_lock);
    return rc;
}
static bbm_status lightbar(bbm_handle m, uint8_t r, uint8_t g, uint8_t b) {
    if (!m) return BBM_ERR_ARG;
    modhost *h = m->host;
    plat_mutex_lock(h->run_lock);
    bbm_pad_info p;
    bbm_status rc = BBM_OK;
    if (m->bypassed || m->faulted) rc = BBM_ERR_STATE;
    else if (info_locked(h, &p) || !(p.features & BBM_PAD_HAS_LIGHTBAR) || !h->cfg.pad.lightbar) rc = BBM_ERR_UNSUPPORTED;
    else if (h->led_owner && h->led_owner != m) rc = BBM_ERR_CONFLICT;
    else { h->led_owner = m; h->led[0] = r; h->led[1] = g; h->led[2] = b; }
    plat_mutex_unlock(h->run_lock);
    return rc;
}
static bbm_status haptics(bbm_handle m, const float *pcm, uint32_t frames) {
    if (!m || frames > 12000 || (frames && !pcm)) return BBM_ERR_ARG;
    for (uint32_t i=0; i<frames*2; ++i)
        if (!isfinite(pcm[i]) || fabsf(pcm[i]) > 0.5f) return BBM_ERR_ARG;
    modhost *h=m->host;
    plat_mutex_lock(h->run_lock);
    bbm_status rc=BBM_OK;
    bbm_pad_info p;
    if (h->haptics_owner && h->haptics_owner != m) rc=BBM_ERR_CONFLICT;
    else if (!frames) {
        if (h->haptics_owner && h->cfg.pad.haptics) h->cfg.pad.haptics(h->cfg.pad.user,NULL,0);
        h->haptics_owner=NULL;
    } else if (m->bypassed || m->faulted || captured(h)) rc=BBM_ERR_STATE;
    else if (!h->cfg.pad.haptics || info_locked(h,&p) || p.kind!=BBM_PAD_DUALSENSE || p.connection!=BBM_PAD_USB)
        rc=BBM_ERR_UNSUPPORTED;
    else if (h->cfg.pad.haptics(h->cfg.pad.user,pcm,frames)) rc=BBM_ERR_UNSUPPORTED;
    else h->haptics_owner=m;
    plat_mutex_unlock(h->run_lock);
    return rc;
}
static void stop_haptics(modhost *h) {
    if (h->haptics_owner && h->cfg.pad.haptics) h->cfg.pad.haptics(h->cfg.pad.user,NULL,0);
    h->haptics_owner=NULL;
}
void mh_pad_release_owner(struct bbm_mod *m) {
    modhost *h = m->host;
    if (h->haptics_owner == m) stop_haptics(h);
    for (int i = 0; i < 2; ++i) if (h->trigger_owner[i] == m) {
        h->trigger_owner[i] = NULL; h->pad_urgent = 1;
    }
    if (h->led_owner == m) { h->led_owner = NULL; h->pad_urgent = 1; }
    mh_pad_tick(h, plat_now_ms());
}
/* run_lock is held. Safety resets bypass the 30 Hz effect limit. Failed sends
 * retain the dirty state and retry after 1 s, with one warning per outage. */
void mh_pad_tick(modhost *h, uint64_t now) {
    bbm_pad_info p;
    if (info_locked(h, &p) || !p.device_id) {
        stop_haptics(h);
        h->pad_valid = h->led_valid = 0; h->pad_device = 0; return;
    }
    if (h->pad_device != p.device_id) {
        if (h->pad_device) stop_haptics(h);
        h->pad_device = p.device_id; h->pad_valid = h->led_valid = 0; h->pad_next_ms = 0;
    }
    int cap = captured(h);
    if (cap) stop_haptics(h);
    if (cap && !h->pad_capture) h->pad_urgent = 1;
    h->pad_capture = cap;
    uint8_t desired[2][11], led[3] = {0};
    for (int i = 0; i < 2; ++i) {
        memcpy(desired[i], !cap && h->trigger_owner[i] ? h->trigger_desired[i] : off, 11);
    }
    if (h->led_owner) for (int i = 0; i < 3; ++i) led[i] = cap ? h->led[i] / 4 : h->led[i];
    if (!h->pad_urgent && now < h->pad_next_ms) return;
    h->pad_urgent = 0;
    int failed = 0, sent = 0;
    if ((p.features & BBM_PAD_HAS_TRIGGERS) && h->cfg.pad.send_triggers &&
        (!h->pad_valid || memcmp(desired, h->trigger_sent, sizeof desired))) {
        sent = 1;
        if (h->cfg.pad.send_triggers(h->cfg.pad.user, desired[1], desired[0])) failed = 1;
        else { memcpy(h->trigger_sent, desired, sizeof desired); h->pad_valid = 1; }
    }
    if ((h->led_owner || h->led_valid) && (p.features & BBM_PAD_HAS_LIGHTBAR) && h->cfg.pad.lightbar &&
        (!h->led_valid || memcmp(led, h->led_sent, 3))) {
        sent = 1;
        if (h->cfg.pad.lightbar(h->cfg.pad.user, led[0], led[1], led[2])) failed = 1;
        else { memcpy(h->led_sent, led, 3); h->led_valid = 1; }
    }
    if (failed && !h->pad_failed) mh_log(h, NULL, BBM_LOG_WARN, "Pad effects send failed; retrying at 1 Hz");
    if (sent) { h->pad_failed = failed; h->pad_next_ms = now + (failed ? 1000 : 34); }
}
void mh_pad_fill(bbm_host *api) {
    api->pad_haptics = haptics;
    api->pad_info = info; api->pad_trigger = trigger;
    api->pad_release = release; api->pad_lightbar = lightbar;
}
