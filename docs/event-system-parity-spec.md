# Event System Parity — Implementation Spec

Status: proposed, ready to implement
Scope: `CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_EVENTS` event path only
Audience: an implementer who has not designed the change. Every decision is
made here; follow the checklist at the end top to bottom.

---

## 1. Goal

Bring the widget **event system** up to parity with the **feedback system**, so a
display can show everything feedback can speak — recording, stop/assign-pending,
saved, move (prompt → source → moved/cancel), delete (mode → deleted), preview,
chaining, the occupied/empty/full/queue-full errors, and the knob settings
(level / style / erase) — plus a reliable "what mode am I in now" signal.

Two complementary signals come out of this work, and the design keeps them
distinct on purpose:

- **`event` (type)** — *what just happened* (a transition). The rich, transient
  signal. Parity = every user-meaningful transition that feedback speaks also
  raises a notify code that maps to a public event.
- **`state` (effective mode)** — *what mode are we in now / heading into*. Made
  accurate by expanding the public state enum and reporting the machine's
  **effective** state (the parked return-state while feedback is typing).

### Non-goals

- No change to the feedback (typed-output) behavior, message tables, or `dm_spec`
  / `dm_fb_kind`. We only *add notify calls alongside* existing transitions.
- `STATUS_HEADER` / `STATUS_SLOT` are **not** events. They are responses to a
  `DM_STATE` query; a display reads that data on demand through the existing
  query API (`dm_get_used_*_slots`, `dm_is_slot_empty`, `dm_get_preview_string`).
  Do not turn them into events.
- No live "recording progress" event. A widget that wants a live event counter
  polls `dm_get_recording_event_count()` on its own timer; that API already
  exists. (Documented as a future option, not built here.)

---

## 2. Background — the seam this builds on

The redesign already has the right architecture; we are completing it, not
reshaping it.

```
dm_machine ──notify(code,slot[,slot2])──▶ cb_notify ──▶ dm_events_raise
   │                                                         │
   └──speak(dm_feedback_spec)──▶ dm_feedback (typed output)  └─▶ zmk_dynamic_macro_state_changed
```

- `dm_machine` is the **only writer of `state`** and the only raiser of notify
  codes (except `PLAY_FINISHED`, raised by the shell's playback emitter —
  `src/behaviors/behavior_dynamic_macro.c`). It raises a `dm_notify_code`
  (`include/.../dm_notify.h`) through the `notify` callback.
- `dm_events.c::map_event()` translates `dm_notify_code` → the public
  `zmk_dynamic_macro_event_type`. It has **no `default` case**, so `-Wswitch`
  forces every new code to be mapped — lean on that.
- `dm_events.c::coarse_state()` currently flattens everything that is not
  `RECORDING`/`PLAYING` to `IDLE`.

### The asymmetry we are removing

`dm_fb_kind` (the feedback vocabulary, `dm_spec.h`) is today a **superset** of
`dm_notify_code` (the event vocabulary). Feedback can say things events cannot:

| Feedback `dm_fb_kind` | Raises a notify today? |
| --- | --- |
| `DM_FB_REC` / `DM_FB_STOP` / `DM_FB_NO_REC` | yes |
| `DM_FB_SAVED` / `DM_FB_DELETED` / `DM_FB_MOVED` | yes |
| `DM_FB_OVERFLOW` | yes (`ERROR_OVERFLOW`) |
| `DM_FB_SLOT_EMPTY` | yes (`ERROR_SLOT_EMPTY`) |
| `DM_FB_SAVE_FAILED` / `DM_FB_DELETE_FAILED` | yes |
| `DM_FB_SAVE_QFULL` / `DM_FB_DELETE_QFULL` | yes, but **collapsed** to one `ERROR_QUEUE_FULL` |
| `DM_FB_SLOT_FULL` (assign/move target occupied) | **no** |
| `DM_FB_MOVE_PROMPT` (move mode entered) | **no** |
| `DM_FB_MOVE_SRC` (source selected) | **no** |
| `DM_FB_MOVE_CANCEL` | **no** |
| `DM_FB_CHAIN_INSERT` | **no** |
| `DM_FB_CHAIN_NO_ROOM` | **no** |
| `DM_FB_KNOB` (level / style / erase) | **no** |
| delete-mode entry (`do_delete_mode`, no cue) | **no** |
| preview-mode entry (`do_preview`, no cue) | **no** |
| pending-state **timeout** (`dm_machine_timeout`) | **no** |

