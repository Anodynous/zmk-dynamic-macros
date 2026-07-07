/*
 * Copyright (c) 2026 Benjamin H
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests for dm_playback_emit — the PURE verdicts of the playback buffer: the
 * capture decision (press), the paired-fate release decision, and the drain
 * emit-next decision. These pin the policy; the Zephyr mechanics that act on the
 * verdicts live in the shell (native_sim integration). Written test-first.
 */

#include "ztest_shim.h"

#include <stdbool.h>

#include <zmk-behavior-dynamic-macros/dm_playback_emit.h>

ZTEST_SUITE(dm_playback_emit, NULL, NULL, NULL, NULL, NULL);

/* ---- dm_pb_capture_verdict (foreign PRESS) -------------------------------- */

ZTEST(dm_playback_emit, press_while_playing_with_space_is_captured) {
    zassert_equal(dm_pb_capture_verdict(/*playing=*/true, /*ring_has_space=*/true),
                  DM_PB_CAPTURE, "playing + room -> capture + swallow");
}

ZTEST(dm_playback_emit, press_while_playing_but_full_bubbles) {
    zassert_equal(dm_pb_capture_verdict(/*playing=*/true, /*ring_has_space=*/false),
                  DM_PB_BUBBLE, "playing + full -> overflow live");
}

ZTEST(dm_playback_emit, press_not_playing_passes_through) {
    zassert_equal(dm_pb_capture_verdict(/*playing=*/false, /*ring_has_space=*/true),
                  DM_PB_PASS, "not playing -> pass through (space irrelevant)");
    zassert_equal(dm_pb_capture_verdict(/*playing=*/false, /*ring_has_space=*/false),
                  DM_PB_PASS, "not playing -> pass through even if full");
}

/* ---- dm_pb_release_fate (paired-fate) ------------------------------------- */

/* A release whose PRESS was bubbled must also bubble — the key stays entirely live,
 * never split across the overflow boundary. */
ZTEST(dm_playback_emit, release_of_bubbled_press_bubbles) {
    zassert_equal(dm_pb_release_fate(/*press_was_bubbled=*/true),
                  DM_PB_REL_BUBBLE, "bubbled press -> bubbled release (paired fate)");
}

/* A release whose PRESS was captured must force-capture — even at nominal capacity
 * (the release headroom guarantees room), so a modifier is never stranded half-typed. */
ZTEST(dm_playback_emit, release_of_captured_press_force_captures) {
    zassert_equal(dm_pb_release_fate(/*press_was_bubbled=*/false),
                  DM_PB_REL_CAPTURE_FORCE, "captured press -> force-capture release");
}

/* ---- dm_pb_emit_next (drain decision) ------------------------------------- */

/* Macro events take priority over buffered keys. */
ZTEST(dm_playback_emit, emit_prefers_macro_over_buffer) {
    zassert_equal(dm_pb_emit_next(/*macro_remain=*/true, /*buffer_nonempty=*/true),
                  DM_PB_EMIT_MACRO, "macro remaining takes priority");
    zassert_equal(dm_pb_emit_next(/*macro_remain=*/true, /*buffer_nonempty=*/false),
                  DM_PB_EMIT_MACRO, "macro remaining even with empty buffer");
}

/* Macro exhausted, buffer non-empty: drain the buffer. */
ZTEST(dm_playback_emit, emit_drains_buffer_after_macro) {
    zassert_equal(dm_pb_emit_next(/*macro_remain=*/false, /*buffer_nonempty=*/true),
                  DM_PB_EMIT_BUFFER, "macro done + buffer non-empty -> drain buffer");
}

/* The HARD REQUIREMENT: FINISH only when BOTH are exhausted (observed empty at
 * entry). Any other combination must NOT finish. */
ZTEST(dm_playback_emit, emit_finishes_only_when_both_exhausted) {
    zassert_equal(dm_pb_emit_next(/*macro_remain=*/false, /*buffer_nonempty=*/false),
                  DM_PB_EMIT_FINISH, "macro done AND buffer empty -> finish");
    /* the three non-finish combinations, restated to lock the truth table */
    zassert_false(dm_pb_emit_next(true, true) == DM_PB_EMIT_FINISH, "T,T never finishes");
    zassert_false(dm_pb_emit_next(true, false) == DM_PB_EMIT_FINISH, "T,F never finishes");
    zassert_false(dm_pb_emit_next(false, true) == DM_PB_EMIT_FINISH, "F,T never finishes");
}
