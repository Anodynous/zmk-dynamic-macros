# Event System API Reference

The dynamic macro event system provides notifications and query functions for display widgets and custom integrations.

## Configuration

Enable in your `.conf`:

```ini
CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_EVENTS=y
```

When disabled, no events are raised and query functions are not compiled.

## Events

### Event Structure

```c
struct zmk_dynamic_macro_state_changed {
    enum zmk_dynamic_macro_state state;
    enum zmk_dynamic_macro_event_type event;
    int slot;
    int slot2;
    bool slot_is_nvs;
    bool slot2_is_nvs;
};
```

| Field         | Description                                                      |
| ------------- | --------------------------------------------------------------- |
| `state`       | **Effective** macro state after the event (see below)           |
| `event`       | What triggered this notification                                |
| `slot`        | Primary slot index, or -1 if not applicable                     |
| `slot2`       | Secondary slot index, or -1. Used by `MOVED` (move source)      |
| `slot_is_nvs` | True if `slot` is NVS, false if RAM (valid when `slot >= 0`)     |
| `slot2_is_nvs`| True if `slot2` is NVS, false if RAM (valid when `slot2 >= 0`)   |

For `MOVED`, `slot` is the destination and `slot2` is the source. For
`MOVE_SRC_SELECTED`, `slot` is the selected source and `slot2` is -1. Every other
event sets `slot2 = -1`.

### States

`state` is the *effective* mode — the mode the machine has settled into (or is
settling into once its feedback finishes typing), never the transient internal
typing state. `dm_get_state()` reports the same value, so polling and events
always agree.

| State                                       | Meaning                              |
| ------------------------------------------- | ------------------------------------ |
| `ZMK_DYNAMIC_MACRO_STATE_IDLE`              | No operation in progress             |
| `ZMK_DYNAMIC_MACRO_STATE_RECORDING`         | Currently recording                  |
| `ZMK_DYNAMIC_MACRO_STATE_PLAYING`           | Playing back a macro                 |
| `ZMK_DYNAMIC_MACRO_STATE_ASSIGN_PENDING`    | Recording stopped, awaiting a slot   |
| `ZMK_DYNAMIC_MACRO_STATE_MOVE_PENDING`      | Move mode: awaiting source or dest   |
| `ZMK_DYNAMIC_MACRO_STATE_DELETE_PENDING`    | Delete mode: awaiting a slot         |
| `ZMK_DYNAMIC_MACRO_STATE_PREVIEW_PENDING`   | Preview mode: awaiting a slot        |

### Event Types

#### Normal Events

| Event                               | When raised                          |
| ----------------------------------- | ------------------------------------ |
| `ZMK_DYNAMIC_MACRO_RECORDING_STARTED` | Recording began                    |
| `ZMK_DYNAMIC_MACRO_RECORDING_STOPPED` | Recording stopped (before save)    |
| `ZMK_DYNAMIC_MACRO_SAVED`             | Macro saved to slot                |
| `ZMK_DYNAMIC_MACRO_DELETED`           | Slot cleared                       |
| `ZMK_DYNAMIC_MACRO_MOVED`             | Macro moved (`slot`=dst, `slot2`=src) |
| `ZMK_DYNAMIC_MACRO_PLAY_STARTED`      | Playback began                     |
| `ZMK_DYNAMIC_MACRO_PLAY_FINISHED`     | Playback completed                 |
| `ZMK_DYNAMIC_MACRO_PREVIEW_READY`     | Preview data available for query   |

#### Mode / Transition Events

| Event                                  | When raised                          |
| -------------------------------------- | ------------------------------------ |
| `ZMK_DYNAMIC_MACRO_MOVE_PROMPT`        | Entered move mode                    |
| `ZMK_DYNAMIC_MACRO_MOVE_SRC_SELECTED`  | Move source selected (`slot`=source) |
| `ZMK_DYNAMIC_MACRO_MOVE_CANCELLED`     | Move cancelled (same-slot)           |
| `ZMK_DYNAMIC_MACRO_DELETE_PROMPT`      | Entered delete mode                  |
| `ZMK_DYNAMIC_MACRO_PREVIEW_PROMPT`     | Entered preview mode                 |
| `ZMK_DYNAMIC_MACRO_CHAIN_INSERTED`     | Slot chained into the draft          |
| `ZMK_DYNAMIC_MACRO_PENDING_CANCELLED`  | A pending mode timed out → IDLE      |
| `ZMK_DYNAMIC_MACRO_SETTINGS_CHANGED`   | Feedback level / style / erase changed |