The fix inverts the relationship: **`dm_notify_code` becomes the superset / single
source of truth for "a user-meaningful thing happened."** The feedback builder
ignores codes it does not type; the event mapper maps them all.

---

## 3. Design decisions (fixed — do not re-litigate while implementing)

**D1 — Notify is the source of truth.** Add notify codes for every transition in
the gap table above. Feedback is untouched; we only add `notify(...)` lines next
to the existing `speak(...)` lines.

**D2 — `state` reports the *effective* state.** Add `dm_machine_effective_state()`
that returns the parked return-state while the machine is in a typing state, and
the live state otherwise. `dm_events.c` maps **that** to the public state. Result:
`dm_get_state()` and every event's `.state` agree and both report the mode the
machine has settled into (or is settling into).

```c
/* dm_machine.c */
dm_state dm_machine_effective_state(const dm_machine *m) {
    if (m->state == DM_STATE_TYPING_FEEDBACK) return m->return_state;
    if (m->state == DM_STATE_TYPING_ERASE)    return m->erase_return_state;
    return m->state;
}
```
`return_state` / `erase_return_state` are always settled states (never `TYPING_*`),
so the result is always a settled state.

**D3 — Uniform ordering rule: raise notify AFTER the single `state` write.** Every
transition writes `state` exactly once (via `enter_typing()` or a direct
`m->state = ...`). Move/keep every `notify(...)` call so it runs **after** that
write. Combined with D2 this makes `.state` the *destination* mode for every
event. This **reverses** the current "raise before the write so the event reads
the pre-transition state" intent for `do_rec` / `do_stop` / `do_overflow` /
`slot_recording`(empty) / `slot_play`(empty) / `deliver_async` /
`typing_finished`(qfull); update those code comments accordingly. See §6 for the
before/after `.state` table and the test impact.

**D4 — Public enums are append-only.** The redesign branch is treated as the
about-to-ship baseline. Existing `enum zmk_dynamic_macro_state` ordinals (IDLE=0,
RECORDING=1, PLAYING=2) and existing `zmk_dynamic_macro_event_type` ordinals
(0..13) must not move. New constants append. `dm_notify_code` is internal and
may be reordered freely (it is mapped).

**D5 — Context via state, not code proliferation.** `ERROR_SLOT_EMPTY` stays a
single code for chain-empty / move-source-empty / delete-empty / play-empty. The
display disambiguates from `.state` (e.g. `ERROR_SLOT_EMPTY` while
`state == RECORDING` is a failed chain). We do **not** add per-context empty
codes. We *do* split the genuinely distinct cases feedback distinguishes:
occupied (`SLOT_FULL`), chain-full vs recording-overflow, and save- vs
delete-queue-full.

**D6 — Add `slot2` to the event** for two-slot operations. `MOVED` reports
`slot = dst`, `slot2 = src`. `MOVE_SRC_SELECTED` reports `slot = src`,
`slot2 = -1`. All other events set `slot2 = -1`.

**D7 — Settings (knob) parity = one event + queries.** A knob change raises
`SETTINGS_CHANGED` (slot/slot2 = -1). The display reads current values on demand
via three new query functions. We do **not** encode the value in the event.

---

## 4. The complete target enums

### 4.1 Public state — `events/dynamic_macro_state_changed.h`

