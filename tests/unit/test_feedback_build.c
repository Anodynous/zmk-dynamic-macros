/*
 * Copyright (c) 2026 Benjamin H
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests for dm_feedback_build — the pure message-builder core.
 *
 * The builder emits fb_events (keycode+mods). To assert against the human
 * message strings, a decoding sink maps each fb_event back to the character it
 * types on the given locale (the inverse of the builder's ascii_to_hid), so a US
 * "[DM SAVED N3" asserts as that literal. This pins the output string-for-string
 * while exercising the actual keycode path.
 */

#include "ztest_shim.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zmk-behavior-dynamic-macros/dm_feedback_build.h>

/* ---- decoding sink: fb_event -> char (US/UK/FI reverse map) --------------- */

struct decode_sink {
    dm_fb_sink sink;
    dm_locale  locale;
    char       buf[256];
    int        len;
    int        cap;
};

/* Inverse of ascii_to_hid for the locales under test (US/UK/FI). Enough of the
 * map to decode the message strings + previews the tests assert. */
static char decode(dm_locale locale, uint16_t kc, uint8_t mods) {
    bool shift = (mods & 0x02) != 0;
    bool altgr = (mods & 0x04) != 0;
    bool uk    = (locale == DM_LOCALE_UK);
    bool fi    = (locale == DM_LOCALE_FI);

    if (fi && altgr) {
        /* FI AltGr level (inverse of ascii_to_hid_fi's AltGr entries). */
        switch (kc) {
        case 0x1F: return '@';
        case 0x21: return '$';
        case 0x24: return '{';
        case 0x25: return '[';
        case 0x26: return ']';
        case 0x27: return '}';
        case 0x2D: return '\\';
        case 0x30: return '~';
        case 0x64: return '|';
        default:   return '?';
        }
    }

    if (fi) {
        if (kc >= 0x04 && kc <= 0x1D) {
            return (char)((shift ? 'A' : 'a') + (kc - 0x04));
        }
        if (kc >= 0x1E && kc <= 0x27) {
            static const char n[] = "1234567890";
            static const char sFI[] = "!\"#?%&/()="; /* ? = non-ASCII currency */
            if (!shift) {
                return n[kc - 0x1E];
            }
            return sFI[kc - 0x1E];
        }
        switch (kc) {
        case 0x2C: return ' ';
        case 0x28: return '\n';
        case 0x2D: return shift ? '?' : '+';
        case 0x2E: return shift ? '`' : '?';
        case 0x2F: return '?';
        case 0x30: return shift ? '^' : '?';
        case 0x32: return shift ? '*' : '\'';
        case 0x33: return shift ? ';' : '?';
        case 0x34: return shift ? ':' : '?';
        case 0x35: return '?';
        case 0x36: return shift ? ';' : ',';
        case 0x37: return shift ? ':' : '.';
        case 0x38: return shift ? '_' : '-';
        case 0x64: return shift ? '>' : '<';
        default:   return '?';
        }
    }

    if (kc >= 0x04 && kc <= 0x1D) {
        return (char)((shift ? 'A' : 'a') + (kc - 0x04));
    }
    if (kc >= 0x1E && kc <= 0x27) {
        static const char n[] = "1234567890";
        static const char sUS[] = "!@#$%^&*()";
        if (!shift) {
            return n[kc - 0x1E];
        }
        if (uk && kc == 0x1F) return '"';
        return sUS[kc - 0x1E];
    }
    switch (kc) {
    case 0x2C: return ' ';
    case 0x28: return '\n';
    case 0x2D: return shift ? '_' : '-';
    case 0x2E: return shift ? '+' : '=';
    case 0x2F: return shift ? '{' : '[';
    case 0x30: return shift ? '}' : ']';
    case 0x33: return shift ? ':' : ';';
    case 0x36: return shift ? '<' : ',';
    case 0x37: return shift ? '>' : '.';
    case 0x38: return shift ? '?' : '/';
    case 0x31: return shift ? '|' : '\\'; /* US */
    case 0x32: return shift ? '~' : '#';  /* UK */
    case 0x34: return uk ? (shift ? '@' : '\'') : (shift ? '"' : '\'');
    case 0x35: return shift ? '~' : '`';
    case 0x64: return shift ? '|' : '\\'; /* UK ISO */
    default:   return '?';
    }
}

