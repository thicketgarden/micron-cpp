// Copyright (C) 2026 Thicket contributors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0. You may obtain a copy at
// http://www.apache.org/licenses/LICENSE-2.0
//
// Dump our parser's callbacks in the same event format reference_dump.py emits,
// so the two can be diffed line for line.
//
// Compiles with nothing but a C++17 compiler and the parser itself:
//   c++ -std=c++17 -I ../lib/Micron/src ours_dump.cpp ../lib/Micron/src/Micron.cpp
// That command is also the cleanest evidence the parser carries no firmware,
// Reticulum or display dependency.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <cstring>
#include <iostream>
#include "Micron.h"

using namespace micron;

// NomadNet's dark-theme heading palette, STYLES_DARK in MicronParser.py.
// Applying it here is a RENDERER step, done deliberately: this parser reports
// depth and never resolves a theme, and the point of doing it in the dumper is
// to prove that `depth` plus `heading` carries everything needed to reproduce
// the reference's output. Get depth wrong and these stop matching.
static bool heading_palette(uint8_t depth, uint32_t& fg, uint32_t& bg) {
    // There is no heading4 in the theme. The reference looks up "heading" plus
    // the depth, finds nothing past three, and leaves its style variable at the
    // last value that matched, so every deeper heading renders as heading3
    // rather than as plain text. Verified against nomadnet 1.4.0 at depths 1
    // through 6.
    if (depth == 0) return false;
    if (depth >= 3) { fg = 0x000000; bg = 0x777777; return true; }
    if (depth == 2) { fg = 0x111111; bg = 0x999999; return true; }
    fg = 0x222222; bg = 0xbbbbbb; return true;
}

// The dark theme's own default foreground, DEFAULT_FG_DARK = "ddd". A page that
// writes `Fddd explicitly is indistinguishable from one that says nothing in
// the reference, because its state holds the same string either way. This
// parser CAN tell them apart, so the harness collapses the distinction the
// reference cannot represent rather than the other way round.
static const uint32_t THEME_DEFAULT_FG = 0xdddddd;

// FOREGROUND ONLY. DEFAULT_BG is the literal string "default", not a colour, so
// an explicit `Bddd background is a real value the reference does report.
static std::string color(const Color& c, bool is_fg = false) {
    if (c.is_default) return "default";
    if (!c.is_valid) return "invalid";
    if (is_fg && c.rgb == THEME_DEFAULT_FG) return "default";
    // Always six hex digits. `F00f and `FTff0000 are the same colour spelled
    // two ways, and the reference dumper widens its side to match.
    char buf[8];
    std::snprintf(buf, sizeof buf, "%06x", c.rgb & 0xffffff);
    return buf;
}

static std::string strip(const char* s, size_t n) {
    size_t a = 0, b = n;
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    return std::string(s + a, b - a);
}

static const char* align_name(Align a) {
    return a == Align::Center ? "center" : a == Align::Right ? "right" : "left";
}

class Dumper : public Renderer {
public:
    // Adjacent runs in the same style are merged before printing, on both
    // sides of the diff. A backslash escape ends a run here because the two
    // halves are not contiguous in the source and this parser copies nothing,
    // while the reference accumulates into a Python string and emits one run.
    // The characters and the styles are identical either way, so merging
    // compares what a renderer actually draws instead of where the parser
    // happened to breathe. A real difference in text or style still fails.
    std::string pending_key, pending_text;
    void flush_text() {
        if (pending_key.empty() && pending_text.empty()) return;
        std::printf("TEXT|%s|%s\n", pending_key.c_str(), pending_text.c_str());
        pending_key.clear(); pending_text.clear();
    }
    void onText(const char* t, size_t n, const Style& s) override {
        if (n == 0) return;
        std::string fg = color(s.fg, true), bg = color(s.bg);
        uint32_t hfg, hbg;
        if (s.heading && heading_palette(s.depth, hfg, hbg)) {
            // The heading style REPLACES the current colours rather than
            // filling in unset ones. The reference applies it with
            // style_to_state, which overwrites, so a page that sets a colour
            // just before a heading does not carry it into the heading.
            char b[8];
            std::snprintf(b, sizeof b, "%06x", hfg); fg = b;
            std::snprintf(b, sizeof b, "%06x", hbg); bg = b;
        }
        char flags[4] = { s.bold ? 'b' : '-', s.italic ? 'i' : '-', s.underline ? 'u' : '-', 0 };
        std::string key = std::string(flags) + "|" + fg + "|" + bg + "|" + align_name(s.align)
                        + "|" + std::to_string((unsigned)s.depth) + "|" + (s.literal ? "lit" : "-");
        if (!pending_text.empty() && key != pending_key) flush_text();
        pending_key = key;
        pending_text.append(t, n);
    }
    // The reference routes a link's label around make_part, so links cannot be
    // interleaved with text faithfully. Both sides buffer links and flush them
    // after the line's text. Order within each class is compared; order between
    // them is not.
    std::vector<std::string> links;
    void onLink(const char* l, size_t ln, const char* t, size_t tn,
                const char* f, size_t fn, const Style&) override {
        // No flush. The reference collects links separately and its text parts
        // merge straight across a link, so flushing here would split a
        // sentence that contains one. Text is flushed at end of line only.
        links.push_back("LINK|" + std::string(l, ln) + "|" + std::string(t, tn)
                        + "|" + std::string(f, fn));
    }
    // Not compared yet: the reference returns these as urwid widgets rather
    // than through make_part, so reference_dump.py cannot see them. Emitted
    // with a SKIP prefix so the differ drops them on this side too, and so the
    // gap is visible in the dump rather than silent.
    void onDivider(uint32_t ch, const Style&) override { std::printf("SKIP_DIV|%u\n", ch); }
    void onField(const Field& f, const Style&) override {
        std::printf("SKIP_FIELD|%.*s\n", (int)f.name_len, f.name ? f.name : "");
    }
    void onAnchor(const char* n, size_t len) override { std::printf("SKIP_ANCHOR|%.*s\n", (int)len, n); }