```c
enum zmk_dynamic_macro_state {
    ZMK_DYNAMIC_MACRO_STATE_IDLE = 0,
    ZMK_DYNAMIC_MACRO_STATE_RECORDING = 1,
    ZMK_DYNAMIC_MACRO_STATE_PLAYING = 2,
    /* appended (D4) */
    ZMK_DYNAMIC_MACRO_STATE_ASSIGN_PENDING = 3,  /* recording stopped, awaiting slot */
    ZMK_DYNAMIC_MACRO_STATE_MOVE_PENDING = 4,    /* move mode: awaiting source or dest */
    ZMK_DYNAMIC_MACRO_STATE_DELETE_PENDING = 5,  /* delete mode: awaiting slot */
    ZMK_DYNAMIC_MACRO_STATE_PREVIEW_PENDING = 6, /* preview mode: awaiting slot */
};
```

### 4.2 Public event type — `events/dynamic_macro_state_changed.h`

Keep the existing 14 constants in their existing order (ordinals 0..13). Append:

```c
    /* mode / transition (appended) */
    ZMK_DYNAMIC_MACRO_MOVE_PROMPT,            /* entered move mode */
    ZMK_DYNAMIC_MACRO_MOVE_SRC_SELECTED,      /* slot = source */
    ZMK_DYNAMIC_MACRO_MOVE_CANCELLED,         /* same-slot cancel */
    ZMK_DYNAMIC_MACRO_DELETE_PROMPT,          /* entered delete mode */
    ZMK_DYNAMIC_MACRO_PREVIEW_PROMPT,         /* entered preview mode */
    ZMK_DYNAMIC_MACRO_CHAIN_INSERTED,         /* slot chained into the draft */
    ZMK_DYNAMIC_MACRO_PENDING_CANCELLED,      /* a *_PENDING mode timed out -> IDLE */
    ZMK_DYNAMIC_MACRO_SETTINGS_CHANGED,       /* level / style / erase changed */
    /* errors (appended) */
    ZMK_DYNAMIC_MACRO_ERROR_SLOT_OCCUPIED,    /* assign/move target not empty */
    ZMK_DYNAMIC_MACRO_ERROR_CHAIN_FULL,       /* chain would exceed MAX_EVENTS */
    ZMK_DYNAMIC_MACRO_ERROR_SAVE_QUEUE_FULL,  /* split from ERROR_QUEUE_FULL */
    ZMK_DYNAMIC_MACRO_ERROR_DELETE_QUEUE_FULL,/* split from ERROR_QUEUE_FULL */
```

`ZMK_DYNAMIC_MACRO_ERROR_QUEUE_FULL` (ordinal 11) is **retained** for source
compatibility but is **no longer raised** — the split SAVE/DELETE variants
replace it. Mark it deprecated in a comment.

### 4.3 Event struct — `events/dynamic_macro_state_changed.h`

```c
struct zmk_dynamic_macro_state_changed {
    enum zmk_dynamic_macro_state state;
    enum zmk_dynamic_macro_event_type event;
    int slot;          /* primary slot, or -1 */
    int slot2;         /* secondary slot (move source for MOVED), or -1   <-- NEW */
    bool slot_is_nvs;  /* valid when slot  >= 0 */
    bool slot2_is_nvs; /* valid when slot2 >= 0                            <-- NEW */
};
```
(ZMK modules compile from source — there is no binary ABI; widgets just
recompile.)

### 4.4 Internal notify — `dm_notify.h`

```c
typedef enum {
    DM_EVT_RECORDING_STARTED = 0,
    DM_EVT_RECORDING_STOPPED,
    DM_EVT_SAVED,
    DM_EVT_DELETED,
    DM_EVT_MOVED,
    DM_EVT_PLAY_STARTED,
    DM_EVT_PLAY_FINISHED,
    DM_EVT_PREVIEW_READY,
    /* new mode / transition codes */
    DM_EVT_MOVE_PROMPT,
    DM_EVT_MOVE_SRC_SELECTED,
    DM_EVT_MOVE_CANCELLED,
    DM_EVT_DELETE_PROMPT,
    DM_EVT_PREVIEW_PROMPT,
    DM_EVT_CHAIN_INSERTED,
    DM_EVT_PENDING_CANCELLED,
    DM_EVT_SETTINGS_CHANGED,
    /* errors */
    DM_EVT_ERROR_NO_RECORDING,
    DM_EVT_ERROR_SLOT_EMPTY,
    DM_EVT_ERROR_SLOT_OCCUPIED,
    DM_EVT_ERROR_OVERFLOW,
    DM_EVT_ERROR_CHAIN_FULL,
    DM_EVT_ERROR_SAVE_FAILED,
    DM_EVT_ERROR_DELETE_FAILED,
    DM_EVT_ERROR_SAVE_QUEUE_FULL,
    DM_EVT_ERROR_DELETE_QUEUE_FULL,
    DM_EVT__COUNT, /* keep last */
} dm_notify_code;
```