static void decode_emit(void *ctx, uint16_t kc, uint8_t mods) {
    struct decode_sink *d = ctx;
    if (d->len < d->cap - 1) {
        d->buf[d->len++] = decode(d->locale, kc, mods);
        d->buf[d->len] = '\0';
    }
}

static bool decode_space(void *ctx, uint8_t n) {
    struct decode_sink *d = ctx;
    return d->len + n < d->cap - 1;
}

static void ds_init(struct decode_sink *d, dm_locale locale) {
    memset(d, 0, sizeof(*d));
    d->locale = locale;
    d->cap = (int)sizeof(d->buf);
    d->sink.emit = decode_emit;
    d->sink.space_for = decode_space;
    d->sink.ctx = d;
}

/* US-ish facts: 8 NVS + 8 RAM, like the host dm_config defaults. */
static dm_fb_facts facts_default(void) {
    return (dm_fb_facts){
        .filled_count = 0, .nvs_slots = 8, .max_slots = 16,
        .preview_event_count = 0, .slot_is_empty = false,
        .header_sep = " || ", .slot_sep = " | ", .status_first_slot = false,
    };
}

/* ---- scaffolding messages, US FULL style ---------------------------------- */

ZTEST(dm_feedback_build, rec_full_us) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_US);
    dm_feedback_spec spec = {.kind = DM_FB_REC, .slot = -1, .slot2 = -1};
    dm_fb_facts f = facts_default();
    bool preview = dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_US, &f, &d.sink);
    zassert_false(preview, NULL);
    zassert_str_equal(d.buf, "[DM REC]", NULL);
}

ZTEST(dm_feedback_build, saved_no_preview_full_us) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_US);
    dm_feedback_spec spec = {.kind = DM_FB_SAVED, .slot = 3, .slot2 = -1, .show_preview = false};
    dm_fb_facts f = facts_default();
    bool preview = dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_US, &f, &d.sink);
    zassert_false(preview, NULL);
    zassert_str_equal(d.buf, "[DM SAVED N3]", NULL);
}

ZTEST(dm_feedback_build, saved_ram_slot_prefix) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_US);
    dm_feedback_spec spec = {.kind = DM_FB_SAVED, .slot = 10, .slot2 = -1, .show_preview = false};
    dm_fb_facts f = facts_default(); /* nvs_slots=8 -> slot 10 is RAM */
    dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_US, &f, &d.sink);
    zassert_str_equal(d.buf, "[DM SAVED R10]", NULL);
}

ZTEST(dm_feedback_build, slot_empty_full_us) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_US);
    dm_feedback_spec spec = {.kind = DM_FB_SLOT_EMPTY, .slot = 2, .slot2 = -1};
    dm_fb_facts f = facts_default();
    dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_US, &f, &d.sink);
    zassert_str_equal(d.buf, "[DM SLOT N2: -]", NULL);
}

ZTEST(dm_feedback_build, slot_full_full_us) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_US);
    dm_feedback_spec spec = {.kind = DM_FB_SLOT_FULL, .slot = 4, .slot2 = -1};
    dm_fb_facts f = facts_default();
    dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_US, &f, &d.sink);
    zassert_str_equal(d.buf, "[DM SLOT N4 FULL]", NULL);
}

ZTEST(dm_feedback_build, moved_full_us) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_US);
    dm_feedback_spec spec = {.kind = DM_FB_MOVED, .slot = 1, .slot2 = 9};
    dm_fb_facts f = facts_default();
    dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_US, &f, &d.sink);
    zassert_str_equal(d.buf, "[DM MOV N1->R9]", NULL);
}

ZTEST(dm_feedback_build, delete_failed_full_us) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_US);
    dm_feedback_spec spec = {.kind = DM_FB_DELETE_FAILED, .slot = 5, .slot2 = -1};
    dm_fb_facts f = facts_default();
    dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_US, &f, &d.sink);
    zassert_str_equal(d.buf, "[DM DEL N5 FAILED]", NULL);
}

