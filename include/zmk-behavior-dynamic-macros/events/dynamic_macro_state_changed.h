/*
 * Copyright (c) 2026 Benjamin H
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>
#include <zmk-behavior-dynamic-macros/dm_event.h>

enum zmk_dynamic_macro_state {
    ZMK_DYNAMIC_MACRO_STATE_IDLE = 0,
    ZMK_DYNAMIC_MACRO_STATE_RECORDING = 1,
    ZMK_DYNAMIC_MACRO_STATE_PLAYING = 2,
    /* appended — effective pending modes */
    ZMK_DYNAMIC_MACRO_STATE_ASSIGN_PENDING = 3,  /* recording stopped, awaiting slot */
    ZMK_DYNAMIC_MACRO_STATE_MOVE_PENDING = 4,    /* move mode: awaiting source or dest */
    ZMK_DYNAMIC_MACRO_STATE_DELETE_PENDING = 5,  /* delete mode: awaiting slot */
    ZMK_DYNAMIC_MACRO_STATE_PREVIEW_PENDING = 6, /* preview mode: awaiting slot */
};

enum zmk_dynamic_macro_event_type {
    ZMK_DYNAMIC_MACRO_RECORDING_STARTED,
    ZMK_DYNAMIC_MACRO_RECORDING_STOPPED,
    ZMK_DYNAMIC_MACRO_SAVED,
    ZMK_DYNAMIC_MACRO_DELETED,
    ZMK_DYNAMIC_MACRO_MOVED,
    ZMK_DYNAMIC_MACRO_PLAY_STARTED,
    ZMK_DYNAMIC_MACRO_PLAY_FINISHED,
    ZMK_DYNAMIC_MACRO_PREVIEW_READY,

    ZMK_DYNAMIC_MACRO_ERROR_OVERFLOW,
    ZMK_DYNAMIC_MACRO_ERROR_SAVE_FAILED,
    ZMK_DYNAMIC_MACRO_ERROR_DELETE_FAILED,
    /* deprecated: superseded by the SAVE/DELETE queue-full split below; retained
     * for source compatibility (ordinal 11) but no longer raised. */
    ZMK_DYNAMIC_MACRO_ERROR_QUEUE_FULL,
    ZMK_DYNAMIC_MACRO_ERROR_SLOT_EMPTY,
    ZMK_DYNAMIC_MACRO_ERROR_NO_RECORDING,

    /* mode / transition (appended — ordinals 14..) */
    ZMK_DYNAMIC_MACRO_MOVE_PROMPT,            /* entered move mode */
    ZMK_DYNAMIC_MACRO_MOVE_SRC_SELECTED,      /* slot = source */
    ZMK_DYNAMIC_MACRO_MOVE_CANCELLED,         /* same-slot cancel */
    ZMK_DYNAMIC_MACRO_DELETE_PROMPT,          /* entered delete mode */
    ZMK_DYNAMIC_MACRO_PREVIEW_PROMPT,         /* entered preview mode */
    ZMK_DYNAMIC_MACRO_CHAIN_INSERTED,         /* slot chained into the draft */
    ZMK_DYNAMIC_MACRO_PENDING_CANCELLED,      /* a *_PENDING mode timed out -> IDLE */
    ZMK_DYNAMIC_MACRO_SETTINGS_CHANGED,       /* level / style / erase changed */
    /* errors (appended) */
    ZMK_DYNAMIC_MACRO_ERROR_SLOT_OCCUPIED,    /* assign/move target not empty */
    ZMK_DYNAMIC_MACRO_ERROR_CHAIN_FULL,       /* chain would exceed MAX_EVENTS */
    ZMK_DYNAMIC_MACRO_ERROR_SAVE_QUEUE_FULL,  /* split from ERROR_QUEUE_FULL */
    ZMK_DYNAMIC_MACRO_ERROR_DELETE_QUEUE_FULL,/* split from ERROR_QUEUE_FULL */
};

struct zmk_dynamic_macro_state_changed {
    enum zmk_dynamic_macro_state state;
    enum zmk_dynamic_macro_event_type event;
    int slot;          /* primary slot, or -1 */
    int slot2;         /* secondary slot (move source for MOVED), or -1 */
    bool slot_is_nvs;  /* valid when slot  >= 0 */
    bool slot2_is_nvs; /* valid when slot2 >= 0 */
};

ZMK_EVENT_DECLARE(zmk_dynamic_macro_state_changed);

int dm_get_preview_string(int slot_idx, char *buf, size_t len);
/* Returned pointer is valid only until the next macro operation on this slot. */
const struct dm_event *dm_get_slot_events(int slot_idx, uint32_t *count);
bool dm_is_slot_empty(int slot_idx);
int dm_get_used_nvs_slots(void);
int dm_get_used_ram_slots(void);
int dm_get_total_nvs_slots(void);
int dm_get_total_ram_slots(void);
enum zmk_dynamic_macro_state dm_get_state(void);
uint32_t dm_get_recording_event_count(void);

/* Current runtime feedback knobs — the display reads these on a SETTINGS_CHANGED
 * event (the value is not encoded in the event). Return defaults when feedback is
 * compiled out. */
int  dm_get_feedback_level(void);  /* DM_FB_LEVEL_* */
int  dm_get_feedback_style(void);  /* 0 = full, 1 = arrow */
bool dm_get_erase_enabled(void);   /* auto-erase on/off */