### 4.5 `map_event()` — `dm_events.c`

One-to-one; no `default` (keep `-Wswitch` enforcement). `DM_EVT__COUNT` keeps its
`break;`/fallthrough-to-safe-return as today.

| `dm_notify_code` | public `zmk_dynamic_macro_event_type` |
| --- | --- |
| `DM_EVT_RECORDING_STARTED` | `…RECORDING_STARTED` |
| `DM_EVT_RECORDING_STOPPED` | `…RECORDING_STOPPED` |
| `DM_EVT_SAVED` | `…SAVED` |
| `DM_EVT_DELETED` | `…DELETED` |
| `DM_EVT_MOVED` | `…MOVED` |
| `DM_EVT_PLAY_STARTED` | `…PLAY_STARTED` |
| `DM_EVT_PLAY_FINISHED` | `…PLAY_FINISHED` |
| `DM_EVT_PREVIEW_READY` | `…PREVIEW_READY` |
| `DM_EVT_MOVE_PROMPT` | `…MOVE_PROMPT` |
| `DM_EVT_MOVE_SRC_SELECTED` | `…MOVE_SRC_SELECTED` |
| `DM_EVT_MOVE_CANCELLED` | `…MOVE_CANCELLED` |
| `DM_EVT_DELETE_PROMPT` | `…DELETE_PROMPT` |
| `DM_EVT_PREVIEW_PROMPT` | `…PREVIEW_PROMPT` |
| `DM_EVT_CHAIN_INSERTED` | `…CHAIN_INSERTED` |
| `DM_EVT_PENDING_CANCELLED` | `…PENDING_CANCELLED` |
| `DM_EVT_SETTINGS_CHANGED` | `…SETTINGS_CHANGED` |
| `DM_EVT_ERROR_NO_RECORDING` | `…ERROR_NO_RECORDING` |
| `DM_EVT_ERROR_SLOT_EMPTY` | `…ERROR_SLOT_EMPTY` |
| `DM_EVT_ERROR_SLOT_OCCUPIED` | `…ERROR_SLOT_OCCUPIED` |
| `DM_EVT_ERROR_OVERFLOW` | `…ERROR_OVERFLOW` |
| `DM_EVT_ERROR_CHAIN_FULL` | `…ERROR_CHAIN_FULL` |
| `DM_EVT_ERROR_SAVE_FAILED` | `…ERROR_SAVE_FAILED` |
| `DM_EVT_ERROR_DELETE_FAILED` | `…ERROR_DELETE_FAILED` |
| `DM_EVT_ERROR_SAVE_QUEUE_FULL` | `…ERROR_SAVE_QUEUE_FULL` |
| `DM_EVT_ERROR_DELETE_QUEUE_FULL` | `…ERROR_DELETE_QUEUE_FULL` |

---

## 5. Threading `slot2` and updating the state mapper

### 5.1 Notify callback signature (vtable)

`dm_machine.h` — add `slot2`:

```c
void (*notify)(void *ctx, int event, int slot, int slot2);
```

`dm_machine.c` — the static helper gains `slot2` and a 3-arg convenience:

```c
static void notify(dm_machine *m, int event, int slot, int slot2) {
    if (m->cb->notify) m->cb->notify(m->cb->ctx, event, slot, slot2);
}
```
Every existing call becomes `notify(m, CODE, slot, -1)` except `MOVED`
(see §6). Host-test fakes that implement `notify` must update their signature and
record `slot2`.

