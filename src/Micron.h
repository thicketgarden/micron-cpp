// Copyright (C) 2026 Thicket contributors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0. You may obtain a copy at
// http://www.apache.org/licenses/LICENSE-2.0
//
// ---------------------------------------------------------------------------
// Micron, a streaming parser for NomadNet's page markup.
//
// WHY THIS SHAPE
//
// This is a SAX-style parser: you feed it a line, it calls back with styled
// spans. It builds no document tree & allocates nothing per element.
//
// That's not a stylistic preference, it's the constraint. Our own estimate
// budgets roughly 113-146 KB of SRAM for everything the display & page
// browsing layer adds, against 256 KB total. The nearest comparable
// renderer (reticulous/nomad's Micron->LVGL) caps itself at 600 retained LVGL
// objects and still requires 8 MB of PSRAM, because it holds an object tree
// per page. We can't, so we don't: parse & draw straight into the
// framebuffer, keep the source bytes and a scroll offset, re-render on change.
// A Sharp memory LCD holds its own image with the CPU asleep, so the panel IS
// the retained model & re-rendering is rare.
//
// GRAMMAR OF RECORD
//
// markqvist/NomadNet, nomadnet/ui/textui/MicronParser.py. Implemented against
// that file directly, never against a description of the format: second-hand
// summaries of Micron get five specific details wrong.
//
// MICRON.md IS THE SINGLE SOURCE for the grammar, those five details, and what
// the renderer is expected to finish. None of it is restated here, because a
// list kept in two files drifts.
//
// NOT YET IMPLEMENTED, deliberately, each a no-op that doesn't corrupt the
// rest of the line:  `t tables  ·  `{ partials.  MICRON.md says why.
// ---------------------------------------------------------------------------

#ifndef THICKET_MICRON_H
#define THICKET_MICRON_H

#include <stddef.h>
#include <stdint.h>

namespace micron {

enum class Align : uint8_t { Left, Center, Right };

// A colour is 24-bit RGB, or "the renderer's default".
// Micron carries explicit colour; a 1-bit panel has two inks. Resolving that is
// the RENDERER's job, not the parser's --- the parser reports what the page
// asked for and never decides what it means. See MICRON.md.
struct Color {
    uint32_t rgb = 0;          // 0xRRGGBB
    bool     is_default = true;
    // The page asked for a colour and it wasn't one. Micron doesn't validate
    // its colour digits, so `Fzz is legal syntax carrying nonsense, and a
    // renderer needs to be able to tell that apart from "no colour asked for".
    // rgb is meaningless when this is true.
    bool     is_valid = true;

    bool operator==(const Color& o) const {
        if (is_default != o.is_default) return false;
        if (is_default) return true;
        if (is_valid != o.is_valid) return false;
        return !is_valid || rgb == o.rgb;
    }
    bool operator!=(const Color& o) const { return !(*this == o); }
};

struct Style {
    bool  bold      = false;
    bool  italic    = false;
    bool  underline = false;
    Color fg;
    Color bg;
    Align align     = Align::Left;
    uint8_t depth   = 0;       // section depth; indent is depth * SECTION_INDENT
    bool  heading   = false;   // THIS line is the heading, not content beneath
                               // it. Both carry the same depth, and only the
                               // heading takes a heading style.
    bool  literal   = false;   // inside a `= block: emit verbatim, no markup

