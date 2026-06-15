/*
 * Copyright (c) 2026 Benjamin H
 *
 * SPDX-License-Identifier: MIT
 *
 * dm_events — notifications + the read-only query projection.
 *
 * Raises zmk_dynamic_macro_state_changed and implements the dm_get_* widget API
 * as a projection over the single instance's slot_store + machine. Widgets ask by
 * slot index, not by device, so the single-instance assumption is resolved HERE,
 * in one place, anchored to the BUILD_ASSERT(<=1) at the behavior-shell wiring
 * site. The string/number projection delegates to the pure dm_query; this file is
 * only the Zephyr edge (event raising, instance resolution, counts over the
 * store).
 *
 * Compiled only under EVENTS.
 */

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <zmk-behavior-dynamic-macros/dm_kconfig.h>
#include <zmk-behavior-dynamic-macros/dm_notify.h>
#include <zmk-behavior-dynamic-macros/dm_query.h>
#include <zmk-behavior-dynamic-macros/dm_shell.h>
#include <zmk-behavior-dynamic-macros/events/dynamic_macro_state_changed.h>
#if DM_TYPING_ENABLED
#include <zmk-behavior-dynamic-macros/dm_feedback_pump.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_EVENTS)

/* The machine raises a dm_notify_code (from dm_notify.h, the one owner); this is
 * the single place it is turned into the public widget event enum. The map is
 * explicit because the two orderings deliberately diverge past PREVIEW_READY — it
 * is real translation, not a duplicate of the code list. No `default`, so a new
 * dm_notify_code that misses a case is flagged by -Wswitch. */
static enum zmk_dynamic_macro_event_type map_event(dm_notify_code machine_event) {
    switch (machine_event) {
    case DM_EVT_RECORDING_STARTED:  return ZMK_DYNAMIC_MACRO_RECORDING_STARTED;
    case DM_EVT_RECORDING_STOPPED:  return ZMK_DYNAMIC_MACRO_RECORDING_STOPPED;
    case DM_EVT_SAVED:              return ZMK_DYNAMIC_MACRO_SAVED;
    case DM_EVT_DELETED:            return ZMK_DYNAMIC_MACRO_DELETED;
    case DM_EVT_MOVED:              return ZMK_DYNAMIC_MACRO_MOVED;
    case DM_EVT_PLAY_STARTED:       return ZMK_DYNAMIC_MACRO_PLAY_STARTED;
    case DM_EVT_PLAY_FINISHED:      return ZMK_DYNAMIC_MACRO_PLAY_FINISHED;
    case DM_EVT_PREVIEW_READY:      return ZMK_DYNAMIC_MACRO_PREVIEW_READY;
    case DM_EVT_MOVE_PROMPT:        return ZMK_DYNAMIC_MACRO_MOVE_PROMPT;
    case DM_EVT_MOVE_SRC_SELECTED:  return ZMK_DYNAMIC_MACRO_MOVE_SRC_SELECTED;
    case DM_EVT_MOVE_CANCELLED:     return ZMK_DYNAMIC_MACRO_MOVE_CANCELLED;
    case DM_EVT_DELETE_PROMPT:      return ZMK_DYNAMIC_MACRO_DELETE_PROMPT;
    case DM_EVT_PREVIEW_PROMPT:     return ZMK_DYNAMIC_MACRO_PREVIEW_PROMPT;
    case DM_EVT_CHAIN_INSERTED:     return ZMK_DYNAMIC_MACRO_CHAIN_INSERTED;
    case DM_EVT_PENDING_CANCELLED:  return ZMK_DYNAMIC_MACRO_PENDING_CANCELLED;
    case DM_EVT_SETTINGS_CHANGED:   return ZMK_DYNAMIC_MACRO_SETTINGS_CHANGED;
    case DM_EVT_ERROR_NO_RECORDING: return ZMK_DYNAMIC_MACRO_ERROR_NO_RECORDING;
    case DM_EVT_ERROR_SLOT_EMPTY:   return ZMK_DYNAMIC_MACRO_ERROR_SLOT_EMPTY;
    case DM_EVT_ERROR_SLOT_OCCUPIED:return ZMK_DYNAMIC_MACRO_ERROR_SLOT_OCCUPIED;
    case DM_EVT_ERROR_OVERFLOW:     return ZMK_DYNAMIC_MACRO_ERROR_OVERFLOW;
    case DM_EVT_ERROR_CHAIN_FULL:   return ZMK_DYNAMIC_MACRO_ERROR_CHAIN_FULL;
    case DM_EVT_ERROR_SAVE_FAILED:  return ZMK_DYNAMIC_MACRO_ERROR_SAVE_FAILED;
    case DM_EVT_ERROR_DELETE_FAILED:return ZMK_DYNAMIC_MACRO_ERROR_DELETE_FAILED;
    case DM_EVT_ERROR_SAVE_QUEUE_FULL:   return ZMK_DYNAMIC_MACRO_ERROR_SAVE_QUEUE_FULL;
    case DM_EVT_ERROR_DELETE_QUEUE_FULL: return ZMK_DYNAMIC_MACRO_ERROR_DELETE_QUEUE_FULL;
    case DM_EVT__COUNT:             break; /* not a real code */
    }
    return ZMK_DYNAMIC_MACRO_ERROR_NO_RECORDING;
}

/* The machine's effective (settled/destination) state, projected to the public
 * enum. Reads through TYPING_* to the parked return-state, so .state is the mode
 * the machine has settled into — and dm_get_state() agrees with every event. */