### 5.2 Shell — `dm_events_raise` and `cb_notify`

`dm_shell.h` + `dm_events.c` — `dm_events_raise` gains `slot2`:

```c
void dm_events_raise(struct dm_inst *inst, int machine_event, int slot, int slot2);
```
`cb_notify` forwards it: `dm_events_raise(ctx, event, slot, slot2);`
The playback emitter's `PLAY_FINISHED` call becomes
`dm_events_raise(inst, DM_EVT_PLAY_FINISHED, slot, -1);`

### 5.3 `coarse_state` → `map_state` (uses effective state)

Rename and rewrite in `dm_events.c`:

```c
static enum zmk_dynamic_macro_state map_state(struct dm_inst *inst) {
    switch (dm_machine_effective_state(&inst->machine)) {
    case DM_STATE_RECORDING:       return ZMK_DYNAMIC_MACRO_STATE_RECORDING;
    case DM_STATE_PLAYING:         return ZMK_DYNAMIC_MACRO_STATE_PLAYING;
    case DM_STATE_PENDING_ASSIGN:  return ZMK_DYNAMIC_MACRO_STATE_ASSIGN_PENDING;
    case DM_STATE_MOVE_PENDING:    return ZMK_DYNAMIC_MACRO_STATE_MOVE_PENDING;
    case DM_STATE_DELETE_PENDING:  return ZMK_DYNAMIC_MACRO_STATE_DELETE_PENDING;
    case DM_STATE_PREVIEW_PENDING: return ZMK_DYNAMIC_MACRO_STATE_PREVIEW_PENDING;
    default:                       return ZMK_DYNAMIC_MACRO_STATE_IDLE;
    }
}
```
`dm_events_raise` uses `map_state(inst)`; `dm_get_state()` switches from
`coarse_state` to `map_state` too (so polling and events agree).

`dm_events_raise` body sets the new fields:

```c
raise_zmk_dynamic_macro_state_changed((struct zmk_dynamic_macro_state_changed){
    .state = map_state(inst),
    .event = map_event(machine_event),
    .slot = slot,
    .slot2 = slot2,
    .slot_is_nvs  = slot  >= 0 ? slot_is_nvs(slot)  : false,
    .slot2_is_nvs = slot2 >= 0 ? slot_is_nvs(slot2) : false,
});
```
Update the `LOG_DBG` shape (see §7).

---

## 6. Per-transition edits in `dm_machine.c`

Apply D3 (notify **after** the state write) everywhere. Add the new notifies.
"after enter_typing(X)" means immediately after that call and before `speak(...)`.