ZTEST(dm_feedback_build, save_queue_full_full_us) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_US);
    dm_feedback_spec spec = {.kind = DM_FB_SAVE_QFULL, .slot = 0, .slot2 = -1};
    dm_fb_facts f = facts_default();
    dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_US, &f, &d.sink);
    zassert_str_equal(d.buf, "[DM SAVE QUEUE FULL N0]", NULL);
}

ZTEST(dm_feedback_build, knob_full_us) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_US);
    dm_feedback_spec spec = {.kind = DM_FB_KNOB, .slot = -1, .slot2 = -1, .knob_text = "VERBOSE"};
    dm_fb_facts f = facts_default();
    dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_US, &f, &d.sink);
    zassert_str_equal(d.buf, "[DM FB:VERBOSE]", NULL);
}

/* ---- ARROW style ---------------------------------------------------------- */

ZTEST(dm_feedback_build, rec_arrow) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_US);
    dm_feedback_spec spec = {.kind = DM_FB_REC, .slot = -1, .slot2 = -1};
    dm_fb_facts f = facts_default();
    dm_feedback_build(&spec, DM_FB_STYLE_ARROW, DM_LOCALE_US, &f, &d.sink);
    zassert_str_equal(d.buf, ">*", NULL);
}

ZTEST(dm_feedback_build, saved_arrow_slot) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_US);
    dm_feedback_spec spec = {.kind = DM_FB_SAVED, .slot = 3, .slot2 = -1, .show_preview = false};
    dm_fb_facts f = facts_default();
    dm_feedback_build(&spec, DM_FB_STYLE_ARROW, DM_LOCALE_US, &f, &d.sink);
    /* arrow saved = ">", slot ref "N3", close "" */
    zassert_str_equal(d.buf, ">N3", NULL);
}

ZTEST(dm_feedback_build, slot_empty_arrow) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_US);
    dm_feedback_spec spec = {.kind = DM_FB_SLOT_EMPTY, .slot = 2, .slot2 = -1};
    dm_fb_facts f = facts_default();
    dm_feedback_build(&spec, DM_FB_STYLE_ARROW, DM_LOCALE_US, &f, &d.sink);
    zassert_str_equal(d.buf, "?N2", NULL);
}

/* ---- status header -------------------------------------------------------- */

ZTEST(dm_feedback_build, status_header_full_us) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_US);
    dm_feedback_spec spec = {.kind = DM_FB_STATUS_HEADER, .slot = -1, .slot2 = -1};
    dm_fb_facts f = facts_default();
    f.filled_count = 2;
    dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_US, &f, &d.sink);
    /* single-line: header carries no trailing newline (slots are joined after it) */
    zassert_str_equal(d.buf, "[DM 2/16 NVS:0-7 RAM:8-15]", NULL);
}

ZTEST(dm_feedback_build, status_header_arrow) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_US);
    dm_feedback_spec spec = {.kind = DM_FB_STATUS_HEADER, .slot = -1, .slot2 = -1};
    dm_fb_facts f = facts_default();
    f.filled_count = 2;
    dm_feedback_build(&spec, DM_FB_STYLE_ARROW, DM_LOCALE_US, &f, &d.sink);
    zassert_str_equal(d.buf, "==2/16 N0-7 R8-15", NULL);
}

/* ---- status slot lines ---------------------------------------------------- */

/* The FIRST slot after the header leads with header_sep (" || "), no trailing \n. */
ZTEST(dm_feedback_build, status_slot_empty_first_leads_header_sep) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_US);
    dm_feedback_spec spec = {.kind = DM_FB_STATUS_SLOT, .slot = 4, .slot2 = -1};
    dm_fb_facts f = facts_default();
    f.slot_is_empty = true;
    f.status_first_slot = true;
    bool preview = dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_US, &f, &d.sink);
    zassert_false(preview, NULL);
    zassert_str_equal(d.buf, " || N4: -", NULL);
}

/* A LATER slot leads with slot_sep (" | "), no trailing \n. */
ZTEST(dm_feedback_build, status_slot_count_later_leads_slot_sep) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_US);
    dm_feedback_spec spec = {.kind = DM_FB_STATUS_SLOT, .slot = 4, .slot2 = -1, .show_preview = false};
    dm_fb_facts f = facts_default();
    f.slot_is_empty = false;
    f.preview_event_count = 7;
    f.status_first_slot = false;
    dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_US, &f, &d.sink);
    zassert_str_equal(d.buf, " | N4: 7", NULL);
}

