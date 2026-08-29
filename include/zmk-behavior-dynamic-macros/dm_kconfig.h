/*
 * Copyright (c) 2026 Benjamin H
 *
 * SPDX-License-Identifier: MIT
 *
 * dm_kconfig — the Kconfig-derived compile-time constants the Zephyr shells need.
 *
 * The shells need the firmware's compile-time knobs (locale, feedback defaults,
 * status detail, slot geometry, slot_is_nvs). This header carries only the scalar
 * Kconfig values and the one small inline; the state enum lives in dm_machine.h
 * and the locale enum in dm_render.h, so it composes cleanly with both.
 */

#ifndef DM_KCONFIG_H
#define DM_KCONFIG_H

#include <stdbool.h>

/* Slot geometry: MAX_EVENTS / NVS_SLOTS / RAM_SLOTS / MAX_SLOTS / SLOT_CAPACITY.
 * dm_config.h sources these from Kconfig under __ZEPHYR__ (host defaults
 * otherwise) — the single source of truth, shared with the pure cores. */
#include <zmk-behavior-dynamic-macros/dm_config.h>

/* Inter-event typing delay (ms), shared by playback, feedback, and erase. The
 * single source of truth for the constant: the shells (behavior_dynamic_macro.c,
 * dm_feedback_pump.c) consume DM_TAP_DELAY rather than re-#defining it, and
 * PLAYBACK_BUF_SIZE below is derived from it. Host build falls back to the firmware
 * default so the pure ring/verdict headers compute the same size off-Zephyr. */
#ifdef CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_TAP_DELAY
#define DM_TAP_DELAY CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_TAP_DELAY
#else
#define DM_TAP_DELAY 20
#endif

/*
 * Playback buffer: capture the user's keypresses during macro playback and drain
 * them after (see docs/playback-buffer-plan.md). PLAYBACK_BUFFER_ENABLED gates the
 * whole feature; when 0 the ring/branch compile out and playback reverts to live
 * interleave.
 *
 * PLAYBACK_BUF_SIZE holds ONLY captured foreign keys (never the macro — that
 * replays in place from the slot store). Peak occupancy is the user's typing
 * accrued while a full macro replays (MAX_EVENTS x DM_TAP_DELAY ms) at the fastest
 * sustained human rate (one event / DM_MIN_TYPING_INTERVAL_MS); since drain
 * out-paces typing, occupancy falls after the macro->drain handoff. The
 * +DM_PB_RELEASE_HEADROOM is force-append room so a captured key's RELEASE always
 * fits even at the peak (the paired-fate rule — a captured press must never get a
 * bubbled release, which would strand a modifier). Truncation of the /interval term
 * is accepted; the bubble-live path is the safe overflow fallback.
 */
#ifdef CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_PLAYBACK_BUFFER
#define PLAYBACK_BUFFER_ENABLED 1
#else
#define PLAYBACK_BUFFER_ENABLED 0
#endif

#define DM_MIN_TYPING_INTERVAL_MS 50
#define DM_PB_RELEASE_HEADROOM    4
#define PLAYBACK_BUF_SIZE \
    (((MAX_EVENTS * DM_TAP_DELAY) / DM_MIN_TYPING_INTERVAL_MS) + DM_PB_RELEASE_HEADROOM)

/* Feedback levels (numeric ladder; mirrors the Kconfig choice values). */
#define DM_FEEDBACK_OFF     0
#define DM_FEEDBACK_ERROR   1
#define DM_FEEDBACK_COMMAND 2
#define DM_FEEDBACK_BASIC   3
#define DM_FEEDBACK_VERBOSE 4
#define DM_FEEDBACK_LEVEL   CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_FEEDBACK_LEVEL

/* Status detail (compile-time, independent of runtime feedback level). */
#define DM_STATUS_OFF          0
#define DM_STATUS_COUNT        1
#define DM_STATUS_USED         2
#define DM_STATUS_USED_PREVIEW 3
#define DM_STATUS_FULL         4
#define DM_STATUS_DETAIL       CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_STATUS_DETAIL

/* Single-line status separators (compile-time strings). Fall back to the safe,
 * newline-free defaults if the Kconfig is absent (e.g. a typing-disabled build). */
#ifdef CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_STATUS_HEADER_SEP
#define DM_STATUS_HEADER_SEP CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_STATUS_HEADER_SEP
#else
#define DM_STATUS_HEADER_SEP " || "
#endif
#ifdef CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_STATUS_SLOT_SEP
#define DM_STATUS_SLOT_SEP CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_STATUS_SLOT_SEP
#else
#define DM_STATUS_SLOT_SEP " | "
#endif

/* Whether any typed output is compiled in at all. */
#define DM_TYPING_ENABLED (DM_FEEDBACK_LEVEL > DM_FEEDBACK_OFF || DM_STATUS_DETAIL > DM_STATUS_OFF)

/* Locale as the Kconfig integer (0..4). The dm_locale ENUM and its DM_LOCALE_US/
 * UK/DE/FR/FI names live in dm_render.h; this is only the selected value, cast
 * to dm_locale at the boundary. */
#if DM_TYPING_ENABLED
#define DM_LOCALE CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_LOCALE
#else
#define DM_LOCALE 0
#endif

/* Feedback style default (0 = FULL, 1 = ARROW), matching dm_feedback_pump.h's
 * DM_FB_STYLE_FULL/ARROW. */
#if DM_TYPING_ENABLED
#define DM_FEEDBACK_STYLE CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_FEEDBACK_STYLE
#else
#define DM_FEEDBACK_STYLE 0
#endif

static inline bool slot_is_nvs(int slot_idx) {
    return IS_ENABLED(CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_PERSIST) && slot_idx < NVS_SLOTS;
}

#endif /* DM_KCONFIG_H */