| Function | Edit |
| --- | --- |
| `do_rec` | move `notify(RECORDING_STARTED,-1,-1)` to **after** `enter_typing(RECORDING)` |
| `do_stop` (empty draft) | move `notify(ERROR_NO_RECORDING,-1,-1)` after `enter_typing(IDLE)` |
| `do_stop` (normal) | move `notify(RECORDING_STOPPED,-1,-1)` after `enter_typing(PENDING_ASSIGN)` |
| `do_overflow` | move `notify(ERROR_OVERFLOW,-1,-1)` after `enter_typing(PENDING_ASSIGN)` |
| `do_delete_mode` | **add** `notify(DELETE_PROMPT,-1,-1)` after `m->state = DM_STATE_DELETE_PENDING` |
| `do_move_mode` | **add** `notify(MOVE_PROMPT,-1,-1)` after `enter_typing(MOVE_PENDING)` |
| `do_preview` | **add** `notify(PREVIEW_PROMPT,-1,-1)` after `m->state = DM_STATE_PREVIEW_PENDING` |
| `do_knob` | **add** `notify(SETTINGS_CHANGED,-1,-1)` after `m->cb->apply_knob(...)` |
| `slot_recording` (empty) | move `notify(ERROR_SLOT_EMPTY,idx,-1)` after `enter_typing(RECORDING)` |
| `slot_recording` (no room) | **add** `notify(ERROR_CHAIN_FULL,idx,-1)` after `enter_typing(RECORDING)` |
| `slot_recording` (success) | **add** `notify(CHAIN_INSERTED,idx,-1)` after `enter_typing(RECORDING)` |
| `slot_assign` (occupied) | **add** `notify(ERROR_SLOT_OCCUPIED,idx,-1)` after `enter_typing(PENDING_ASSIGN)` |
| `slot_assign` (`rc != DM_OK`) | **add** `notify(ERROR_SLOT_OCCUPIED,idx,-1)` after `enter_typing(PENDING_ASSIGN)` |
| `slot_assign` (success) | keep `notify(SAVED,idx,-1)` (already after `enter_typing(IDLE)`) |
| `slot_delete` (empty) | keep `notify(ERROR_SLOT_EMPTY,idx,-1)` (already after `enter_typing(IDLE)`) |
| `slot_delete` (queue full) | change to `notify(ERROR_DELETE_QUEUE_FULL,idx,-1)` |
| `slot_delete` (deferred) | unchanged (no notify; `DELETED` arrives via `deliver_async`) |
| `slot_delete` (RAM sync) | keep `notify(DELETED,idx,-1)` |
| `slot_move` (source empty) | keep `notify(ERROR_SLOT_EMPTY,idx,-1)` |
| `slot_move` (source selected) | **add** `notify(MOVE_SRC_SELECTED,idx,-1)` after `enter_typing(MOVE_PENDING)` |
| `slot_move` (same-slot cancel) | **add** `notify(MOVE_CANCELLED,-1,-1)` after `enter_typing(IDLE)` |
| `slot_move` (dst occupied) | **add** `notify(ERROR_SLOT_OCCUPIED,dst,-1)` after `enter_typing(MOVE_PENDING)` |
| `slot_move` (save qfull) | change to `notify(ERROR_SAVE_QUEUE_FULL,dst,-1)` |
| `slot_move` (delete qfull) | change to `notify(ERROR_DELETE_QUEUE_FULL,src,-1)` |
| `slot_move` (moved) | change to **`notify(MOVED, dst, src)`** (slot2 = src) |
| `slot_preview` | keep `notify(PREVIEW_READY,idx,-1)` |
| `slot_play` (empty) | move `notify(ERROR_SLOT_EMPTY,idx,-1)` after `enter_typing(IDLE)` |
| `slot_play` (success) | keep `notify(PLAY_STARTED,idx,-1)` (after `m->state = PLAYING`) |
| `deliver_async` | move `notify(notify_evt,slot,-1)` after `enter_typing(IDLE)` |
| `typing_finished` (persist qfull) | change to `notify(ERROR_SAVE_QUEUE_FULL,persist_slot,-1)`, after `enter_typing(m->state)` |
| `dm_machine_timeout` | **add** `notify(PENDING_CANCELLED,-1,-1)` after `m->state = DM_STATE_IDLE` |

### Resulting `.state` per event (the D3 consequence)

`.state` now reports the destination/effective mode. Changed values:

| Event | `.state` before | `.state` after |
| --- | --- | --- |
| `RECORDING_STARTED` | prior state | `RECORDING` |
| `RECORDING_STOPPED` | `RECORDING` | `ASSIGN_PENDING` |
| `ERROR_OVERFLOW` | `RECORDING` | `ASSIGN_PENDING` |
| `ERROR_NO_RECORDING` | prior state | `IDLE` |
| `ERROR_SLOT_EMPTY` (chain) | `RECORDING` | `RECORDING` (unchanged) |
| `ERROR_SLOT_EMPTY` (play) | prior state | `IDLE` |
| `DELETE_PROMPT` | — (new) | `DELETE_PENDING` |
| `MOVE_PROMPT` / `MOVE_SRC_SELECTED` / `ERROR_SLOT_OCCUPIED`(move) | — | `MOVE_PENDING` |
| `PREVIEW_PROMPT` | — | `PREVIEW_PENDING` |
| `SAVED` / `DELETED` / `MOVED` / `MOVE_CANCELLED` / `PENDING_CANCELLED` | (various) | `IDLE` |
| `PLAY_STARTED` | `PLAYING` | `PLAYING` (unchanged) |

