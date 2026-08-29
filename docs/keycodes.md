# Behavior Binding Reference

All `&dm` bindings follow the form `&dm <COMMAND> <param2>`.

`COMMAND` is one of the constants below, defined in
`dt-bindings/zmk/dynamic_macros.h`. `param2` is either a slot index (for slot
commands) or `0` for commands that don't use it — the compiler enforces this.

## Quick Reference

| Constant | Value | param2 | Summary |
| -------- | :---: | ------ | ------- |
| `DM_REC` | 0 | `0` | Start recording |
| `DM_STP` | 1 | `0` | Stop recording |
| `DM_DEL` | 2 | `0` | Enter delete mode |
| `DM_STATE` | 3 | `0` | Output slot status |
| `DM_MOV` | 4 | `0` | Enter move mode |
| `DM_SLOT_NVS` | 5 | slot index | Activate NVS slot N |
| `DM_SLOT_RAM` | 6 | slot index | Activate RAM slot N |
| `DM_PREVIEW` | 7 | `0` | Enter preview mode (requires `EVENTS`) |
| `DM_FEEDBACK_INC` | 8 | `0` | Increase feedback verbosity |
| `DM_FEEDBACK_DEC` | 9 | `0` | Decrease feedback verbosity |
| `DM_STYLE_TOGGLE` | 10 | `0` | Toggle FULL / ARROW feedback style |
| `DM_ERASE_TOGGLE` | 11 | `0` | Toggle auto-erase on / off |

---

## Recording

### `DM_REC` — Start recording

```dts
&dm DM_REC 0
```

Transitions from IDLE to RECORDING. All subsequent keystrokes (from either
half of a split keyboard) are captured into the recording buffer until
`DM_STP` is pressed.

Pressing `DM_REC` while already recording is a no-op.

**Feedback:** `[DM REC]` / `>*`

---

### `DM_STP` — Stop recording

```dts
&dm DM_STP 0
```

Ends the current recording and transitions to ASSIGN_PENDING, waiting for a
slot key to save to. If pressed with no active recording the press is ignored.

**Feedback:** `[DM STOP]` / `>.`

---

## Slot Commands

Slot commands are context-sensitive: their effect depends on the current mode.

| Mode | Effect |
| ---- | ------ |
| IDLE | Play back the macro stored in the slot |
| ASSIGN_PENDING (after STOP) | Save the recording to this slot |
| DELETE_PENDING (after DEL) | Delete the macro in this slot |
| MOVE_PENDING — source step | Select this slot as the move source |
| MOVE_PENDING — destination step | Move the previously selected slot here |
| PREVIEW_PENDING (after PREVIEW) | Raise `PREVIEW_READY` event for this slot |

Pressing a slot key while the slot is full during ASSIGN/MOVE destination will
produce a "slot full" message and leave the pending mode active.

### `DM_SLOT_NVS` — NVS (persistent) slot

```dts
&dm DM_SLOT_NVS <index>
```

`index` must be in the range `0` to `NVS_SLOTS - 1` (default max 7). The
compiler emits a build error if the index is out of range. Internal slot label
uses the `N` prefix: `N0`, `N1`, …

**Feedback examples (playback):** none by default; errors produce `[DM SLOT N0: -]` / `?N0`

**Feedback examples (save):** `[DM SAVED N0]` or `[DM SAVED N0: 'Hello']` (VERBOSE) / `>N0` or `>N0:'Hello'`

---

### `DM_SLOT_RAM` — RAM (temporary) slot

```dts
&dm DM_SLOT_RAM <index>
```

`index` must be in the range `0` to `RAM_SLOTS - 1` (default max 7). RAM slots
are lost on reboot. Internal slot label uses the `R` prefix: `R8`, `R9`, … (the
`R` index equals `NVS_SLOTS + RAM index`).

**Feedback:** same patterns as NVS slots, with the `R` prefix.

---

## Mode Commands

### `DM_DEL` — Enter delete mode

```dts
&dm DM_DEL 0
```

Transitions to DELETE_PENDING. The next slot key pressed will delete that
slot's macro. The mode auto-cancels after `ASSIGN_TIMEOUT` ms if no slot is
pressed.