static enum zmk_dynamic_macro_state map_state(struct dm_inst *inst) {
    switch (dm_machine_effective_state(&inst->machine)) {
    case DM_STATE_RECORDING:       return ZMK_DYNAMIC_MACRO_STATE_RECORDING;
    case DM_STATE_PLAYING:         return ZMK_DYNAMIC_MACRO_STATE_PLAYING;
    case DM_STATE_PENDING_ASSIGN:  return ZMK_DYNAMIC_MACRO_STATE_ASSIGN_PENDING;
    case DM_STATE_MOVE_PENDING:    return ZMK_DYNAMIC_MACRO_STATE_MOVE_PENDING;
    case DM_STATE_DELETE_PENDING:  return ZMK_DYNAMIC_MACRO_STATE_DELETE_PENDING;
    case DM_STATE_PREVIEW_PENDING: return ZMK_DYNAMIC_MACRO_STATE_PREVIEW_PENDING;
    default:                       return ZMK_DYNAMIC_MACRO_STATE_IDLE;
    }
}

void dm_events_raise(struct dm_inst *inst, int machine_event, int slot, int slot2) {
    enum zmk_dynamic_macro_event_type ev = map_event(machine_event);
    enum zmk_dynamic_macro_state st = map_state(inst);

    /* Fixed log shape: tooling strips "dm_event: " and captures
     * "type=N slot=N slot2=N state=N". */
    LOG_DBG("dm_event: type=%d slot=%d slot2=%d state=%d", (int)ev, slot, slot2, (int)st);

    raise_zmk_dynamic_macro_state_changed((struct zmk_dynamic_macro_state_changed){
        .state = st,
        .event = ev,
        .slot = slot,
        .slot2 = slot2,
        .slot_is_nvs  = slot  >= 0 ? slot_is_nvs(slot)  : false,
        .slot2_is_nvs = slot2 >= 0 ? slot_is_nvs(slot2) : false,
    });
}

/* ---- the dm_get_* widget query API ---------------------------------------- */

static dm_render_slot_view view_for(slot_store *store, int slot_idx) {
    return slot_store_get(store, slot_idx); /* {0, NULL} when empty */
}

bool dm_is_slot_empty(int slot_idx) {
    struct dm_inst *inst = dm_shell_instance();
    if (!inst || slot_idx < 0 || slot_idx >= MAX_SLOTS) {
        return true;
    }
    return slot_store_is_empty(&inst->store, slot_idx);
}

const struct dm_event *dm_get_slot_events(int slot_idx, uint32_t *count) {
    if (slot_idx < 0 || slot_idx >= MAX_SLOTS || !count) {
        if (count) {
            *count = 0;
        }
        return NULL;
    }
    struct dm_inst *inst = dm_shell_instance();
    struct dm_slot_view v = inst ? slot_store_get(&inst->store, slot_idx)
                                 : (struct dm_slot_view){0, NULL};
    if (v.events == NULL) {
        *count = 0;
        return NULL;
    }
    *count = v.event_count;
    return v.events;
}

int dm_get_preview_string(int slot_idx, char *buf, size_t len) {
    if (!buf || len == 0) {
        return 0;
    }
    buf[0] = '\0';

    struct dm_inst *inst = dm_shell_instance();
    if (!inst || slot_idx < 0 || slot_idx >= MAX_SLOTS) {
        return 0;
    }
    dm_render_slot_view view = view_for(&inst->store, slot_idx);
    if (view.events == NULL) {
        return 0;
    }
    return dm_query_preview_string(&view, DM_LOCALE, view.event_count,
                                   DM_TYPING_ENABLED, buf, len);
}

int dm_get_used_nvs_slots(void) {
    struct dm_inst *inst = dm_shell_instance();
    return inst ? slot_store_count(&inst->store, DM_SLOT_CLASS_NVS) : 0;
}

int dm_get_used_ram_slots(void) {
    struct dm_inst *inst = dm_shell_instance();
    return inst ? slot_store_count(&inst->store, DM_SLOT_CLASS_RAM) : 0;
}

int dm_get_total_nvs_slots(void) {
    return NVS_SLOTS;
}

int dm_get_total_ram_slots(void) {
    return RAM_SLOTS;
}

enum zmk_dynamic_macro_state dm_get_state(void) {
    struct dm_inst *inst = dm_shell_instance();
    return inst ? map_state(inst) : ZMK_DYNAMIC_MACRO_STATE_IDLE;
}

/* ---- settings (knob) queries ---------------------------------------------- */

/* The display reads these on a SETTINGS_CHANGED event. They mirror the pump's
 * runtime knobs; when feedback is compiled out there is no runtime knob, so the
 * defensive default is returned (SETTINGS_CHANGED only fires under typing). */
int dm_get_feedback_level(void) {
#if DM_TYPING_ENABLED
    struct dm_inst *inst = dm_shell_instance();
    return inst ? (int)dm_feedback_level(&inst->feedback) : 0;
#else
    return 0;
#endif
}

int dm_get_feedback_style(void) {
#if DM_TYPING_ENABLED
    struct dm_inst *inst = dm_shell_instance();
    return inst ? (int)dm_feedback_style(&inst->feedback) : 0;
#else
    return 0;
#endif
}

bool dm_get_erase_enabled(void) {
#if DM_TYPING_ENABLED
    struct dm_inst *inst = dm_shell_instance();
    return inst ? dm_feedback_erase(&inst->feedback) : false;
#else
    return false;
#endif
}

uint32_t dm_get_recording_event_count(void) {
    struct dm_inst *inst = dm_shell_instance();
    if (!inst || dm_machine_state(&inst->machine) != DM_STATE_RECORDING) {
        return 0;
    }
    return slot_store_draft_count(&inst->store);
}

#endif /* CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_EVENTS */
