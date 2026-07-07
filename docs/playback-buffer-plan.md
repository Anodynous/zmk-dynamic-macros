# Plan: Buffer & catch-up user keypresses during macro playback

## Context (why)

When a dynamic macro plays back, the shell replays its recorded keystrokes at
`TAP_DELAY` spacing (`playback_work_handler`, `behavior_dynamic_macro.c:297`).
Today, if the user keeps typing *during* playback, their keys interleave with the
macro output in raw arrival order, producing garbled, out-of-order text — the
listener has no branch for playback, so foreign keys just bubble to the host live
(`dm_event_listener` `suppress_recording` bubble at `behavior_dynamic_macro.c:726`,
which is toggled off *between* emitted events).

Desired behaviour: while a macro is playing, **buffer** the user's real keypresses
instead of letting them interleave; when the macro's events finish, **drain the
buffer** so those keys are typed after the macro, in press order. Because replay
runs at `TAP_DELAY` (faster than human typing), the buffer catches up quickly and
output returns to live once the buffer empties.

This is distinct from the shipped interrupt feature (which *aborts* informational
status/SAVED output on a keypress). Playback is content the user asked for, so we
**preserve every key** rather than dropping it.

### Decisions (confirmed with user)
- **Drain window:** keep buffering *through* the drain — new keys pressed during
  the drain append to the buffer; buffering ends only when the buffer fully
  empties (the catch-up point), then output returns to live.
- **Overflow:** when the buffer is full, extra keys pass through **live** (never
  lost; degrades to today's interleave only past capacity). Refined by the
  **paired-fate rule** (§2): a key never *splits* across the boundary — press and
  release share one fate — so overflow can't strand a half-typed modifier.
- **Gating:** new Kconfig `ZMK_BEHAVIOR_DYNAMIC_MACRO_PLAYBACK_BUFFER`, **default y**.
- **Faster keystrokes (companion change):** lower `TAP_DELAY` default **30 → 20ms**
  (`Kconfig:29-31`). Speeds up all typed output (playback, feedback, erase — they
  share `TAP_DELAY`) by ~33% and improves catch-up. 20ms keeps comfortable margin
  for host HID polling and held modifiers.

## Key facts established (from ../zmk + this repo)

- ZMK event dispatch is **synchronous/inline** (`../zmk/app/src/event_manager.c`),
  so a plain per-raise flag is a race-free discriminator (the pattern
  `emitting_now` already uses, `behavior_dynamic_macro.c:255/264`).
- **Fresh keycode emission is fully supported and re-entrant-safe** on the work
  queue — `raise_zmk_keycode_state_changed(...)`; the playback emitter already
  does exactly this (`:311-322`). So draining a captured key is mechanically
  identical to replaying a macro event. We do **not** need ZMK's `CAPTURED`
  re-raise machinery (that requires copying the event and re-running keymap
  resolution — unnecessary here; a foreign key is already a resolved keycode).
- `struct dm_event` (`dm_event.h:31`, 8 bytes packed) is the shared record/replay
  format — a captured foreign key stores 1:1 into it.
- Machine is in `DM_STATE_PLAYING` during playback (`dm_machine.c:409`); the
  emitter's liveness flag is `inst->playback_slot >= 0` (`dm_shell.h:91`).
- **Discriminator for a foreign key during playback:** `playback_slot >= 0 &&
  !suppress_recording`. Our own playback/drain raises hold `suppress_recording`
  true for the nested dispatch (`:320-322`); a real user key arriving between
  emitted events has it false.

## Design: swallow-and-copy, drain through the existing emitter

### 1. Buffer storage (`struct dm_inst`, `dm_shell.h`)
Add a fixed ring of `dm_event` plus the paired-fate tracking set, gated on the new
Kconfig:
```c
#if PLAYBACK_BUFFER_ENABLED
struct dm_event playback_buf[PLAYBACK_BUF_SIZE]; /* captured foreign keys */
uint16_t        playback_buf_head, playback_buf_tail;
/* keys whose PRESS was diverted live at overflow: their RELEASE must also go live
 * (paired-fate rule, §2). usage_page<<16|keycode, 0 = free. Mirrors the interrupt
 * feature's swallowed_release[]. Bounded by simultaneously-held keys. */
uint32_t        bubbled_press[4];
#endif
```
**`PLAYBACK_BUF_SIZE` is derived, not a fixed 32.** The buffer holds ONLY captured
foreign keys, never the macro (the macro replays in place from the slot store), so
peak occupancy is the user's typing accrued while the macro replays. That peak is
`MAX_EVENTS × TAP_DELAY / typing_interval`. With drain (`TAP_DELAY` 20ms) faster
than typing (~50ms), occupancy strictly falls after the macro→drain handoff, so the
handoff is the peak. Add a small **release headroom** so a captured key's *release*
can always be force-appended even at the occupancy peak (paired-fate rule, §2 —
without it a captured press could get a bubbled release, sticking a modifier). Define
(truncation accepted):
```c
#define DM_MIN_TYPING_INTERVAL_MS 50
#define DM_PB_RELEASE_HEADROOM    4   /* force-append room for held keys' releases */
#define PLAYBACK_BUF_SIZE \
    (((MAX_EVENTS * DM_TAP_DELAY) / DM_MIN_TYPING_INTERVAL_MS) + DM_PB_RELEASE_HEADROOM)
```
At defaults (MAX_EVENTS 64, TAP_DELAY 20): `1280/50 + 4 = 29` slots, **28 usable**
(one slot reserved for the full/empty discriminator, mirroring the feedback ring).
Self-scales with both Kconfig knobs. The size is generally NOT a power of two, so use
an explicit wrap (`if (++i == SIZE) i = 0`), **not** `& (SIZE-1)` masking — reuse the
feedback ring's *structure* (head/tail, one reserved slot, `ring_count`/`_space`/
`_push`/`_pop` helpers) but with modular, not masked, arithmetic. The `+4` headroom
is *justified* (release force-append), not arbitrary slack. See §5 for `DM_TAP_DELAY`
(the consolidated header constant this expression needs).