/* A custom separator (e.g. a user setting slot_sep to "\n") flows through. */
ZTEST(dm_feedback_build, status_slot_honours_custom_separator) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_US);
    dm_feedback_spec spec = {.kind = DM_FB_STATUS_SLOT, .slot = 4, .slot2 = -1, .show_preview = false};
    dm_fb_facts f = facts_default();
    f.slot_sep = "\n";
    f.preview_event_count = 7;
    f.status_first_slot = false;
    dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_US, &f, &d.sink);
    zassert_str_equal(d.buf, "\nN4: 7", NULL);
}

/* ---- preview streaming: SAVED with a literal 'ab' macro ------------------- */

static struct dm_event key(uint16_t kc, uint8_t imp, uint8_t exp, uint8_t pressed) {
    struct dm_event e = {0};
    e.usage_page = 0x07;
    e.keycode = kc;
    e.implicit_mods = imp;
    e.explicit_mods = exp;
    e.pressed = pressed;
    return e;
}

ZTEST(dm_feedback_build, saved_with_preview_literal) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_US);
    dm_feedback_spec spec = {.kind = DM_FB_SAVED, .slot = 3, .slot2 = -1, .show_preview = true};
    dm_fb_facts f = facts_default();
    f.preview_event_count = 2;

    bool preview = dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_US, &f, &d.sink);
    zassert_true(preview, NULL);
    /* scaffolding so far: "[DM SAVED N3: '" */
    zassert_str_equal(d.buf, "[DM SAVED N3: '", NULL);

    struct dm_event evs[] = {
        key(0x04, 0, 0, 1), key(0x04, 0, 0, 0), /* a */
        key(0x05, 0, 0, 1), key(0x05, 0, 0, 0), /* b */
    };
    dm_render_slot_view view = {.event_count = 4, .events = evs};
    bool done = dm_feedback_build_preview(&view, DM_LOCALE_US, &d.sink, NULL);
    zassert_true(done, NULL);
    dm_feedback_build_preview_suffix(&spec, DM_FB_STYLE_FULL, DM_LOCALE_US, &f, &d.sink);
    zassert_str_equal(d.buf, "[DM SAVED N3: 'ab']", NULL);
}

ZTEST(dm_feedback_build, saved_with_preview_ctrl_token) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_US);
    dm_feedback_spec spec = {.kind = DM_FB_SAVED, .slot = 0, .slot2 = -1, .show_preview = true};
    dm_fb_facts f = facts_default();
    f.preview_event_count = 1;

    dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_US, &f, &d.sink);
    /* Ctrl held + C -> <LCTL+C> token (rendered as chars, re-encoded to keys) */
    struct dm_event evs[] = {
        key(0xE0, 0, 0, 1),         /* LCTL down */
        key(0x06, 0, 0x01, 1),      /* C with ctrl */
        key(0x06, 0, 0x01, 0),
        key(0xE0, 0, 0, 0),
    };
    dm_render_slot_view view = {.event_count = 4, .events = evs};
    dm_feedback_build_preview(&view, DM_LOCALE_US, &d.sink, NULL);
    dm_feedback_build_preview_suffix(&spec, DM_FB_STYLE_FULL, DM_LOCALE_US, &f, &d.sink);
    zassert_str_equal(d.buf, "[DM SAVED N0: '<LCTL+C>']", NULL);
}

/* A STATUS_SLOT with a preview: leads with its separator, ends with " (N)" and
 * NO trailing newline (single-line status). */
ZTEST(dm_feedback_build, status_slot_preview_line_single_line) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_US);
    dm_feedback_spec spec = {.kind = DM_FB_STATUS_SLOT, .slot = 4, .slot2 = -1, .show_preview = true};
    dm_fb_facts f = facts_default();
    f.slot_is_empty = false;
    f.preview_event_count = 2;
    f.status_first_slot = false; /* a later slot -> leads with slot_sep */

    bool preview = dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_US, &f, &d.sink);
    zassert_true(preview, NULL);
    /* scaffolding so far: " | N4: '" */
    zassert_str_equal(d.buf, " | N4: '", NULL);

    struct dm_event evs[] = {
        key(0x04, 0, 0, 1), key(0x04, 0, 0, 0), /* a */
        key(0x05, 0, 0, 1), key(0x05, 0, 0, 0), /* b */
    };
    dm_render_slot_view view = {.event_count = 4, .events = evs};
    dm_feedback_build_preview(&view, DM_LOCALE_US, &d.sink, NULL);
    dm_feedback_build_preview_suffix(&spec, DM_FB_STYLE_FULL, DM_LOCALE_US, &f, &d.sink);
    zassert_str_equal(d.buf, " | N4: 'ab' (2)", NULL);
}

