/*
 * Copyright (c) 2026 Benjamin H
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests for dm_playback_buffer — the PURE ring holding foreign keypresses captured
 * during macro playback. Pins FIFO order (including across the modular wrap) and the
 * overflow boundary (push returns false exactly at capacity, one slot reserved).
 * The Zephyr mechanics that drive this ring live in the shell and are exercised by
 * the native_sim integration test. Written test-first.
 */

#include "ztest_shim.h"

#include <stdbool.h>
#include <stdint.h>

#include <zmk-behavior-dynamic-macros/dm_playback_buffer.h>

ZTEST_SUITE(dm_playback_buffer, NULL, NULL, NULL, NULL, NULL);

/* A small event carrying just a keycode, enough to check identity/order. */
static struct dm_event ev(uint16_t keycode, bool pressed) {
    struct dm_event e = {
        .usage_page = 0x07,
        .keycode = keycode,
        .implicit_mods = 0,
        .explicit_mods = 0,
        .pressed = (uint8_t)pressed,
        ._reserved = 0,
    };
    return e;
}

/* Build a ring over a caller-owned buffer of length cap. Fields assigned
 * imperatively (not a designated initializer) so the two-declaration macro parses
 * cleanly under both MSVC C mode and gcc/clang. */
#define MK_RING(name, capacity)                                                                    \
    struct dm_event name##_buf[capacity];                                                          \
    struct dm_pb_ring name;                                                                        \
    name.buf = name##_buf;                                                                         \
    name.cap = (capacity);                                                                         \
    name.head = 0;                                                                                 \
    name.tail = 0

/* ---- empty / count / space boundaries ------------------------------------- */

ZTEST(dm_playback_buffer, fresh_ring_is_empty) {
    MK_RING(r, 8);
    zassert_true(dm_pb_empty(&r), "fresh ring is empty");
    zassert_false(dm_pb_full(&r), "fresh ring is not full");
    zassert_equal(dm_pb_count(&r), 0, "fresh count is 0");
    zassert_equal(dm_pb_space(&r), 7, "usable space is cap-1");
}

ZTEST(dm_playback_buffer, pop_from_empty_returns_false) {
    MK_RING(r, 8);
    struct dm_event out;
    zassert_false(dm_pb_pop(&r, &out), "pop from empty returns false");
}

/* ---- FIFO order --------------------------------------------------------- */

ZTEST(dm_playback_buffer, push_pop_preserves_fifo_order) {
    MK_RING(r, 8);
    for (uint16_t i = 0; i < 5; i++) {
        struct dm_event e = ev((uint16_t)(0x10 + i), true);
        zassert_true(dm_pb_push(&r, &e), "push within capacity succeeds");
    }
    zassert_equal(dm_pb_count(&r), 5, "count after 5 pushes");
    for (uint16_t i = 0; i < 5; i++) {
        struct dm_event out;
        zassert_true(dm_pb_pop(&r, &out), "pop returns an event");
        zassert_equal(out.keycode, 0x10 + i, "FIFO: pops in push order");
    }
    zassert_true(dm_pb_empty(&r), "empty after draining");
}

/* The load-bearing case: interleaved push/pop forces head and tail past the array
 * end so the modular wrap is exercised; order must still be FIFO. */
ZTEST(dm_playback_buffer, fifo_order_preserved_across_wrap) {
    MK_RING(r, 4); /* usable capacity 3 */
    uint16_t next_push = 0x20;
    uint16_t next_pop = 0x20;

    /* Prime with 2, then repeatedly push 1 / pop 1 many times: head+tail sweep the
     * whole array multiple times, crossing the wrap boundary each lap. */
    for (int i = 0; i < 2; i++) {
        struct dm_event e = ev(next_push++, true);
        zassert_true(dm_pb_push(&r, &e), "prime push");
    }
    for (int i = 0; i < 20; i++) {
        struct dm_event e = ev(next_push++, true);
        zassert_true(dm_pb_push(&r, &e), "steady-state push has room");
        struct dm_event out;
        zassert_true(dm_pb_pop(&r, &out), "steady-state pop");
        zassert_equal(out.keycode, next_pop++, "FIFO order holds across wrap");
    }
    /* drain the remaining 2 */
    for (int i = 0; i < 2; i++) {
        struct dm_event out;
        zassert_true(dm_pb_pop(&r, &out), "drain remaining");
        zassert_equal(out.keycode, next_pop++, "FIFO to the end");
    }
    zassert_true(dm_pb_empty(&r), "empty after full sweep");
}

/* ---- overflow boundary --------------------------------------------------- */

/* push fills exactly cap-1 slots, then returns false — the overflow->BUBBLE
 * trigger. One slot stays reserved so full is distinguishable from empty. */
ZTEST(dm_playback_buffer, push_returns_false_exactly_at_capacity) {
    MK_RING(r, 4); /* usable capacity 3 */
    for (uint16_t i = 0; i < 3; i++) {
        struct dm_event e = ev((uint16_t)(0x30 + i), true);
        zassert_true(dm_pb_push(&r, &e), "the first cap-1 pushes succeed");
    }
    zassert_true(dm_pb_full(&r), "full at cap-1");
    zassert_equal(dm_pb_space(&r), 0, "no space at cap-1");

    struct dm_event overflow = ev(0x99, true);
    zassert_false(dm_pb_push(&r, &overflow), "the cap-th push overflows (false)");
    zassert_equal(dm_pb_count(&r), 3, "overflow did not mutate the ring");

    /* the reserved slot means head != tail while full: pop one, push succeeds again */
    struct dm_event out;
    zassert_true(dm_pb_pop(&r, &out), "pop makes room");
    zassert_equal(out.keycode, 0x30, "popped the oldest (FIFO)");
    zassert_true(dm_pb_push(&r, &overflow), "push succeeds after making room");
}

/* Overflowing does not corrupt the stored events (no wild write past the tail). */
ZTEST(dm_playback_buffer, overflow_push_preserves_contents) {
    MK_RING(r, 4);
    for (uint16_t i = 0; i < 3; i++) {
        struct dm_event e = ev((uint16_t)(0x40 + i), true);
        (void)dm_pb_push(&r, &e);
    }
    struct dm_event bad = ev(0xEE, true);
    (void)dm_pb_push(&r, &bad); /* rejected */
    for (uint16_t i = 0; i < 3; i++) {
        struct dm_event out;
        zassert_true(dm_pb_pop(&r, &out), "pop the surviving events");
        zassert_equal(out.keycode, 0x40 + i, "contents intact after a rejected push");
    }
}

/* reset empties the ring regardless of cursor positions. */
ZTEST(dm_playback_buffer, reset_empties) {
    MK_RING(r, 8);
    struct dm_event e = ev(0x50, true);
    (void)dm_pb_push(&r, &e);
    (void)dm_pb_push(&r, &e);
    dm_pb_reset(&r);
    zassert_true(dm_pb_empty(&r), "reset makes the ring empty");
    zassert_equal(dm_pb_count(&r), 0, "reset zeroes the count");
}