This is a deliberate semantic enrichment of `.state`; it is the reason the test
snapshots in §7 must be regenerated.

---

## 7. Settings query API

### 7.1 New public queries — `events/dynamic_macro_state_changed.h`

```c
int  dm_get_feedback_level(void);  /* current runtime feedback level (DM_FEEDBACK_*) */
int  dm_get_feedback_style(void);  /* DM_STYLE_* (0 = full, 1 = arrow) */
bool dm_get_erase_enabled(void);   /* auto-erase on/off */
```

### 7.2 Feedback accessors — `dm_feedback_pump.h` / `dm_feedback_pump.c`

`struct dm_feedback` already holds `level`, `style`, `erase_enabled`. Add:

```c
int  dm_feedback_get_level(const dm_feedback *f);
int  dm_feedback_get_style(const dm_feedback *f);
bool dm_feedback_get_erase_enabled(const dm_feedback *f);
```

### 7.3 Implementation — `dm_events.c`

Read the single instance's feedback handle, guarded by `DM_TYPING_ENABLED`:

```c
int dm_get_feedback_level(void) {
#if DM_TYPING_ENABLED
    struct dm_inst *inst = dm_shell_instance();
    return inst ? dm_feedback_get_level(&inst->feedback) : 0;
#else
    return 0; /* feedback compiled out: no runtime knob */
#endif
}
/* style / erase analogous; erase default false, style default 0 */
```
`SETTINGS_CHANGED` only ever fires when `DM_TYPING_ENABLED` (knob commands route
through `apply_knob`, which is feedback-only), so the `#else` branch is the
defensive default for builds that query without feedback.

---

## 8. Test impact

### 8.1 Log shape — regenerate every snapshot

`dm_events_raise` log line gains `slot2`:

```c
LOG_DBG("dm_event: type=%d slot=%d slot2=%d state=%d",
        (int)ev, slot, slot2, (int)st);
```
`tests/**/events.patterns` capture the tail of `dm_event: ` verbatim, so **every**
`tests/core/*/keycode_events.snapshot` and `tests/events/*/keycode_events.snapshot`
that asserts events must be regenerated from a clean run. Reasons a snapshot
changes: (a) the new `slot2=` field, (b) shifted/added event-type ordinals,
(c) changed `state=` values from §6. Regenerate, then eyeball the diff against
the §6 table — do not blind-accept.

### 8.2 New / extended event tests (`tests/events/`)

Add cases that assert the newly observable transitions:

- **move full flow**: `MOVE_PROMPT` → `MOVE_SRC_SELECTED (slot=src)` →
  `MOVED (slot=dst, slot2=src)`.
- **move cancel**: prompt → same-slot → `MOVE_CANCELLED`.
- **move dst occupied**: prompt → src → occupied dst → `ERROR_SLOT_OCCUPIED`,
  state stays `MOVE_PENDING`.
- **delete flow**: `DELETE_PROMPT (state=DELETE_PENDING)` → `DELETED`.
- **chain**: `CHAIN_INSERTED`; and a `MAX_EVENTS`-bound case → `ERROR_CHAIN_FULL`.
- **assign occupied**: stop → occupied slot → `ERROR_SLOT_OCCUPIED`, state stays
  `ASSIGN_PENDING`.
- **pending timeout**: enter assign/move/delete/preview, let the timeout fire →
  `PENDING_CANCELLED (state=IDLE)` (one case per pending state if cheap, else
  assign + move).
- **preview**: `PREVIEW_PROMPT` → `PREVIEW_READY`.
- **settings**: each knob (`DM_FEEDBACK_INC/DEC`, `DM_STYLE_TOGGLE`,
  `DM_ERASE_TOGGLE`) → `SETTINGS_CHANGED`; assert `dm_get_feedback_*` reflect it.
