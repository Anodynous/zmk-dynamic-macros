/*
 * Copyright (c) 2026 Benjamin H
 *
 * SPDX-License-Identifier: MIT
 *
 * dm_config — compile-time sizing for the pure core.
 *
 * slot_store and dm_slot are sized by MAX_EVENTS / NVS_SLOTS / RAM_SLOTS. These
 * must be IDENTICAL across every translation unit that sees a dm_slot, or the
 * struct layout diverges between the pure cores and the shell. The single source
 * of truth, by build:
 *
 *   - Firmware (Zephyr): the Kconfig values. Zephyr force-includes autoconf.h
 *     globally, so the CONFIG_* macros are visible here — the pure .c files
 *     (slot_store.c, dm_machine.c, …) stay Kconfig-correct in the firmware build
 *     WITHOUT being coupled to Zephyr.
 *   - Host unit build: there is no Kconfig, so the test-representative defaults
 *     below apply. They match the Kconfig defaults so host tests exercise the
 *     same array shapes the firmware uses.
 *
 * PURE: no Zephyr include. Do not add one. (CONFIG_* come from autoconf.h, which
 * Zephyr injects on the command line — not from an include here.)
 */

#ifndef DM_CONFIG_H
#define DM_CONFIG_H

#ifdef __ZEPHYR__
/*
 * Firmware build: take the Kconfig values (NVS slots collapse to 0 when PERSIST
 * is off — a disabled bool Kconfig is undefined, not 0). The #ifndef guards make
 * each define idempotent, so a translation unit that has already set one of these
 * keeps its value and the rest are filled in here.
 */
#ifndef MAX_EVENTS
#define MAX_EVENTS CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_MAX_EVENTS
#endif
#ifndef AVG_EVENTS
#define AVG_EVENTS CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_AVG_EVENTS_PER_SLOT
#endif
#ifndef NVS_SLOTS
#if defined(CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_PERSIST)
#define NVS_SLOTS CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_NVS_SLOTS
#else
#define NVS_SLOTS 0
#endif
#endif
#ifndef RAM_SLOTS
#define RAM_SLOTS CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_RAM_SLOTS
#endif
/* Auto-overwrite of occupied slots (0/1, per slot class): an assign/move into an
 * occupied slot silently replaces the stored macro instead of being rejected.
 * Both default 0 (reject). Each symbol is ABSENT (not 0) when its depends-on is
 * off (RAM_SLOTS = 0 / PERSIST off) or in the host build, hence the inner
 * #ifdef. Host tests flip them via -D to pin the enabled rail. */
#ifndef DM_AUTO_OVERWRITE_NVS
#ifdef CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_AUTO_OVERWRITE_NVS
#define DM_AUTO_OVERWRITE_NVS CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_AUTO_OVERWRITE_NVS
#else
#define DM_AUTO_OVERWRITE_NVS 0
#endif
#endif
#ifndef DM_AUTO_OVERWRITE_RAM
#ifdef CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_AUTO_OVERWRITE_RAM
#define DM_AUTO_OVERWRITE_RAM CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_AUTO_OVERWRITE_RAM
#else
#define DM_AUTO_OVERWRITE_RAM 0
#endif
#endif

#else /* host unit build: no Kconfig — test-representative defaults */

#ifndef MAX_EVENTS
#define MAX_EVENTS 64
#endif
/* Host build keeps the pool small enough that a unit test can fill it:
 * ARENA_EVENTS = 16 * 16 = 256. */
#ifndef AVG_EVENTS
#define AVG_EVENTS 16
#endif
#ifndef NVS_SLOTS
#define NVS_SLOTS 8
#endif
#ifndef RAM_SLOTS
#define RAM_SLOTS 8
#endif
/* Off by default in the host build; the overwrite rail (Makefile) flips both to
 * 1 via -D to exercise the enabled policy. */
#ifndef DM_AUTO_OVERWRITE_NVS
#define DM_AUTO_OVERWRITE_NVS 0
#endif
#ifndef DM_AUTO_OVERWRITE_RAM
#define DM_AUTO_OVERWRITE_RAM 0
#endif

#endif /* __ZEPHYR__ */

#ifndef MAX_SLOTS
#define MAX_SLOTS (NVS_SLOTS + RAM_SLOTS)
#endif

#ifndef SLOT_CAPACITY
#define SLOT_CAPACITY (MAX_SLOTS > 0 ? MAX_SLOTS : 1)
#endif

/* The shared event arena: all stored slots draw from this one pool, sized for the
 * expected average rather than the per-slot worst case. A single macro may exceed
 * AVG_EVENTS (up to MAX_EVENTS), as long as the total across slots fits. */
#ifndef ARENA_EVENTS
#define ARENA_EVENTS (AVG_EVENTS * SLOT_CAPACITY)
#endif

#endif /* DM_CONFIG_H */