`SETTINGS_CHANGED` carries no value; read the current settings with the query API
below.

#### Error Events

| Event                                     | Meaning                           |
| ----------------------------------------- | --------------------------------- |
| `ZMK_DYNAMIC_MACRO_ERROR_OVERFLOW`        | Recording buffer full             |
| `ZMK_DYNAMIC_MACRO_ERROR_CHAIN_FULL`      | Chain would exceed the slot limit |
| `ZMK_DYNAMIC_MACRO_ERROR_SAVE_FAILED`     | NVS write failed                  |
| `ZMK_DYNAMIC_MACRO_ERROR_DELETE_FAILED`   | NVS delete failed                 |
| `ZMK_DYNAMIC_MACRO_ERROR_SAVE_QUEUE_FULL` | Save queue full, retry later      |
| `ZMK_DYNAMIC_MACRO_ERROR_DELETE_QUEUE_FULL`| Delete queue full, retry later   |
| `ZMK_DYNAMIC_MACRO_ERROR_SLOT_EMPTY`      | Operation on an empty slot        |
| `ZMK_DYNAMIC_MACRO_ERROR_SLOT_OCCUPIED`   | Assign/move target not empty      |
| `ZMK_DYNAMIC_MACRO_ERROR_NO_RECORDING`    | Stop pressed with no recording    |

> `ZMK_DYNAMIC_MACRO_ERROR_QUEUE_FULL` is **deprecated** — it is retained for
> source compatibility but no longer raised. Use the `SAVE`/`DELETE` queue-full
> split above.

`ERROR_SLOT_EMPTY` covers several cases (failed chain, empty move source, empty
delete, empty play); disambiguate from `state` — e.g. `ERROR_SLOT_EMPTY` while
`state == RECORDING` is a failed chain.

## Subscribing to Events

```c
#include <zmk-behavior-dynamic-macros/events/dynamic_macro_state_changed.h>

static int my_macro_listener(const zmk_event_t *eh) {
    const struct zmk_dynamic_macro_state_changed *ev = as_zmk_dynamic_macro_state_changed(eh);
    
    switch (ev->event) {
    case ZMK_DYNAMIC_MACRO_RECORDING_STARTED:
        // Update display to show recording indicator
        break;
    case ZMK_DYNAMIC_MACRO_SAVED:
        // Refresh slot display, ev->slot has the saved slot
        break;
    case ZMK_DYNAMIC_MACRO_PREVIEW_READY:
        // Fetch preview data for ev->slot
        char buf[64];
        dm_get_preview_string(ev->slot, buf, sizeof(buf));
        break;
    // ... handle other events
    }
    
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(my_macro_widget, my_macro_listener);
ZMK_SUBSCRIPTION(my_macro_widget, zmk_dynamic_macro_state_changed);
```

## Query API

Events notify that something changed. Query functions retrieve current state. Call these from your event handler or at any time.

### State Queries

#### `dm_get_state()`

```c
enum zmk_dynamic_macro_state dm_get_state(void);
```

Returns the current **effective** state — the same value reported in each event's
`.state` field. This includes the pending modes (`ASSIGN_PENDING`,
`MOVE_PENDING`, `DELETE_PENDING`, `PREVIEW_PENDING`) in addition to `IDLE`,
`RECORDING`, and `PLAYING`.

Prefer `.state` (or `dm_get_state()`) for the durable mode a widget renders, and
`event` for transient toasts.

#### `dm_get_recording_event_count()`

```c
uint32_t dm_get_recording_event_count(void);
```

Returns number of events captured in current recording. Returns 0 if not recording.

### Settings Queries

Read on a `SETTINGS_CHANGED` event to mirror the runtime feedback knobs (the
value is not encoded in the event). When feedback is compiled out these return
defaults (level 0, style 0, erase false).

#### `dm_get_feedback_level()`

```c
int dm_get_feedback_level(void);
```