    bool operator==(const Style& o) const {
        return bold == o.bold && italic == o.italic && underline == o.underline
            && fg == o.fg && bg == o.bg && align == o.align
            && depth == o.depth && heading == o.heading && literal == o.literal;
    }
};

// NomadNet indents section content by 2 columns per depth level.
static constexpr uint8_t SECTION_INDENT = 2;

// MicronParser.py's default when a field declares no width.
static constexpr uint8_t DEFAULT_FIELD_WIDTH = 24;

enum class FieldKind : uint8_t { Text, Checkbox, Radio };

// `(alt`w=40`a=c`/path/image.webp)
// Every part points into the caller's line buffer and dies with the callback.
// Dimensions are reported AS WRITTEN, "40" or "50%", never resolved: what a
// width means depends on the panel, and the parser does not know the panel.
struct Image {
    const char* alt = nullptr;    size_t alt_len = 0;
    const char* url = nullptr;    size_t url_len = 0;
    const char* width = nullptr;  size_t width_len = 0;
    const char* height = nullptr; size_t height_len = 0;
    Align       align = Align::Left;
    bool        align_set = false;
};

// `{/page/thing.mu`10`field=value}  --- an in-page region the client refetches.
// Refresh is in seconds as written; the reference ignores anything under 1.
struct Partial {
    const char* url = nullptr;      size_t url_len = 0;
    const char* refresh = nullptr;  size_t refresh_len = 0;
    const char* fields = nullptr;   size_t fields_len = 0;
};

// `t opens a table, `t closes it. `tc80 opens one centred at 80 columns.
struct Table {
    Align    align = Align::Left;
    bool     align_set = false;
    uint16_t max_width = 0;
    bool     max_width_set = false;
};

struct Field {
    const char* name     = nullptr;  // NOT null-terminated; use name_len
    size_t      name_len = 0;
    // A TEXT field's value is its preset content, and it has no label.
    // A CHECKBOX or RADIO carries both, and they are different things: the
    // value is what gets submitted, the label is what a reader sees beside the
    // box (MicronParser.py, "for checkboxes and radios, field_data is the
    // label"). Conflating them puts the submitted value on screen.
    const char* value    = nullptr;
    size_t      value_len = 0;
    const char* label     = nullptr;
    size_t      label_len = 0;
    uint8_t     width    = DEFAULT_FIELD_WIDTH;
    FieldKind   kind     = FieldKind::Text;
    bool        masked   = false;    // render as asterisks
    bool        prechecked = false;
};

// Renderer interface. Implement this for text, for the Sharp panel, for tests.
//
// Every pointer handed to a callback points INTO THE CALLER'S LINE BUFFER and
// is invalid once the callback returns. Copy what you need. This is what keeps
// the parser allocation-free.
class Renderer {
public:
    virtual ~Renderer() = default;

    // A run of text in one style. Never spans a line.
    virtual void onText(const char* text, size_t len, const Style& style) = 0;

    // `[label`target`fields] --- three parts at most, any of which may be
    // absent. An empty label falls back to the target. `fields` carries the
    // pipe-separated form data an interactive page submits with the request,
    // and is empty for an ordinary link. A body with more than three
    // backtick-separated parts is not a link at all and never reaches here.
    virtual void onLink(const char* label, size_t label_len,
                        const char* target, size_t target_len,
                        const char* fields, size_t fields_len,
                        const Style& style) = 0;

    // A line starting with '-'. `ch` is the fill character (UTF-8 codepoint).
    virtual void onDivider(uint32_t ch, const Style& style) = 0;

    // `<...`...> form widget.
    virtual void onField(const Field& field, const Style& style) = 0;

    // `:name --- a zero-width named position, for in-document links.
    virtual void onAnchor(const char* name, size_t len) = 0;

    // Tables, images and partials. These have DEFAULT NO-OP implementations,
    // so a renderer that does not care about them need not mention them, and
    // adding one later does not break existing renderers.
    //
    // STRUCTURE, NOT LAYOUT. A table's rows arrive exactly as written and this
    // parser buffers nothing and measures nothing. The reference lays tables
    // out into box-drawing characters at a fixed width and re-parses the
    // result, which is a rendering decision: column widths depend on the panel
    // and on the font, and neither is knowable here. Split the rows on '|' and
    // lay them out for the display you actually have.
    virtual void onTableBegin(const Table&, const Style&) {}
    virtual void onTableRow(const char* /*row*/, size_t /*len*/, const Style&) {}
    virtual void onTableEnd(const Style&) {}
    virtual void onImage(const Image&, const Style&) {}
    virtual void onPartial(const Partial&, const Style&) {}

    // Called once per input line that produced a row, after its content.
    // Not called for comments, literal toggles, or empty results, because the
    // reference renders no row for those either and page height must match.
    virtual void onLineEnd(const Style& style) = 0;
};

class Parser {
public:
    // Feed one line, without its terminator. Safe to call with len == 0.
    void parseLine(const char* line, size_t len, Renderer& out);

    // Reset to document-start state. Call between pages --- style, section
    // depth, literal mode & table mode all persist across lines by design.
    void reset() { _style = Style{}; _in_table = false; _table = Table{}; }

    // True while between an opening and closing `t. Every line fed in this
    // state is reported through onTableRow untouched.
    bool inTable() const { return _in_table; }

    const Style& style() const { return _style; }

private:
    Style _style;
    bool  _in_table = false;
    Table _table;

    // Inline markup pass. `pre_escape` means the line began with a backslash,
    // so its first character is literal. Returns true if it emitted anything,
    // which is what decides whether the line becomes a row: a line holding only
    // markup, like a lone colour command, renders nothing in the reference.
    bool emitInline(const char* line, size_t len, Renderer& out, bool pre_escape);

    // Shared body parser for `( images and `{ partials: both are a
    // backtick-separated body ending at the LAST closing delimiter.
    void emitDelimited(const char* line, size_t len, char close,
                       Renderer& out, bool image);
};

} // namespace micron

#endif // THICKET_MICRON_H