### 2. Capture in the listener (`dm_event_listener`, `behavior_dynamic_macro.c:671`)
Add the capture branch **immediately below the foreign-key erase-cancel sweep
(`:731-737`), at ~`:738`, before the recording logic** — NOT before the suppress
bubble. Ordering is load-bearing (see below).

**Gating & build topology.** The branch is gated on **`PLAYBACK_BUFFER_ENABLED`
alone — NOT `DM_TYPING_ENABLED`**: playback replays keycodes independently of
feedback typing, so a `typing-off + buffer-on` build must capture. Placement holds
in both configs because the anchor above it, the `suppress_recording` bubble
(`:722-729`), is **unconditional** (present in every build), while the erase-cancel
sweep (`:731-737`) is `#if DM_TYPING_ENABLED`. So "after the erase-cancel sweep
*where present*, else directly after the suppress bubble" is the *same line region*
— correct in both. The branch touches only **unconditional** fields
(`suppress_recording`, `playback_slot`) plus its own `PLAYBACK_BUFFER_ENABLED`-gated
ring; it does **not** use the typing-gated `swallowed_release[]` (it does its own
symmetric press+release swallow into the ring). **Checklist:** every `playback_buf`/
ring reference must sit inside a `PLAYBACK_BUFFER_ENABLED` guard so the disabled
build reverts cleanly to today's live-interleave.

The branch, when `inst->playback_slot >= 0` (playback live — Design A keeps this true
for the whole macro+drain span, so no separate `draining` flag is consulted), applies
the **paired-fate rule**: a key is captured *entirely* or bubbled *entirely* — its
press and release must never split across the overflow boundary (a captured press +
bubbled release, or vice versa, leaves an unbalanced key on the host — a **stuck
modifier** for a modifier key). Decide per-event:
- **PRESS:**
  - Ring has space → push `*ev` verbatim (same field copy the recorder uses at
    `:769-776`, but **no** modifier-folding — we replay the raw resolved event),
    return `ZMK_EV_EVENT_HANDLED` (swallow — host sees it only on drain).
  - Ring **full** → record the keycode in `bubbled_press[]`, return
    `ZMK_EV_EVENT_BUBBLE` (this key goes live, entirely).