    // Tables, images and partials are reported as structure and never laid
    // out, so their rendered text cannot be diffed against a reference that
    // does lay them out. They are printed for eyeballing and excluded from the
    // comparison; the unit tests assert their fields instead.
    void onTableBegin(const Table& t, const Style&) override {
        in_table = true;
        std::printf("TABLE_BEGIN|%s|%u\n",
                    t.align_set ? align_name(t.align) : "-",
                    t.max_width_set ? (unsigned)t.max_width : 0u);
    }
    void onTableRow(const char* r, size_t n, const Style&) override {
        std::printf("TABLE_ROW|%.*s\n", (int)n, r);
    }
    void onTableEnd(const Style&) override { in_table = false; std::printf("TABLE_END\n"); }

    void onImage(const Image& i, const Style&) override {
        in_media = true;
        // The reference strips alt and url, and exposes alignment only as
        // ImageWidget's own glyph: unset and centre are both '|', so the two
        // are indistinguishable there and compared as one here.
        const char* glyph = !i.align_set ? "|"
                          : i.align == Align::Left ? "<"
                          : i.align == Align::Right ? ">" : "|";
        std::printf("IMAGE|%s|%s|%s|%s|%s\n",
                    strip(i.alt, i.alt_len).c_str(), strip(i.url, i.url_len).c_str(),
                    std::string(i.width ? i.width : "", i.width_len).c_str(),
                    std::string(i.height ? i.height : "", i.height_len).c_str(), glyph);
    }

    void onPartial(const Partial& p, const Style&) override {
        in_media = true;
        // Refresh is reported as written by the parser; the reference stores a
        // float and discards anything under a second, which is the renderer's
        // rule and is applied here to compare like with like.
        std::string refresh(p.refresh ? p.refresh : "", p.refresh_len);
        double secs = refresh.empty() ? 0.0 : std::strtod(refresh.c_str(), nullptr);
        std::string rout;
        if (secs >= 1.0) {
            char b[32];
            if (secs == (long long)secs) std::snprintf(b, sizeof b, "%lld", (long long)secs);
            else                          std::snprintf(b, sizeof b, "%g", secs);
            rout = b;
        }
        std::string fields(p.fields ? p.fields : "", p.fields_len);
        std::printf("PARTIAL|%s|%s|%s\n",
                    std::string(p.url ? p.url : "", p.url_len).c_str(), rout.c_str(), fields.c_str());
    }
    bool in_table = false, in_media = false;
    void onLineEnd(const Style&) override {
        // An image or partial row is excluded from the diff along with its
        // content, because the reference renders a widget we do not produce.
        if (in_media) { in_media = false; pending_key.clear(); pending_text.clear(); links.clear();
                        std::printf("SKIP_MEDIA_EOL\n"); return; }
        flush_text();
        for (const auto& l : links) std::printf("%s\n", l.c_str());
        links.clear();
        std::printf("EOL\n");
    }
};

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: ours_dump <page.mu>...\n"); return 2; }
    for (int a = 1; a < argc; a++) {
        std::ifstream in(argv[a]);
        if (!in) { std::fprintf(stderr, "cannot open %s\n", argv[a]); return 1; }
        const char* base = std::strrchr(argv[a], '/');
        std::printf("# page %s\n", base ? base + 1 : argv[a]);

        Parser p;
        p.reset();
        Dumper out;
        std::string line;
        // getline strips the terminator, which is what parseLine expects.
        while (std::getline(in, line)) p.parseLine(line.data(), line.size(), out);
    }
    return 0;
}
