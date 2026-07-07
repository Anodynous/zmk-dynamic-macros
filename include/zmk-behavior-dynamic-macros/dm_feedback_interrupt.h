/*
 * Copyright (c) 2026 Benjamin H
 *
 * SPDX-License-Identifier: MIT
 *
 * dm_feedback_interrupt — the PURE decision logic for interrupting feedback
 * output (status dump + SAVED-with-preview) on a keypress.
 *
 * The mechanics of the abort (stopping the emit timer, draining the ring,
 * dropping suppression, reporting typing_finished) are Zephyr-coupled and live
 * in dm_feedback_pump.c. But the *policy* — "is the current output abortable?"
 * and "should this listener event abort + swallow?" — is pure boolean logic of a
 * few pump/shell flags, so it lives here as static-inline predicates the host
 * suite tests directly, without a Zephyr scheduler.
 *
 * The predicates take the plain flags (not the k_timer-laden dm_feedback struct)
 * so a host test can exercise every case by constructing the flags in isolation.
 */

#ifndef DM_FEEDBACK_INTERRUPT_H
#define DM_FEEDBACK_INTERRUPT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Is a feedback output currently abortable?
 *
 * True iff the pump is actively emitting (emit_active) AND that emission is NOT
 * the auto-erase backspace run (erase_in_progress) — the erase sequence has its
 * own cancel path (dm_feedback_pump_cancel_erase) and must not be aborted here.
 * This covers exactly the two interruptible operations: the status dump and
 * SAVED-with-preview, both of which set emit_active with erase_in_progress false.
 *
 * A short cue (REC/STOP/errors/knob) is also emit_active while it types, but by
 * design we DO abort those too if this predicate alone gated it — so callers pair
 * it with the caller-context rules. In practice the two abortable operations are
 * the long ones; short cues finish faster than a human interrupt. The predicate
 * intentionally does not special-case cue vs. status: aborting a half-typed short
 * cue settles to its parked return-state exactly as a full drain would, so the
 * behavior is safe either way. (See dm_feedback_pump_cancel_output.)
 */
static inline bool dm_fb_output_abortable(bool emit_active, bool erase_in_progress) {
    return emit_active && !erase_in_progress;
}

/*
 * Given a keycode event arriving at the listener, should it abort + swallow the
 * current output?
 *
 * emitting_now is true for the pump's OWN emitted keystrokes (set around the
 * raise), false for a foreign key. A foreign key aborts iff output is abortable;
 * our own emission never self-aborts. Returning true means: abort the output AND
 * return ZMK_EV_EVENT_HANDLED (swallow the key from the host).
 */
static inline bool dm_fb_listener_should_abort(bool emitting_now, bool emit_active,
                                               bool erase_in_progress) {
    if (emitting_now) {
        return false;
    }
    return dm_fb_output_abortable(emit_active, erase_in_progress);
}

#ifdef __cplusplus
}
#endif

#endif /* DM_FEEDBACK_INTERRUPT_H */