- **RELEASE:**
  - If its keycode is in `bubbled_press[]` → clear that slot, return
    `ZMK_EV_EVENT_BUBBLE` (release follows its bubbled press — live).
  - Else (its press was captured) → **force-append** into the ring (always fits:
    the `+4` release headroom, §1) and return `ZMK_EV_EVENT_HANDLED`. A release of a
    captured press is *never* bubbled, even if the ring is at nominal capacity.

So overflow only ever diverts a **press** to live; a release never independently
overflows. `bubbled_press[]` mirrors the interrupt feature's `swallowed_release[]`
(`[4]`, `0 = free`), bounded by simultaneously-held keys. The three-way release
decision is the pure `dm_pb_release_fate` verdict (§ testing / `dm_playback_emit.h`).

**Why below the erase-cancel sweep, not before the suppress bubble:** two
invariants must survive.
1. *Every real foreign key cancels a pending auto-erase* (the general rule; both
   entry points enforce it today — `on_keymap_binding_pressed:494` and listener
   `:735`). A stale auto-erase can be armed while the machine was IDLE **just
   before** the play command (default `ERASE_DELAY` 1000ms >> a short macro), so
   it can fire *mid-playback / mid-drain* and inject backspaces into the output.
   Placing capture *below* `:735` means a captured key still cancels that stale
   erase before being buffered. (The first foreign key of the playback cancels it
   for the whole span; a macro played with *zero* foreign keys leaves the
   pre-existing "erase fires after playback" behavior unchanged — out of scope.)
2. *Our own drain/playback output must not self-capture nor spuriously cancel the
   erase.* It carries `suppress == true`, so it exits at the `:726` suppress
   bubble — above both the erase-cancel and the capture branch. So the capture
   condition needs only `playback_slot >= 0` (suppress-true output already
   returned); a redundant `&& !suppress` is harmless but unnecessary.

Note: capture **both** press and release (no lone-tap logic — unlike recording,
we replay the exact event stream). Modifiers ride in `implicit_mods`/`explicit_mods`.

**Why no modifier-folding (confirmed against ZMK HID model).** The recorder folds
(`:769-776`) because it stores one *semantic* keystroke and drops the separate bare-
modifier events via its lone-tap/bracketed `pending_mods` logic — so it must bake the
modifier onto the key. The buffer instead keeps **every event verbatim**, and ZMK's
HID modifier state is *persistent* (`../zmk/app/src/hid.c` `explicit_modifiers` is a
static flipped by each mod event, composed into every report via `SET_MODIFIERS`).
So replaying raw `press-Shift, press-A, release-A, release-Shift` reproduces Shift+A
with no folding: the replayed Shift press sets the persistent bit before A is raised.
Two consequences to keep in mind:
- **Bare modifiers are captured verbatim** — the recorder's `pending_mods`
  deferral is *entirely bypassed* on the capture path. Dropping a captured Shift
  the way the recorder does would strip the modifier from the following key.
- **The swallow (`HANDLED`) is what prevents a physical/replayed press collision.**
  `hid_listener_keycode_pressed` pre-releases an already-pressed key (`:22-35`); but
  the user's physical press was swallowed on capture, so the host never registered
  it, so `zmk_hid_is_pressed` is false when the drain replays that keycode. No
  double-press.

### 3. Drain after the macro (`playback_work_handler` decision, no new state)
Design A: `playback_slot` stays `>= 0` and the `slot_store` pin stays held for the
whole macro+drain span (see §4); **no `draining` flag**. The only change to the
handler is *what it emits next*:
- Each work iteration: if `playback_event < slot.event_count`, emit the next macro
  event (unchanged, `:310-321`); else if the buffer is non-empty, pop one
  `dm_event` from `playback_buf` and raise it through the **same** suppress-wrapped
  raise (`:320-322` pattern) at `TAP_DELAY`; else call `playback_finish`.