/* ---- UK locale punctuation in a message ----------------------------------- */

ZTEST(dm_feedback_build, uk_slot_ref_roundtrips) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_UK);
    dm_feedback_spec spec = {.kind = DM_FB_SAVED, .slot = 3, .slot2 = -1, .show_preview = false};
    dm_fb_facts f = facts_default();
    dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_UK, &f, &d.sink);
    zassert_str_equal(d.buf, "[DM SAVED N3]", NULL);
}

/* ---- FI locale (full punctuation) ------------------------------------------ */

ZTEST(dm_feedback_build, rec_full_fi) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_FI);
    dm_feedback_spec spec = {.kind = DM_FB_REC, .slot = -1, .slot2 = -1};
    dm_fb_facts f = facts_default();
    dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_FI, &f, &d.sink);
    zassert_str_equal(d.buf, "[DM REC]", NULL);
}

ZTEST(dm_feedback_build, saved_no_preview_full_fi) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_FI);
    dm_feedback_spec spec = {.kind = DM_FB_SAVED, .slot = 3, .slot2 = -1, .show_preview = false};
    dm_fb_facts f = facts_default();
    dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_FI, &f, &d.sink);
    zassert_str_equal(d.buf, "[DM SAVED N3]", NULL);
}

ZTEST(dm_feedback_build, status_header_full_fi) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_FI);
    dm_feedback_spec spec = {.kind = DM_FB_STATUS_HEADER, .slot = -1, .slot2 = -1};
    dm_fb_facts f = facts_default();
    f.filled_count = 2;
    dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_FI, &f, &d.sink);
    zassert_str_equal(d.buf, "[DM 2/16 NVS:0-7 RAM:8-15]", NULL);
}

/* FI is a full-punctuation locale, so ARROW is available on it. */
ZTEST(dm_feedback_build, rec_arrow_fi) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_FI);
    dm_feedback_spec spec = {.kind = DM_FB_REC, .slot = -1, .slot2 = -1};
    dm_fb_facts f = facts_default();
    dm_feedback_build(&spec, DM_FB_STYLE_ARROW, DM_LOCALE_FI, &f, &d.sink);
    zassert_str_equal(d.buf, ">*", NULL);
}

/* The FI preview round-trip: recorded FI punctuation renders literal and the
 * re-typed characters decode back to the same FI characters. */
ZTEST(dm_feedback_build, saved_with_preview_fi_punctuation) {
    struct decode_sink d;
    ds_init(&d, DM_LOCALE_FI);
    dm_feedback_spec spec = {.kind = DM_FB_SAVED, .slot = 0, .slot2 = -1, .show_preview = true};
    dm_fb_facts f = facts_default();
    f.preview_event_count = 2;

    bool preview = dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_FI, &f, &d.sink);
    zassert_true(preview, NULL);
    zassert_str_equal(d.buf, "[DM SAVED N0: '", NULL);

    struct dm_event evs[] = {
        key(0x36, 0, 0, 1), key(0x36, 0, 0, 0),        /* , */
        key(0x37, 0, 0x02, 1), key(0x37, 0, 0x02, 0),  /* : (FI shift+dot) */
    };
    dm_render_slot_view view = {.event_count = 4, .events = evs};
    bool done = dm_feedback_build_preview(&view, DM_LOCALE_FI, &d.sink, NULL);
    zassert_true(done, NULL);
    dm_feedback_build_preview_suffix(&spec, DM_FB_STYLE_FULL, DM_LOCALE_FI, &f, &d.sink);
    zassert_str_equal(d.buf, "[DM SAVED N0: ',:']", NULL);
}

/* ---- raw keycode capture (pins exact keycode + modifiers) ------------------ */