- **queue-full split**: force save- vs delete-queue-full → the two distinct codes.

Register each new test in `tests/README.md` (follow the existing index style).

### 8.3 Host / unit tests (`tests/unit`)

- **`map_event` exhaustiveness** is compiler-enforced (no `default`, `-Wswitch`);
  keep it that way. A new `dm_notify_code` without a mapping must fail the build.
- **`dm_machine_effective_state`**: unit test that it returns `return_state` in
  `TYPING_FEEDBACK`, `erase_return_state` in `TYPING_ERASE`, and the live state
  otherwise.
- **notify ordering / payload**: extend the machine's fake-callback tests to
  assert, per transition, the `(event, slot, slot2)` tuple and that notify fires
  after the state write (the fake can snapshot `dm_machine_state` at notify time;
  with D2/D3 the recorded effective state matches the §6 "after" column).
- Update every fake `notify` implementation to the 4-arg signature.

---

## 9. Documentation updates (`docs/event-api.md`)

- **States** table: add the four pending states; add a sentence that `.state` is
  the *effective* mode (the mode entered/returned-to once feedback finishes), and
  that `dm_get_state()` reports the same value.
- **Event Types**: add the 12 new constants with their "when raised" rows; note
  `ERROR_QUEUE_FULL` is deprecated/superseded by the SAVE/DELETE split.
- **Event Structure**: document `slot2` / `slot2_is_nvs`, and the `MOVED`
  (`slot=dst`, `slot2=src`) and `MOVE_SRC_SELECTED` conventions.
- **Query API**: add `dm_get_feedback_level` / `_style` / `dm_get_erase_enabled`.
- Add a short "deriving UI mode" note: prefer `.state` for the durable mode and
  `event` for transient toasts; `ERROR_SLOT_EMPTY` is disambiguated by `.state`
  (D5).
- README event-system bullet list: mention move/delete/preview-mode visibility
  and settings mirroring.

---

## 10. Implementation checklist (in order)

1. `dm_notify.h` — replace the enum with §4.4.
2. `events/dynamic_macro_state_changed.h` — append states (§4.1), append event
   types (§4.2), add `slot2`/`slot2_is_nvs` (§4.3), add the three settings query
   decls (§7.1).
3. `dm_machine.h` — `notify` callback gains `slot2` (§5.1); declare
   `dm_machine_effective_state` (§3 D2).
4. `dm_machine.c` — implement `dm_machine_effective_state`; update the static
   `notify` helper; apply every row of §6 (ordering + new + renamed codes).
5. `dm_feedback_pump.h/.c` — add the three feedback accessors (§7.2).
6. `dm_shell.h` / `dm_events.c` — `dm_events_raise` gains `slot2`; `cb_notify`
   forwards it; the playback emitter's `PLAY_FINISHED` call updates; rename
   `coarse_state`→`map_state` over effective state (§5.3); extend `map_event`
   (§4.5); implement the three settings queries (§7.3); update `LOG_DBG` (§8.1).
   Point `dm_get_state` at `map_state`.
7. Build native_sim with `-Wswitch` clean (it will name any unmapped code).
8. Regenerate all event snapshots (§8.1); diff against §6.
9. Add the new event tests (§8.2) and host tests (§8.3); register in
   `tests/README.md`.
10. Update docs (§9).
11. Full test suite green (core + events + feedback + unit + size builds).

### Acceptance

- A widget can render: recording (+ live count via existing poll), assign-pending,
  saved (slot, nvs/ram), delete-mode and deleted, move prompt/source/moved
  (src→dst)/cancelled, preview prompt/ready, chain inserted/empty/full, every
  error feedback can speak, and current level/style/erase — i.e. **everything the
  feedback system shows**.
- `dm_get_state()` and `event.state` always agree and report the effective mode.
- `map_event` has no `default`; adding a notify code without a mapping fails the
  build.
- No change to typed feedback output (feedback snapshots that do not assert
  events are unchanged).
