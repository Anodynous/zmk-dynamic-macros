/*
 * Copyright (c) 2026 Benjamin H
 *
 * SPDX-License-Identifier: MIT
 *
 * dm_playback_emit — the PURE decision logic for the playback buffer (see
 * docs/playback-buffer-plan.md). Three verdicts over plain flags, host-testable in
 * isolation with no Zephyr scheduler (the seam pattern of dm_feedback_interrupt.h):
 *
 *   1. dm_pb_capture_verdict  — a foreign PRESS during playback: capture / bubble
 *                               (overflow) / pass (not playing).
 *   2. dm_pb_release_fate     — a foreign RELEASE: follow its press's fate
 *                               (paired-fate rule) — force-capture or bubble.
 *   3. dm_pb_emit_next        — what the drain emits next: macro event / buffered
 *                               key / finish.
 *
 * The mechanics (raising the event, the timer loop, suppression, the
 * playback_slot/pin lifecycle) are Zephyr-coupled and live in
 * behavior_dynamic_macro.c; only the *policy* is here.
 */

#ifndef DM_PLAYBACK_EMIT_H
#define DM_PLAYBACK_EMIT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Verdict for a foreign PRESS arriving at the listener during playback. */
typedef enum {
    DM_PB_PASS,     /* not in playback: let the event fall through unchanged */
    DM_PB_CAPTURE,  /* buffer it + swallow (HANDLED) — host sees it only on drain */
    DM_PB_BUBBLE,   /* ring full: this press overflows live (BUBBLE) */
} dm_pb_capture;

/*
 * A foreign press: capture iff we are playing AND the ring has room; if playing but
 * full, it overflows live (and the caller records it in bubbled_press[] so its
 * release follows — see dm_pb_release_fate). Not playing: pass through.
 */
static inline dm_pb_capture dm_pb_capture_verdict(bool playing, bool ring_has_space) {
    if (!playing) {
        return DM_PB_PASS;
    }
    return ring_has_space ? DM_PB_CAPTURE : DM_PB_BUBBLE;
}

/* Fate of a foreign RELEASE, resolved by whether its own PRESS was bubbled. */
typedef enum {
    DM_PB_REL_BUBBLE,        /* its press bubbled live: bubble the release too */
    DM_PB_REL_CAPTURE_FORCE, /* its press was captured: force-append the release
                              * (always fits — the release headroom), never bubble */
} dm_pb_release;

/*
 * Paired-fate rule: a key is captured *entirely* or bubbled *entirely*, never
 * split across the overflow boundary. So a release follows its press:
 *   - press was bubbled (in bubbled_press[]) -> bubble the release;
 *   - otherwise the press was captured        -> force-capture the release.
 * This is only consulted while playing; a release outside playback passes through
 * on the normal path (the caller gates on playback_slot >= 0 first).
 */
static inline dm_pb_release dm_pb_release_fate(bool press_was_bubbled) {
    return press_was_bubbled ? DM_PB_REL_BUBBLE : DM_PB_REL_CAPTURE_FORCE;
}

/* What the drain work-iteration emits next. */
typedef enum {
    DM_PB_EMIT_MACRO,   /* a macro event remains: emit it (priority) */
    DM_PB_EMIT_BUFFER,  /* macro exhausted, buffer non-empty: pop + emit */
    DM_PB_EMIT_FINISH,  /* macro exhausted AND buffer empty: finish */
} dm_pb_emit;

/*
 * Emit-next decision. Macro events take priority over buffered keys; FINISH only
 * when BOTH are exhausted. This encodes the HARD REQUIREMENT: finish is returned
 * only on an iteration that observes the buffer already empty at entry — the caller
 * emits the last buffered key on the prior iteration and re-arms, so playback_slot
 * stays live across the trailing gap and a modifier release arriving there is still
 * captured+drained in order (never leaks live).
 */
static inline dm_pb_emit dm_pb_emit_next(bool macro_remain, bool buffer_nonempty) {
    if (macro_remain) {
        return DM_PB_EMIT_MACRO;
    }
    if (buffer_nonempty) {
        return DM_PB_EMIT_BUFFER;
    }
    return DM_PB_EMIT_FINISH;
}

#ifdef __cplusplus
}
#endif

#endif /* DM_PLAYBACK_EMIT_H */
