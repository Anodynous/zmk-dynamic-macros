/*
 * Copyright (c) 2026 Benjamin H
 *
 * SPDX-License-Identifier: MIT
 *
 * Sanity test: one trivial test that passes BOTH
 * ways — as Ztest under `west test` and as a standalone host binary. Its only
 * job is to prove the dual-mode loop is wired before any pure-core module
 * (dm_render / slot_store / dm_machine) is extracted.
 *
 * It also serves as the first DECOUPLING PROOF: it includes the pure-core
 * header dm_event.h and asserts the dm_event ABI (8-byte packed), with NO
 * Zephyr include. If a future change makes that header pull in Zephyr, the
 * standalone host compile breaks here and the decoupling regression is caught
 * locally in under a second.
 */

#include <stdint.h>
#include "ztest_shim.h"

#include <zmk-behavior-dynamic-macros/dm_event.h>

ZTEST_SUITE(dm_sanity, NULL, NULL, NULL, NULL, NULL);

/* The harness itself runs. */
ZTEST(dm_sanity, harness_runs) {
    zassert_true(true, "the dual-mode harness executes a test body");
}

/* The pure-core dm_event ABI holds without Zephyr (decoupling proof seed). */
ZTEST(dm_sanity, dm_event_is_8_bytes_packed) {
    zassert_equal(sizeof(struct dm_event), 8, "dm_event must be 8 bytes packed");

    /* Field round-trips through the packed layout the storage format depends on. */
    struct dm_event ev = {
        .usage_page = 0x07,
        .keycode = 0x04,
        .implicit_mods = 0x02,
        .explicit_mods = 0x00,
        .pressed = 1,
        ._reserved = 0,
    };
    zassert_equal(ev.usage_page, 0x07, "usage_page round-trips");
    zassert_equal(ev.keycode, 0x04, "keycode round-trips");
    zassert_equal(ev.pressed, 1, "pressed round-trips");
}

/* The DM_DELAY marker encoding: the sentinel page is the ONLY marker — the
 * constructor, the predicate, and the duration accessor agree, and a real
 * keycode event can never be mistaken for a marker (the sentinel is outside
 * every HID usage page ZMK emits: 0x07 / 0x09 / 0x0C). */
ZTEST(dm_sanity, delay_event_marker_round_trip) {
    struct dm_event ev = dm_event_make_delay(50);

    zassert_equal(sizeof(ev), 8, "delay marker is the same 8-byte ABI");
    zassert_equal(ev.usage_page, DM_EVENT_DELAY_PAGE, "sentinel page written");
    zassert_equal(ev.pressed, 1, "delay marker is a press (no release twin)");
    zassert_equal(ev.implicit_mods, 0, "no implicit mods on a marker");
    zassert_equal(ev.explicit_mods, 0, "no explicit mods on a marker");
    zassert_true(dm_event_is_delay(&ev), "constructor output is recognised");
    zassert_equal(dm_event_delay_ms(&ev), 50, "duration round-trips in the keycode field");

    struct dm_event kc = {
        .usage_page = 0x07,
        .keycode = 0x04,
        .pressed = 1,
    };
    zassert_false(dm_event_is_delay(&kc), "a keyboard event is not a marker");

    /* Full range: min and max durations the Kconfig allows. */
    struct dm_event min_ev = dm_event_make_delay(1);
    struct dm_event max_ev = dm_event_make_delay(10000);
    zassert_equal(dm_event_delay_ms(&min_ev), 1, "1 ms fits");
    zassert_equal(dm_event_delay_ms(&max_ev), 10000, "10000 ms fits");
}