- Capture stays active for the whole span because it keys on `playback_slot >= 0`
  (step 2), so keys pressed during the drain keep appending — the loop continues
  until *both* macro events are exhausted **and** the buffer empties.
- `playback_finish` is unchanged (single atomic teardown: pin release + machine
  settle + `PLAY_FINISHED`); only its *call site* moves from "macro exhausted" to
  "macro exhausted AND buffer empty".

Cleanest structure: factor the "emit next unit" into one helper that prefers a
remaining macro event, else a buffered key, else finishes — so the timer→work
loop is unchanged and only the "what to emit next" decision grows.

**HARD REQUIREMENT — finish only on an iteration that finds the buffer already
empty at entry; never finish eagerly the instant the last key is popped.** This
timing is load-bearing for the modifier-balance guarantee (see edge case #1): the
iteration that emits the last buffered key must re-arm the timer and return with
`playback_slot` still `>= 0`; `playback_finish` runs on the *next* iteration, which
observes nothing to emit. This keeps `playback_slot >= 0` across the trailing
inter-iteration gap, so a modifier **release** (or any key) arriving in that gap is
still captured and drained in order rather than leaking live. An "optimization" that
finishes the moment the buffer hits empty mid-iteration would reopen that leak — do
not do it.

### 4. Machine-state interaction — **Design A (pin + PLAYING span the whole drain)**
Keep `DM_STATE_PLAYING` for the whole macro+drain span (all commands already
IGNORED there, `dm_machine.c:52`), so a DM binding pressed mid-playback still
does nothing new. `dm_machine_play_finished` is called once, at true completion.
`DM_EVT_PLAY_FINISHED` fires once, after the drain (widget sees IDLE only when
fully caught up).

**Design A vs B (resolved → A).** Two candidate splits of the emitter's
lifetime were weighed:
- **A (chosen):** `playback_slot` (emitter cursor) stays `>= 0` and the
  `slot_store` `playing_slot` pin stays held for the *entire* macro+drain span;
  `playback_finish` remains a **single atomic teardown** (pin release + machine
  settle + `PLAY_FINISHED`), called once at true completion (macro exhausted
  **and** buffer empty). The capture discriminator is `playback_slot >= 0`
  **alone** — no separate `draining` flag is needed, and the plan's
  `struct dm_inst` addition shrinks to just the ring (see §1).
- **B (rejected):** release the pin / clear `playback_slot` at *macro-end* and
  drive the drain off a `draining` flag, to unblock arena repack ~sooner.

Why A wins, at zero cost:
1. **The pin only protects slot-store reads.** During the drain the emitter
   reads `playback_buf`, never the slot — so B's early release is *safe* but
   rests on a fragile "nothing in the drain reads the slot" invariant a future
   maintainer could silently break (reintroducing a use-after-free). A needs no
   such invariant.
2. **Repack is unreachable during the drain anyway.** `arena_repack` only runs
   on an *allocating* op (record/assign) that can't fit; those are commands, and
   PLAYING IGNOREs all commands for the whole span. So B's "unblock repack
   sooner" buys nothing — there is no repack to unblock.
3. **Settling early would mis-accept a mid-drain DM binding.** If the machine
   left PLAYING at macro-end, a DM binding pressed *during the drain* (e.g. REC,
   or play-another-macro) would be **accepted**, interleaving a fresh op's output
   into the still-draining buffer. Holding PLAYING across the drain (A) keeps it
   IGNORED — this is an independent, decisive argument for A.

`draining` is therefore **dropped** (see §3 for the emit-next helper and its HARD
REQUIREMENT). `playback_finish` keeps its current two-caller shape; only the *call
site* moves from "macro exhausted" to "macro exhausted AND buffer empty".

### 5. Kconfig (`Kconfig`) + macro (`dm_kconfig.h`)
```
config ZMK_BEHAVIOR_DYNAMIC_MACRO_PLAYBACK_BUFFER
    bool "Buffer user keypresses during macro playback and drain them after"
    default y
    help
      While a macro plays, hold the user's own keypresses and type them after the
      macro finishes, in order, instead of letting them interleave. The buffer
      catches up quickly (replay is faster than typing). Keys past the buffer's
      capacity pass through live rather than being lost.
```
In `dm_kconfig.h` (mirror the `DM_STATUS_*` `#ifdef CONFIG_… / #else` host-fallback
idiom):
- **Consolidate `TAP_DELAY` → `DM_TAP_DELAY`** (prerequisite): it is currently
  `#define`d twice (`behavior_dynamic_macro.c:50`, `dm_feedback_pump.c:25`). Move
  the canonical definition here with a host fallback (`#else 20`); both `.c` files
  drop their local `#define` and consume `DM_TAP_DELAY`. `PLAYBACK_BUF_SIZE` needs
  it visible in the header (it sizes the ring member in `dm_shell.h`).
- `PLAYBACK_BUFFER_ENABLED` (from the new Kconfig bool) + `PLAYBACK_BUF_SIZE` (the
  derived expression from §1), both host-safe.

## Edge cases to handle
- **Modifier held across the playback→drain→live boundary.** Because we buffer
  **both press and release** verbatim and drain in FIFO order, and (per the §3 HARD
  REQUIREMENT) `playback_slot` stays `>= 0` until an iteration finds the buffer
  already empty at entry, a modifier **release** arriving *any time during the drain
  — including the trailing inter-iteration gap — is itself captured and drained in
  order.** So Shift↓,A↓,A↑,…,Shift↑ replays fully paired and balanced; this is a
  **guarantee**, not merely "acceptable." The only genuinely-live release is one
  arriving strictly *after* true IDLE (`playback_slot == -1`, machine settled) — and
  that is also balanced, because the drained Shift↓ set a *persistent* HID modifier
  bit (see §2 "no modifier-folding") that the later live Shift↑ clears. Net: no
  unbalanced hold in either case. **Test both**: (a) modifier released mid-drain →
  captured+drained paired; (b) modifier still physically held past true IDLE →
  drained press + live release, HID modifier balanced.
- **Playback cancelled / superseded** (e.g. delete-while-playing races, see
  `slot_store` playing-slot pinning). If playback ends abnormally, drain the
  buffer anyway (the keys were the user's), then finish. Verify against the
  existing `record_while_playing` / delete-during-play tests.
- **Overflow splits a key (paired-fate).** At the ring-full boundary, a naive
  per-event bubble could capture a press but bubble its release (or vice versa),
  leaving an unbalanced key on the host — a **stuck modifier** for a modifier. The
  paired-fate rule (§2) prevents this: overflow only diverts a *press* live and
  remembers it in `bubbled_press[]`; that key's release then also bubbles, and a
  captured press's release always force-appends (via the `+4` headroom). **Test:**
  fill the ring, then press+release a modifier past capacity → both events bubble
  live together (balanced), no stuck modifier; and a modifier whose press was
  captured always gets its release captured even when the ring hit nominal capacity.
- **Buffer non-empty but macro was empty-slot rejected** — rejection happens
  before `DM_STATE_PLAYING`, so no capture occurs; nothing to drain.
- **DM binding pressed during playback (incl. "play another macro").** A DM
  command binding does **not** flow through `dm_event_listener` — it enters via
  `on_keymap_binding_pressed` (`behavior_dynamic_macro.c:487`), dispatches to the
  machine, is IGNORED in `DM_STATE_PLAYING`, and is swallowed (`OPAQUE`). It is
  **neither buffered nor queued**: the buffer captures typed *keycodes* only, not
  macro *invocations* (a binding press is a behavior invocation carrying a slot
  param, not a `dm_event` keycode — there is nothing to store in the ring).
  Playing a second macro mid-playback is a **non-goal** — the press no-ops,
  matching today's behavior. Because Design A holds PLAYING across the *entire*
  drain, this stays true during the drain too (a mid-drain REC/play is still
  IGNORED). Queuing macro commands would need a separate command queue with its
  own supersede/delete-race semantics and is out of scope. **Test:** a DM binding
  pressed mid-playback AND mid-drain both no-op and do not corrupt the drain.

## Files to modify
- `Kconfig` — new `PLAYBACK_BUFFER` option; **lower `TAP_DELAY` default 30 → 20**.
- `include/zmk-behavior-dynamic-macros/dm_kconfig.h` — consolidate `DM_TAP_DELAY`
  (+ host fallback); `PLAYBACK_BUFFER_ENABLED` + derived `PLAYBACK_BUF_SIZE`.
- `include/zmk-behavior-dynamic-macros/dm_playback_buffer.h` — **new pure seam**:
  the ring (`push`/`pop`/`count`/`empty`/`space`, modular wrap, one reserved slot),
  typed on `dm_event`. No Zephyr. Host-tested.
- `include/zmk-behavior-dynamic-macros/dm_playback_emit.h` — **new pure seam**: three
  verdict predicates — `dm_pb_capture_verdict(playing, has_space)` → CAPTURE/BUBBLE/
  PASS (press path); `dm_pb_release_fate(press_was_bubbled)` → BUBBLE/CAPTURE_FORCE
  (release path, paired-fate rule); `dm_pb_emit_next(macro_remain, buf_nonempty)` →
  MACRO/BUFFER/FINISH. No Zephyr. Host-tested (incl. the finish-only-when-both-empty
  truth table and the paired-fate release verdict).
- `include/zmk-behavior-dynamic-macros/dm_shell.h` — buffer ring member on
  `struct dm_inst` (**no `draining` flag** — Design A).
- `src/behaviors/behavior_dynamic_macro.c` — capture branch in `dm_event_listener`
  (below the erase-cancel sweep); drain decision in `playback_work_handler` driving
  the Zephyr mechanics (raise, timer re-arm, suppress-wrap, `playback_slot`/pin
  lifecycle) off the two pure seams. Consume `DM_TAP_DELAY`.
- `src/dm_feedback_pump.c` — consume `DM_TAP_DELAY` (drop its local `#define`).
- `README.md` / `docs/keycodes.md` — document the buffer option and the new
  `TAP_DELAY` default.

### TAP_DELAY change is independent — ship it as its own commit
The 30→20 default is a one-line Kconfig change with no code dependency on the
buffer feature. Land it as a separate `feat`/`tweak` commit. **It shifts the
default inter-key timing, so any native_sim golden whose capture window is
timing-sensitive may change** — most tests hardcode `TAP_DELAY` in their
`native_sim.conf` (e.g. `40`/`100`), so they are insulated; but sweep for tests
relying on the *default* and regenerate any affected golden in CI (same workflow
as the erase-delay change last session).

## Testing / verification
- **Host ztest (`tests/unit/`)**: two pure seams (mirroring `dm_feedback_interrupt.h`),
  each with its own test TU so a ring-wrap bug and a finish-timing bug fail on
  distinct surfaces:
  - `test_playback_buffer.c` (ring): FIFO order preserved **across the wrap**;
    `push` returns false **exactly at capacity** (the overflow→BUBBLE trigger, one
    reserved slot); `count`/`space`/`empty` boundaries; pop-from-empty defined.
  - `test_playback_emit.c` (verdicts): `dm_pb_capture_verdict` truth table
    (playing+space→CAPTURE, playing+full→BUBBLE, not-playing→PASS); `dm_pb_release_fate`
    (press_was_bubbled→BUBBLE, else→CAPTURE_FORCE) — the **paired-fate rule**;
    `dm_pb_emit_next` truth table with the **HARD REQUIREMENT** pinned — FINISH iff
    *both* macro_remain and buf_nonempty are false; MACRO takes priority over BUFFER.
  Run `powershell -File tests\unit\run-host.ps1` (142+ tests, local fast loop).
- **native_sim integration (`tests/**/native_sim.keymap`, CI-only —
  west/native_sim cannot run locally)**: a new `tests/core/playback_buffer/` (or
  `tests/feedback/`) test: record a macro, start playback, press mock foreign keys
  mid-playback, assert the emitted keycode stream is `macro… then foreign…` in
  order with the foreign keys NOT interleaved. Model on `record_while_playing` +
  the interrupt tests. Seed an empty snapshot, generate the golden in CI
  (`ZMK_TESTS_AUTO_ACCEPT`), verify it shows the ordering, then commit — the
  established snapshot workflow.
- Regression: confirm existing `play_empty`, delete/move, and `record_while_playing`
  tests stay green.
  - **`record_while_playing` is NOT at risk** and its golden must **not** change:
    inspection shows it presses a REC **binding** (not a foreign keycode) during
    playback — that enters via `on_keymap_binding_pressed`, is IGNORED, swallowed
    OPAQUE, and never reaches the listener's capture branch. It exercises the
    binding-during-playback path (untouched, §ec "DM binding pressed during
    playback"), not the foreign-keycode path we change. If its snapshot changes,
    that signals an accidental behavioral change — investigate, don't auto-accept.
  - RECORDING and PLAYING are **mutually exclusive** machine states (`dm_machine.c`
    legality matrix; PLAYING row is all-zeros so REC is IGNORED). So capture
    (`playback_slot >= 0`) and the recording branch (`state == RECORDING`, `:765`)
    can never both fire for one key — no collision, no double-handling.
  - The only tests whose golden *legitimately* changes are ones that press mock
    **foreign keycodes** mid-playback expecting the old interleaved order. Sweep
    for those; a buffered-then-drained golden is the *correct* new behavior (regen
    in CI), not a regression. (The new `playback_buffer` test is the deliberate
    one.)

## Implementation status (landed this session)
- **Pure seams + host tests DONE and green (157 host tests, was ~142):**
  `dm_playback_buffer.h` (ring) + `test_playback_buffer.c`; `dm_playback_emit.h`
  (three verdicts) + `test_playback_emit.c`. Run via `tests\unit\run-host.ps1`.
- **Kconfig/consolidation DONE:** `TAP_DELAY` default 30→20; new
  `..._PLAYBACK_BUFFER` (default y); `DM_TAP_DELAY` consolidated into `dm_kconfig.h`
  (both `.c` files consume it); `PLAYBACK_BUFFER_ENABLED` + derived `PLAYBACK_BUF_SIZE`.
- **Shell wiring DONE:** ring + `bubbled_press[4]` on `struct dm_inst` (guarded);
  ring init in `behavior_dynamic_macro_init`; `playback_raise` helper; drain decision
  in `playback_work_handler` (verdict-driven, HARD REQUIREMENT honored — last buffered
  key re-arms, FINISH only on a both-empty-at-entry iteration); capture branch in the
  listener below the erase-cancel sweep, gated on `PLAYBACK_BUFFER_ENABLED` alone, with
  the paired-fate rule.
- **native_sim integration test SEEDED, CI-only:** `tests/core/playback_buffer/`
  records `A A A`, plays, presses foreign `B` mid-playback. The
  `keycode_events.snapshot` is a **hand-authored prediction** (west/native_sim cannot
  run locally): six macro-A events (3 recorded-live + 3 replayed) then one drained
  `B` press/release **last**, not interleaved. **CI confirms or corrects it.** The
  load-bearing assertion is B's *position* (last, not mid-stream). Timing caveat: the
  exact tick at which B lands relative to playback is what CI validates; if B lands
  after playback fully finishes (no capture) the golden would show B mid/near-end
  differently — inspect the CI-generated golden to confirm B was actually *captured*
  (swallowed during playback) and drained, not merely typed live after.
- **TODO (docs):** `README.md` / `docs/keycodes.md` — document the buffer option and
  the new `TAP_DELAY` default.
