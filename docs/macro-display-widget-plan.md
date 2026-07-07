# Macro Display Widget — Intended Plan

Status: plan (no code yet)
Target: a **separate** ZMK module, forked from `zmk-nice-oled`, nice!view first
Depends on: `docs/event-system-parity-spec.md` (the expanded event/state API)

This captures the agreed design so the widget work can be picked up later. It is
a plan, not a line-by-line implementation spec — the parity spec is the
foundation it builds on.

---

## 1. Why a separate repo

ZMK modules are distributed as standalone repos added to a user's `west.yml`, so
the widget ships as its own module (working name `zmk-dynamic-macro-display`),
**not** as a subfolder of the behavior module. The starting point is a fork of
**`mctechnology17/zmk-nice-oled`** — chosen because it is modular/decoupled by
design, already targets both OLED and nice!view, and demonstrates the
composable-widget pattern we want.

**Session logistics note:** this Claude session is scoped to
`benjaminciq/zmk-dynamic-macros` only (git proxy + GitHub MCP both refuse other
repos, and there is no add-repo tool here). To do the actual build in the fork,
the fork must be added to a session's scope, or a session started against it.
This plan lives in the behavior repo until then.

---

## 2. Hard dependency: the event parity work

The widget's detailed value (pending modes, move src→dst, chain, settings
mirroring) depends on the API defined in `docs/event-system-parity-spec.md`:

- expanded `enum zmk_dynamic_macro_state` (idle / recording / playing +
  assign / move / delete / preview pending), reported as the *effective* state;
- the new event types (move prompt/src/cancel, delete/preview prompt, chain
  inserted, pending cancelled, settings changed, slot occupied, chain full,
  save/delete queue-full split);
- `slot2` on the event (move source);
- the settings queries (`dm_get_feedback_level/style`, `dm_get_erase_enabled`).

**Sequencing:** implement the parity spec first, then build the widget against
the final API. The widget reads only the public contract
(`events/dynamic_macro_state_changed.h` + the `dm_get_*` queries) — never the
internal headers (`dm_internal`, `dm_render`, `dm_query`, `dm_machine`, …).

---

## 3. Architecture — agnostic core + thin renderers

The one load-bearing idea: **separate data from rendering**, so the reusable
asset is display-independent and people on any screen/fork can copy it.

```
zmk-dynamic-macro-display/
├── zephyr/module.yml
├── Kconfig                       # CONFIG_ZMK_DM_WIDGET_* (enable + per-widget toggles)
├── boards/shields/.../           # optional ready custom_status_screen for drop-in users
└── src/
    ├── dm_display_core.c/.h      # DISPLAY-AGNOSTIC: listener + derived UI-state machine
    │                             #   depends ONLY on dynamic_macro_state_changed.h
    └── widgets/
        ├── dm_status.c/.h        # nice!view canvas: mode line + transient toast
        ├── dm_slots.c/.h         # slot-occupancy grid (NVS vs RAM) + capacity bar
        └── dm_preview.c/.h       # preview popup on PREVIEW_READY
```

### 3.1 `dm_display_core` — the copyable asset

- Subscribes to `zmk_dynamic_macro_state_changed`.
- Maintains a `dm_display_state` struct: current mode (from `.state`), live
  recording count (polled via `dm_get_recording_event_count()`), used/total
  NVS+RAM counts, last event + slot(s), a transient toast (event + expiry), and
  current feedback level/style/erase.
- Runs a small **derive-UI-state machine**: durable mode from `.state`;
  transient toasts from `event` (saved / deleted / moved src→dst / errors) with
  an auto-revert timer back to the idle summary; move flow (prompt → src → moved
  / cancelled) tracked across events.
- Exposes the state + a change callback. Pulls in **zero** display code. This is
  the file anyone copies into their own fork to write their own `render()`.

### 3.2 `widgets/*` — thin nice!view renderers

- Each renderer reads `dm_display_state` and draws; no event logic.
- Follow the `zmk_widget_*` convention (`..._init(widget, parent)` / `..._obj`)
  so they compose into zmk-nice-oled's vertical screen or any custom status
  screen.
- Use the nice!view canvas/rotated-draw idiom (as in zmk-nice-oled / nice_view).
- Each guarded by its own Kconfig so users enable only what they want.
- A generic-LVGL (label/bar) renderer is a **later** follow-up for OLED/dongle;
  the core is identical, only the render adapter differs.

---

## 4. What to display

| Widget | Source | Notes |
| --- | --- | --- |
| Mode / status line | `.state` (effective) | idle / recording / assign-pending / move prompt→src / delete-pending / preview |
| Live recording meter | `dm_get_recording_event_count()` | `N / MAX_EVENTS`, warns before overflow |
| Slot-occupancy grid | `dm_is_slot_empty()` + counts | dots/cells, NVS vs RAM visually distinct (`slot_is_nvs`) |
| Capacity bar | `dm_get_used/total_*_slots()` | used / total |
| Persistence hint | `slot_is_nvs` | saved-to-flash vs volatile (RAM lost on reboot) |
| Transient toast | `event` type | saved / deleted / moved (src→dst via `slot2`) / errors; auto-clears |
| Preview popup | `PREVIEW_READY` → `dm_get_preview_string()` | on-demand slot inspect |
| Settings indicator | `SETTINGS_CHANGED` + settings queries | mirror level / style / erase |

---

## 5. Design principles (carried from the discussion)

1. Build **only** against the public contract; never the internal headers.
2. **Separate data (core) from rendering (widgets)** — the core is the copy asset.
3. Ship **two delivery modes**: a `west.yml` module (one-line add) *and* documented
   copy-paste files for people with existing widget stacks.
4. **Kconfig-gated** and hard-guarded on
   `CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_EVENTS`; no-op cleanly if events are off.
5. **Degrade gracefully** — minimal text/line rendering works on small mono
   displays; richer grid/bar/icons where space allows.
6. Transient events need a **revert-to-idle timer** in the core (saved / deleted /
   moved / errors are momentary, not durable states).

---

## 6. Rough phases

1. **Parity API** — implement `docs/event-system-parity-spec.md` (separate work,
   behavior repo, redesign branch).
2. **Fork + skeleton** — fork `zmk-nice-oled`; add `module.yml`, `Kconfig`,
   `dm_display_core` stub + one nice!view `dm_status` widget; build against the
   parity API.
3. **Core** — implement the derive-UI-state machine + `dm_display_state`.
4. **Widgets** — `dm_status`, then `dm_slots`, then `dm_preview`; each Kconfig-toggled.
5. **Docs** — README with both delivery modes, a `west.yml` snippet, and a
   "copy `dm_display_core` + write your own render()" guide; license check on the
   zmk-nice-oled base before publishing.
6. **Generic LVGL renderer** — follow-up for OLED/dongle/prospector.

---

## 7. Open items

- Add the forked repo to a session scope (or start a session against it) before
  build can begin.
- Confirm final module/repo name.
- Confirm `zmk-nice-oled` license is compatible with redistribution as the base.
