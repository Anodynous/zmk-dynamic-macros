/*
 * Copyright (c) 2026 Benjamin H
 *
 * SPDX-License-Identifier: MIT
 *
 * dm_playback_buffer — the PURE ring that holds the user's foreign keypresses
 * captured during macro playback, to be drained after the macro (see
 * docs/playback-buffer-plan.md).
 *
 * This is the ring *structure* only (push/pop/count/empty/space over a
 * caller-provided buffer), with modular — NOT masked — wrap, because the size
 * (PLAYBACK_BUF_SIZE) is derived from Kconfig ints and is generally not a power of
 * two. It mirrors the feedback ring's shape (head/tail, one reserved slot so
 * head==tail means empty unambiguously) but is host-testable in isolation: no
 * Zephyr, no k_timer, no dm_inst. The Zephyr mechanics (raising events, the timer
 * loop, suppression) live in behavior_dynamic_macro.c and are NOT here.
 *
 * The ring stores `struct dm_event` (the shared record/replay format), so a
 * captured foreign key stores 1:1 and drains identically to a replayed macro event.
 */

#ifndef DM_PLAYBACK_BUFFER_H
#define DM_PLAYBACK_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

#include <zmk-behavior-dynamic-macros/dm_event.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A view onto a caller-owned ring: the backing array, its capacity, and the
 * head/tail cursors. The caller (struct dm_inst) owns the storage; these functions
 * are pure operations over it. `cap` is the array length; usable capacity is
 * cap - 1 (one slot reserved to disambiguate full from empty).
 */
struct dm_pb_ring {
    struct dm_event *buf;   /* backing array, length == cap */
    uint16_t         cap;   /* array length (PLAYBACK_BUF_SIZE) */
    uint16_t         head;  /* next write index */
    uint16_t         tail;  /* next read index */
};

/* Advance an index by one with a modular (non-masked) wrap. */
static inline uint16_t dm_pb_next(uint16_t i, uint16_t cap) {
    uint16_t n = (uint16_t)(i + 1);
    return n == cap ? 0 : n;
}

static inline uint16_t dm_pb_count(const struct dm_pb_ring *r) {
    return r->head >= r->tail ? (uint16_t)(r->head - r->tail)
                              : (uint16_t)(r->cap - r->tail + r->head);
}

/* Usable capacity is cap - 1: one slot is reserved so head==tail is unambiguously
 * empty (never "full"). */
static inline uint16_t dm_pb_space(const struct dm_pb_ring *r) {
    return (uint16_t)(r->cap - 1 - dm_pb_count(r));
}

static inline bool dm_pb_empty(const struct dm_pb_ring *r) {
    return r->head == r->tail;
}

static inline bool dm_pb_full(const struct dm_pb_ring *r) {
    return dm_pb_space(r) == 0;
}

/*
 * Push one event. Returns false (and does nothing) if the ring is full — that
 * false is exactly the overflow->BUBBLE trigger in the listener. Never overwrites
 * an unread event.
 */
static inline bool dm_pb_push(struct dm_pb_ring *r, const struct dm_event *ev) {
    if (dm_pb_full(r)) {
        return false;
    }
    r->buf[r->head] = *ev;
    r->head = dm_pb_next(r->head, r->cap);
    return true;
}

/*
 * Pop one event into *out (FIFO). Returns false (leaving *out untouched) if empty.
 */
static inline bool dm_pb_pop(struct dm_pb_ring *r, struct dm_event *out) {
    if (dm_pb_empty(r)) {
        return false;
    }
    *out = r->buf[r->tail];
    r->tail = dm_pb_next(r->tail, r->cap);
    return true;
}

static inline void dm_pb_reset(struct dm_pb_ring *r) {
    r->head = r->tail = 0;
}

#ifdef __cplusplus
}
#endif

#endif /* DM_PLAYBACK_BUFFER_H */