struct raw_sink {
    dm_fb_sink sink;
    uint16_t   kc[32];
    uint8_t    mods[32];
    int        n;
};

static void raw_emit(void *ctx, uint16_t kc, uint8_t mods) {
    struct raw_sink *r = ctx;
    if (r->n < (int)(sizeof(r->kc) / sizeof(r->kc[0]))) {
        r->kc[r->n] = kc;
        r->mods[r->n] = mods;
        r->n++;
    }
}

static bool raw_space(void *ctx, uint8_t n) {
    (void)n;
    struct raw_sink *r = ctx;
    return r->n + 1 <= (int)(sizeof(r->kc) / sizeof(r->kc[0]));
}

/* '[' has no shift-level FI key: it must be typed as AltGr+8 (0x25 + LALT),
 * and ']' as AltGr+9 (0x26 + LALT) — never a US keycode or a wrong character. */
ZTEST(dm_feedback_build, fi_brackets_are_altgr_keycodes) {
    struct raw_sink r = {0};
    r.sink.emit = raw_emit;
    r.sink.space_for = raw_space;
    r.sink.ctx = &r;
    dm_feedback_spec spec = {.kind = DM_FB_REC, .slot = -1, .slot2 = -1};
    dm_fb_facts f = facts_default();
    dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_FI, &f, &r.sink);
    /* "[DM REC]" = [ D M space R E C ] */
    zassert_equal(r.n, 8, NULL);
    zassert_equal(r.kc[0], 0x25, NULL);
    zassert_equal(r.mods[0] & 0x06, 0x04, NULL); /* LALT only, no shift */
    zassert_equal(r.kc[7], 0x26, NULL);
    zassert_equal(r.mods[7] & 0x06, 0x04, NULL); /* LALT only, no shift */
}

/* Pinned round-trip: a recorded FI ` (Shift+0x2E) and ^ (Shift+0x30) must be
 * re-typed on the same physical keys — not 0x35 (½) or 0x2F (A-ring). The
 * typed preview chars are the last two keycodes, before the closing quote. */
ZTEST(dm_feedback_build, fi_backtick_caret_roundtrip_keycodes) {
    struct raw_sink r = {0};
    r.sink.emit = raw_emit;
    r.sink.space_for = raw_space;
    r.sink.ctx = &r;
    dm_feedback_spec spec = {.kind = DM_FB_SAVED, .slot = 0, .slot2 = -1, .show_preview = true};
    dm_fb_facts f = facts_default();
    f.preview_event_count = 4;

    bool preview = dm_feedback_build(&spec, DM_FB_STYLE_FULL, DM_LOCALE_FI, &f, &r.sink);
    zassert_true(preview, NULL);

    struct dm_event evs[] = {
        key(0x2E, 0, 0x02, 1), key(0x2E, 0, 0x02, 0), /* ` (FI shift+acute) */
        key(0x30, 0, 0x02, 1), key(0x30, 0, 0x02, 0), /* ^ (FI shift+diaeresis) */
    };
    dm_render_slot_view view = {.event_count = 4, .events = evs};
    bool done = dm_feedback_build_preview(&view, DM_LOCALE_FI, &r.sink, NULL);
    zassert_true(done, NULL);
    dm_feedback_build_preview_suffix(&spec, DM_FB_STYLE_FULL, DM_LOCALE_FI, &f, &r.sink);

    /* Tail: ` ^ ' ] — the typed preview chars, then the closing quote and bracket. */
    zassert_true(r.n >= 4, NULL);
    zassert_equal(r.kc[r.n - 4], 0x2E, NULL);
    zassert_equal(r.mods[r.n - 4] & 0x06, 0x02, NULL); /* shift only */
    zassert_equal(r.kc[r.n - 3], 0x30, NULL);
    zassert_equal(r.mods[r.n - 3] & 0x06, 0x02, NULL); /* shift only */
    zassert_equal(r.kc[r.n - 2], 0x32, NULL); /* closing ' at the FI apostrophe key */
    zassert_equal(r.mods[r.n - 2] & 0x06, 0x00, NULL);
    zassert_equal(r.kc[r.n - 1], 0x26, NULL);
    zassert_equal(r.mods[r.n - 1] & 0x06, 0x04, NULL); /* LALT only */
}
