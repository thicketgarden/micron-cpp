// Copyright (C) 2026 Thicket contributors
// SPDX-License-Identifier: Apache-2.0
//
// See Micron.h for why this is a streaming parser.
//
// Implemented against markqvist/NomadNet nomadnet/ui/textui/MicronParser.py
// (read 2026-08-01), functions parse_line() and make_output().

#include "Micron.h"

namespace micron {

namespace {

constexpr uint32_t BOX_DRAWINGS_LIGHT_HORIZONTAL = 0x2500;

bool isNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Micron's short colour form is three nibbles, "f0a", which MicronParser
// doubles to #ff00aa. The long form is `FT followed by six hex digits.
bool parseShortColor(const char* s, size_t avail, Color& out) {
    if (avail < 3) return false;
    int r = hexVal(s[0]), g = hexVal(s[1]), b = hexVal(s[2]);
    if (r < 0 || g < 0 || b < 0) return false;
    out.rgb = (uint32_t)((r * 17) << 16 | (g * 17) << 8 | (b * 17));
    out.is_default = false;
    return true;
}

bool parseLongColor(const char* s, size_t avail, Color& out) {
    if (avail < 6) return false;
    uint32_t v = 0;
    for (int i = 0; i < 6; i++) {
        int h = hexVal(s[i]);
        if (h < 0) return false;
        v = (v << 4) | (uint32_t)h;
    }
    out.rgb = v;
    out.is_default = false;
    return true;
}

size_t parseUInt(const char* s, size_t len, uint8_t& out) {
    size_t i = 0;
    unsigned v = 0;
    while (i < len && s[i] >= '0' && s[i] <= '9') { v = v * 10 + (unsigned)(s[i] - '0'); i++; }
    if (i > 0) out = (uint8_t)(v > 255 ? 255 : v);
    return i;
}

} // namespace

void Parser::parseLine(const char* line, size_t len, Renderer& out) {
    if (len == 0) return;

    // `= toggles literal mode. Exactly two characters, checked before anything
    // else --- inside a literal block this is the ONLY markup that's honoured.
    if (len == 2 && line[0] == '`' && line[1] == '=') {
        _style.literal = !_style.literal;
        return;
    }

    if (_style.literal) {
        // MicronParser lets a literal block emit a bare `= by escaping it.
        if (len == 3 && line[0] == '\\' && line[1] == '`' && line[2] == '=') {
            out.onText(line + 1, 2, _style);
        } else {
            out.onText(line, len, _style);
        }
        out.onLineEnd(_style);
        return;
    }

    char first = line[0];
    bool pre_escape = false;

    // A heading line containing a field loses its heading status --- upstream
    // calls this "markup sanitization" and it exists because a heading style
    // can't wrap an editable widget.
    if (first == '>') {
        bool has_field = false;
        for (size_t i = 0; i + 1 < len; i++) {
            if (line[i] == '`' && line[i + 1] == '<') { has_field = true; break; }
        }
        if (has_field) {
            while (len > 0 && line[0] == '>') { line++; len--; }
            if (len == 0) return;
            first = line[0];
        }
    }

    // A leading backslash makes the line's first character literal. Note that
    // `first` deliberately still holds the backslash, so none of the line-level
    // branches below fire --- that's exactly how an escaped '>' or '-' stays
    // text. Faithful to MicronParser, which doesn't re-read first_char here.
    if (first == '\\') {
        line++; len--;
        pre_escape = true;
        if (len == 0) return;
    } else if (first == '#') {
        return;  // comment
    }

    if (!pre_escape) {
        // Tables & partials aren't implemented. Skip the whole line rather
        // than emit its raw markup as text --- a visible "`t" would be worse
        // than a missing table, and silently dropping keeps the page readable.
        // TODO(not yet implemented): `t tables, `{ partials.
        if (len >= 2 && line[0] == '`' && (line[1] == 't' || line[1] == '{')) return;

        if (first == '<') {
            // Section reset, then re-parse the remainder at depth 0.
            _style.depth = 0;
            if (len > 1) parseLine(line + 1, len - 1, out);
            return;
        }

        if (first == '>') {
            size_t i = 0;
            while (i < len && line[i] == '>') i++;
            _style.depth = (uint8_t)i;
            line += i; len -= i;
            if (len == 0) return;
            // The heading flag is set for this line only. Content beneath a
            // heading carries the same depth and is not a heading, and the
            // reference styles only the heading itself.
            _style.heading = true;
            if (emitInline(line, len, out, false)) out.onLineEnd(_style);
            _style.heading = false;
            return;
        }

        if (first == '-') {
            uint32_t ch = BOX_DRAWINGS_LIGHT_HORIZONTAL;
            // "-x" sets the fill character. Control characters are rejected
            // because they crash upstream's renderer.
            if (len == 2 && (unsigned char)line[1] >= 32) ch = (unsigned char)line[1];
            out.onDivider(ch, _style);
            // A divider IS a row. The reference returns a widget for it, so it
            // occupies a line and the page is one taller. Same rule as
            // everywhere else here: onLineEnd fires when a row was produced.
            out.onLineEnd(_style);
            return;
        }
    }

    if (emitInline(line, len, out, pre_escape)) out.onLineEnd(_style);
}

bool Parser::emitInline(const char* line, size_t len, Renderer& out, bool pre_escape) {
    size_t run_start = 0;     // start of the current unstyled run
    size_t i = 0;
    bool escape = pre_escape;

    bool emitted = false;
    auto flush = [&](size_t end) {
        if (end > run_start) { out.onText(line + run_start, end - run_start, _style); emitted = true; }
    };

    while (i < len) {
        char c = line[i];

        if (escape) {           // previous char was a backslash: this one is literal
            escape = false;
            i++;
            continue;
        }

        if (c == '\\') {
            flush(i);           // drop the backslash itself from the output
            run_start = i + 1;
            escape = true;
            i++;
            continue;
        }

        if (c != '`') { i++; continue; }

        // A backtick begins a formatting command. Everything before it's a
        // text run in the CURRENT style; the command changes style for what
        // follows.
        flush(i);
        i++;                    // consume the backtick
        if (i >= len) { run_start = i; break; }

        char cmd = line[i];
        size_t consumed = 1;    // the command character itself

        switch (cmd) {
        case '_': _style.underline = !_style.underline; break;
        case '!': _style.bold      = !_style.bold;      break;
        case '*': _style.italic    = !_style.italic;    break;

        case '`':               // reset every attribute
            // Alignment is one of them (MicronParser.py:919-925, nomadnet 1.4.0). Leaving it
            // out strands a page in whatever alignment it last set, for every
            // line after the reset, which is why a right-aligned block leaked
            // into the sections below it. Section depth is NOT reset here; the
            // reference leaves it alone.
            _style.bold = _style.italic = _style.underline = false;
            _style.fg = Color{}; _style.bg = Color{};
            _style.align = Align::Left;
            break;

        case 'f': _style.fg = Color{}; break;   // fg back to default
        case 'b': _style.bg = Color{}; break;   // bg back to default

        case 'F':
        case 'B': {
            // The reference does NOT validate colour digits. It takes the next
            // three characters whatever they are, or six after a T, and sets
            // the colour to them (MicronParser.py:895-918, nomadnet 1.4.0). Consuming the same
            // characters is what keeps the TEXT identical, which matters more
            // than the colour does: validating here used to leave "zz" in the
            // sentence where NomadNet showed none.
            //
            // Length is the only gate, and it is the reference's: three more
            // characters must exist or nothing at all happens.
            Color parsed;
            if (i + 1 < len && line[i + 1] == 'T' && i + 7 < len) {
                if (!parseLongColor(line + i + 2, len - i - 2, parsed)) {
                    parsed.is_default = false;
                    parsed.is_valid = false;
                }
                consumed = 8;                   // 'F' + 'T' + 6 characters
            } else if (i + 3 < len) {
                if (!parseShortColor(line + i + 1, len - i - 1, parsed)) {
                    parsed.is_default = false;
                    parsed.is_valid = false;
                }
                consumed = 4;                   // 'F' + 3 characters
            } else {
                break;                          // too short: reference does nothing
            }
            if (cmd == 'F') _style.fg = parsed; else _style.bg = parsed;
            break;
        }

        case 'c': _style.align = Align::Center; break;
        case 'l': _style.align = Align::Left;   break;
        case 'r': _style.align = Align::Right;  break;
        case 'a': _style.align = Align::Left;   break;  // back to default

        case ':': {             // `:name --- zero-width anchor
            size_t n = i + 1;
            while (n < len && isNameChar(line[n])) n++;
            if (n > i + 1) out.onAnchor(line + i + 1, n - i - 1);
            consumed = n - i;
            break;
        }

        case '[': {             // `[label`target]  or  `[target]
            size_t close = i + 1;
            while (close < len && line[close] != ']') close++;
            if (close >= len) { consumed = 1; break; }   // unterminated: drop it

            const char* body = line + i + 1;
            size_t body_len = close - i - 1;

            // The reference splits the body on backticks and reads AT MOST
            // three parts: label, target, fields (MicronParser.py:1057-1073, nomadnet 1.4.0).
            // Four or more parts is not a degraded link, it is discarded
            // entirely, and the markup is still consumed.
            size_t tick[3];
            size_t ticks = 0;
            for (size_t k = 0; k < body_len && ticks < 3; k++) {
                if (body[k] == '`') tick[ticks++] = k;
            }

            const char* label = body;   size_t label_len = 0;
            const char* target = body;  size_t target_len = 0;
            const char* fields = body;  size_t fields_len = 0;

            if (ticks == 0) {                       // `[target]
                target_len = body_len;
            } else if (ticks == 1) {                // `[label`target]
                label_len  = tick[0];
                target     = body + tick[0] + 1;
                target_len = body_len - tick[0] - 1;
            } else if (ticks == 2) {                // `[label`target`fields]
                label_len  = tick[0];
                target     = body + tick[0] + 1;
                target_len = tick[1] - tick[0] - 1;
                fields     = body + tick[1] + 1;
                fields_len = body_len - tick[1] - 1;
            } else {
                target_len = 0;                     // four or more: not a link
            }

            // No target, no link. An empty label falls back to the target.
            if (target_len > 0) {
                if (label_len == 0) { label = target; label_len = target_len; }
                out.onLink(label, label_len, target, target_len, fields, fields_len, _style);
                emitted = true;
            }
            consumed = (close - i) + 1;
            break;
        }

        case '<': {             // `<flags|name`value>
            size_t tick = i + 1;
            while (tick < len && line[tick] != '`') tick++;
            if (tick >= len) { consumed = 1; break; }
            size_t close = tick + 1;
            while (close < len && line[close] != '>') close++;
            if (close >= len) { consumed = 1; break; }

            Field f;
            const char* head = line + i + 1;
            size_t head_len = tick - i - 1;

            size_t bar = head_len;
            for (size_t k = 0; k < head_len; k++) {
                if (head[k] == '|') { bar = k; break; }
            }
            if (bar == head_len) {
                f.name = head; f.name_len = head_len;
            } else {
                // flags before the bar: ^ radio, ? checkbox, ! masked, digits width
                for (size_t k = 0; k < bar; k++) {
                    if (head[k] == '^')      f.kind = FieldKind::Radio;
                    else if (head[k] == '?') f.kind = FieldKind::Checkbox;
                    else if (head[k] == '!') f.masked = true;
                }
                for (size_t k = 0; k < bar; k++) {
                    if (head[k] >= '0' && head[k] <= '9') { parseUInt(head + k, bar - k, f.width); break; }
                }
                f.name = head + bar + 1; f.name_len = head_len - bar - 1;
            }
            f.value = line + tick + 1;
            f.value_len = close - tick - 1;

            out.onField(f, _style);
            emitted = true;
            consumed = (close - i) + 1;
            break;
        }

        default:
            // Unknown command. Consume it so stray backticks don't leak into
            // the text, which is what upstream effectively does.
            break;
        }

        i += consumed;
        run_start = i;
    }

    flush(i);
    return emitted;
}

} // namespace micron
