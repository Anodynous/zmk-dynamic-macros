/*
 * Copyright (c) 2026 Benjamin H
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests for dm_feedback_interrupt — the PURE decision logic for interrupting
 * feedback output (status dump + SAVED-with-preview) on a keypress.
 *
 * The mechanical abort (timer stop, ring drain, suppress drop, typing_finished)
 * is Zephyr-coupled and lives in the pump; these tests pin the policy that gates
 * it, with no scheduler. Written test-first.
 */

#include "ztest_shim.h"

#include <stdbool.h>

#include <zmk-behavior-dynamic-macros/dm_feedback_interrupt.h>

ZTEST_SUITE(dm_feedback_interrupt, NULL, NULL, NULL, NULL, NULL);

/* ---- dm_fb_output_abortable ----------------------------------------------- */

/* An active, interruptible emission that is NOT the erase run is abortable —
 * exactly a status dump or a SAVED-with-preview mid-type. */
ZTEST(dm_feedback_interrupt, active_interruptible_non_erase_is_abortable) {
    zassert_true(dm_fb_output_abortable(/*emit_active=*/true, /*erase_in_progress=*/false,
                                        /*interruptible=*/true),
                 "an active interruptible output that is not an erase run must be abortable");
}

/* A short cue (REC/STOP/errors/knob) is emit_active while it types but is NOT
 * interruptible — a key mid-cue must NOT abort it. This is the regression that
 * broke erase_cancel_del + feedback_level_toggle: recording/knob keys arriving
 * during a cue truncated it. */
ZTEST(dm_feedback_interrupt, active_non_interruptible_cue_is_not_abortable) {
    zassert_false(dm_fb_output_abortable(/*emit_active=*/true, /*erase_in_progress=*/false,
                                         /*interruptible=*/false),
                  "a short cue (not interruptible) must never be aborted mid-type");
}

/* Nothing emitting: not abortable. */
ZTEST(dm_feedback_interrupt, idle_is_not_abortable) {
    zassert_false(dm_fb_output_abortable(/*emit_active=*/false, /*erase_in_progress=*/false,
                                         /*interruptible=*/true),
                  "no active emission means nothing to abort");
}

/* An in-progress auto-erase is NOT abortable through this path even if the
 * flag were set: erase has its own cancel (dm_feedback_pump_cancel_erase). */
ZTEST(dm_feedback_interrupt, erase_run_is_not_output_abortable) {
    zassert_false(dm_fb_output_abortable(/*emit_active=*/true, /*erase_in_progress=*/true,
                                         /*interruptible=*/true),
                  "an in-progress erase run is cancelled via the erase path, not here");
}

/* ---- dm_fb_listener_should_abort ------------------------------------------ */

/* A FOREIGN key during an abortable (interruptible) output aborts + swallows. */
ZTEST(dm_feedback_interrupt, foreign_key_aborts_interruptible_output) {
    zassert_true(dm_fb_listener_should_abort(/*emitting_now=*/false, /*emit_active=*/true,
                                             /*erase_in_progress=*/false, /*interruptible=*/true),
                 "a foreign key during a status/SAVED output must abort + swallow");
}

/* A FOREIGN key during a short cue (not interruptible) must NOT abort — it just
 * passes through, leaving the cue to type in full. */
ZTEST(dm_feedback_interrupt, foreign_key_during_short_cue_does_not_abort) {
    zassert_false(dm_fb_listener_should_abort(/*emitting_now=*/false, /*emit_active=*/true,
                                              /*erase_in_progress=*/false, /*interruptible=*/false),
                  "a foreign key during a non-interruptible cue must not abort it");
}

/* Our OWN emitted keystroke (emitting_now=true) must NEVER self-abort — else the
 * dump would swallow its own first keystroke and stop instantly. */
ZTEST(dm_feedback_interrupt, own_emission_never_self_aborts) {
    zassert_false(dm_fb_listener_should_abort(/*emitting_now=*/true, /*emit_active=*/true,
                                              /*erase_in_progress=*/false, /*interruptible=*/true),
                  "the pump's own emission must not abort its own output");
}

/* A foreign key while idle: nothing to abort, so it passes through (no swallow). */
ZTEST(dm_feedback_interrupt, foreign_key_while_idle_does_not_abort) {
    zassert_false(dm_fb_listener_should_abort(/*emitting_now=*/false, /*emit_active=*/false,
                                              /*erase_in_progress=*/false, /*interruptible=*/true),
                  "a foreign key with no active output must pass through untouched");
}

/* A foreign key during the erase run does NOT abort via the output path (the
 * listener's separate erase-cancel handles that). */
ZTEST(dm_feedback_interrupt, foreign_key_during_erase_not_output_abort) {
    zassert_false(dm_fb_listener_should_abort(/*emitting_now=*/false, /*emit_active=*/true,
                                              /*erase_in_progress=*/true, /*interruptible=*/true),
                  "the erase run is cancelled by the erase path, not the output-abort path");
}
