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

/* An active emission that is NOT the erase run is abortable — this is exactly a
 * status dump or a SAVED-with-preview mid-type. */
ZTEST(dm_feedback_interrupt, active_non_erase_is_abortable) {
    zassert_true(dm_fb_output_abortable(/*emit_active=*/true, /*erase_in_progress=*/false),
                 "active output that is not an erase run must be abortable");
}

/* Nothing emitting: not abortable (the common idle case — a stray key must not
 * try to abort a non-existent output). */
ZTEST(dm_feedback_interrupt, idle_is_not_abortable) {
    zassert_false(dm_fb_output_abortable(/*emit_active=*/false, /*erase_in_progress=*/false),
                  "no active emission means nothing to abort");
}

/* An in-progress auto-erase is NOT abortable through this path: erase has its
 * own cancel (dm_feedback_pump_cancel_erase), and conflating the two would let
 * the output-abort settle the wrong (IDLE) state over the erase's parked state. */
ZTEST(dm_feedback_interrupt, erase_run_is_not_output_abortable) {
    zassert_false(dm_fb_output_abortable(/*emit_active=*/true, /*erase_in_progress=*/true),
                  "an in-progress erase run is cancelled via the erase path, not here");
}

/* erase_in_progress without emit_active never happens in the pump, but the
 * predicate must still read false (no active emission dominates). */
ZTEST(dm_feedback_interrupt, inactive_erase_flag_is_not_abortable) {
    zassert_false(dm_fb_output_abortable(/*emit_active=*/false, /*erase_in_progress=*/true),
                  "without an active emission there is nothing to abort");
}

/* ---- dm_fb_listener_should_abort ------------------------------------------ */

/* A FOREIGN key (emitting_now=false) during an abortable output aborts+swallows. */
ZTEST(dm_feedback_interrupt, foreign_key_aborts_active_output) {
    zassert_true(dm_fb_listener_should_abort(/*emitting_now=*/false, /*emit_active=*/true,
                                             /*erase_in_progress=*/false),
                 "a foreign key during a status/SAVED output must abort + swallow");
}

/* Our OWN emitted keystroke (emitting_now=true) must NEVER self-abort — else the
 * dump would swallow its own first keystroke and stop instantly. */
ZTEST(dm_feedback_interrupt, own_emission_never_self_aborts) {
    zassert_false(dm_fb_listener_should_abort(/*emitting_now=*/true, /*emit_active=*/true,
                                              /*erase_in_progress=*/false),
                  "the pump's own emission must not abort its own output");
}

/* A foreign key while idle: nothing to abort, so it passes through (no swallow). */
ZTEST(dm_feedback_interrupt, foreign_key_while_idle_does_not_abort) {
    zassert_false(dm_fb_listener_should_abort(/*emitting_now=*/false, /*emit_active=*/false,
                                              /*erase_in_progress=*/false),
                  "a foreign key with no active output must pass through untouched");
}

/* A foreign key during the erase run does NOT abort via the output path (the
 * listener's separate erase-cancel handles that). */
ZTEST(dm_feedback_interrupt, foreign_key_during_erase_not_output_abort) {
    zassert_false(dm_fb_listener_should_abort(/*emitting_now=*/false, /*emit_active=*/true,
                                              /*erase_in_progress=*/true),
                  "the erase run is cancelled by the erase path, not the output-abort path");
}