Pressing `DM_DEL` while not idle is a no-op.

**Feedback on deletion:** `[DM DEL N0]` / `-N0`

---

### `DM_MOV` — Enter move mode

```dts
&dm DM_MOV 0
```

Transitions to MOVE_PENDING. Requires two slot presses: first selects the
source (must be non-empty), second selects the destination (must be empty).
Auto-cancels after `ASSIGN_TIMEOUT` ms.

**Feedback:** `[DM MOV]` → `[DM MOV SRC N0]` → `[DM MOV N0->N1]` / `<>` → `<>N0` → `>N0>>N1`

---

## Information Commands

### `DM_STATE` — Output slot status

```dts
&dm DM_STATE 0
```

Types a summary of all slot contents to the focused window. The detail level
is controlled by the `STATUS_*` Kconfig options. Output is a single line (no
trailing newline); the header and each macro are joined by the configurable
`STATUS_HEADER_SEP` (default `" || "`) and `STATUS_SLOT_SEP` (default `" | "`)
separators.

**Example output (FULL style, VERBOSE):**
```
[DM 2/8 NVS:0-7 RAM:8-15] || N0: 'Hello' (10) | N1: 42
```

---

### `DM_PREVIEW` — Enter preview mode

```dts
&dm DM_PREVIEW 0
```

**Requires:** `CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_EVENTS=y`

Transitions to PREVIEW_PENDING. The next slot key pressed raises a
`ZMK_DYNAMIC_MACRO_PREVIEW_READY` event carrying the slot index. A display
widget can then call `dm_get_preview_string()` to fetch the slot contents
without typing anything to the host. See [event-api.md](event-api.md) for
integration details.

---

## Runtime Settings

These four commands let you adjust feedback behaviour on the fly without
reflashing. All changes are **persisted across reboots** and only take effect
when **idle** (presses during recording or pending modes are ignored).

---

### `DM_FEEDBACK_INC` — Increase feedback verbosity

```dts
&dm DM_FEEDBACK_INC 0
```

Steps the feedback level up by one:

```
ERROR → COMMAND → BASIC → VERBOSE
```

The ceiling is VERBOSE; pressing again at VERBOSE has no effect. The
`FEEDBACK_OFF` level is only available at build time — it cannot be reached
at runtime.

**Feedback:** `[DM FB:VERBOSE]` / `>FB:VERBOSE` (or whichever level was just set)

---

### `DM_FEEDBACK_DEC` — Decrease feedback verbosity

```dts
&dm DM_FEEDBACK_DEC 0
```

Steps the feedback level down by one:

```
VERBOSE → BASIC → COMMAND → ERROR
```

The floor is ERROR; pressing again at ERROR has no effect.

**Feedback:** `[DM FB:ERROR]` / `>FB:ERROR` (or whichever level was just set)

---

### `DM_STYLE_TOGGLE` — Toggle FULL / ARROW feedback style

```dts
&dm DM_STYLE_TOGGLE 0
```

Toggles between the two feedback styles:

| Style | Example |
| ----- | ------- |
| FULL  | `[DM SAVED N0: 'Hello']` |
| ARROW | `>N0:'Hello'` |

**Requires a full-punctuation locale (US, UK, or FI).** The ARROW style uses
punctuation characters (`>`, `-`, `!`, `?`, `%`) that are not available in
DE/FR plain mode. Pressing this key on a DE or FR build is a no-op.

**Feedback:** `[DM FB:ARROW]` or `>FB:FULL` (typed in the style just switched *to*)

---

### `DM_ERASE_TOGGLE` — Toggle auto-erase

```dts
&dm DM_ERASE_TOGGLE 0
```

Toggles the auto-erase feature on or off. When enabled, feedback text is
automatically erased by emitting backspace keycodes after `FEEDBACK_ERASE_DELAY`
ms. Typing before the delay expires cancels the erase. Status output is excluded
from auto-erase (it is informational and may be long). Pairs well with ARROW
style's compact output.

**Feedback:** `[DM FB:ERASE ON]` / `[DM FB:ERASE OFF]` (or `>FB:ERASE ON` / `>FB:ERASE OFF`)