Current feedback verbosity level (`DM_FB_LEVEL_OFF`..`DM_FB_LEVEL_VERBOSE`).

#### `dm_get_feedback_style()`

```c
int dm_get_feedback_style(void);
```

Current feedback style: 0 = full, 1 = arrow.

#### `dm_get_erase_enabled()`

```c
bool dm_get_erase_enabled(void);
```

True if auto-erase is enabled.

### Slot Queries

#### `dm_is_slot_empty()`

```c
bool dm_is_slot_empty(int slot_idx);
```

Returns true if slot has no macro stored.

**Parameters:**
- `slot_idx`: Slot index (0 to NVS_SLOTS-1 for NVS, NVS_SLOTS to MAX_SLOTS-1 for RAM)

#### `dm_get_used_nvs_slots()`

```c
int dm_get_used_nvs_slots(void);
```

Returns count of non-empty NVS slots.

#### `dm_get_used_ram_slots()`

```c
int dm_get_used_ram_slots(void);
```

Returns count of non-empty RAM slots.

#### `dm_get_total_nvs_slots()`

```c
int dm_get_total_nvs_slots(void);
```

Returns total configured NVS slots (`CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_NVS_SLOTS`).

#### `dm_get_total_ram_slots()`

```c
int dm_get_total_ram_slots(void);
```

Returns total configured RAM slots (`CONFIG_ZMK_BEHAVIOR_DYNAMIC_MACRO_RAM_SLOTS`).

### Content Queries

#### `dm_get_preview_string()`

```c
int dm_get_preview_string(int slot_idx, char *buf, size_t len);
```

Writes a human-readable preview of slot contents to buffer.

**Parameters:**
- `slot_idx`: Slot index
- `buf`: Output buffer
- `len`: Buffer size

**Returns:** Number of characters written, or 0 if slot is empty/invalid.

**Example output:** `"Hello World"` or `"Ctrl+C Ctrl+V"`

#### `dm_get_slot_events()`

```c
const struct dm_event *dm_get_slot_events(int slot_idx, uint32_t *count);
```

Returns pointer to raw event array for a slot.

**Parameters:**
- `slot_idx`: Slot index
- `count`: Output parameter, receives event count

**Returns:** Pointer to event array, or NULL if slot empty/invalid. The returned pointer points directly into the slot buffer and is only valid until the next macro operation (save, delete, or move) on that slot. Copy the data if you need it to persist.

**Event structure:**
```c
struct dm_event {
    uint16_t usage_page;
    uint16_t keycode;
    uint8_t implicit_mods;
    uint8_t explicit_mods;
    uint8_t pressed;
    uint8_t _reserved;
};
```

## Preview Mode

To inspect a slot's contents:

1. User presses `&dm DM_PREVIEW 0`
2. User presses a slot key
3. System raises `ZMK_DYNAMIC_MACRO_PREVIEW_READY` with the slot index
4. Widget calls `dm_get_preview_string()` or `dm_get_slot_events()`

This allows widgets to show slot contents on demand without constantly polling.

## Thread Safety

Query functions read shared state. In ZMK's cooperative threading model, this is safe when called from event handlers or work queue items. The event tells you state changed; the query reads current state at call time.

## Example: Display Widget

```c
#include <zmk-behavior-dynamic-macros/events/dynamic_macro_state_changed.h>

static void update_macro_display(void) {
    enum zmk_dynamic_macro_state state = dm_get_state();
    int nvs_used = dm_get_used_nvs_slots();
    int ram_used = dm_get_used_ram_slots();
    
    // Update your display with current state
    display_set_macro_state(state);
    display_set_slot_counts(nvs_used, ram_used);
}

static int macro_display_listener(const zmk_event_t *eh) {
    const struct zmk_dynamic_macro_state_changed *ev = as_zmk_dynamic_macro_state_changed(eh);
    
    if (ev->event == ZMK_DYNAMIC_MACRO_PREVIEW_READY) {
        char preview[64];
        dm_get_preview_string(ev->slot, preview, sizeof(preview));
        display_show_preview(ev->slot, preview);
    } else {
        update_macro_display();
    }
    
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(macro_display, macro_display_listener);
ZMK_SUBSCRIPTION(macro_display, zmk_dynamic_macro_state_changed);
```
