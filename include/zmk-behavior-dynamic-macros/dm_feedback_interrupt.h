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
 * True iff the pump is actively emitting (emit_active), that emission is NOT the
 * auto-erase backspace run (erase_in_progress — it has its own cancel path,
 * dm_feedback_pump_cancel_erase), AND the current operation is one we chose to
 * make interruptible (interruptible).
 *
 * The interruptible flag is the crux: emit_active alone is true for EVERY cue
 * while it types, including the short ones (REC/STOP/errors/knob). Those must NOT
 * be abortable — a key arriving mid-cue during normal use (e.g. the next command,
 * or recording keys right after REC) would otherwise truncate the cue. Only the
 * two long, informational outputs are interruptible: the status dump and
 * SAVED-with-preview. The pump sets interruptible true for exactly those and
 * false for every short cue, so this predicate draws the line the design intends.
 */
static inline bool dm_fb_output_abortable(bool emit_active, bool erase_in_progress,
                                          bool interruptible) {
    return emit_active && !erase_in_progress && interruptible;
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
                                               bool erase_in_progress, bool interruptible) {
    if (emitting_now) {
        return false;
    }
    return dm_fb_output_abortable(emit_active, erase_in_progress, interruptible);
}

#ifdef __cplusplus
}
#endif

#endif /* DM_FEEDBACK_INTERRUPT_H */
