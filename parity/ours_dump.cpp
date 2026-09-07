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
    switch (depth) {
        case 1: fg = 0x222222; bg = 0xbbbbbb; return true;
        case 2: fg = 0x111111; bg = 0x999999; return true;
        case 3: fg = 0x000000; bg = 0x777777; return true;
        default: return false;   // no heading4 upstream; behaviour undefined
    }
}

static std::string color(const Color& c) {
    if (c.is_default) return "default";
    if (!c.is_valid) return "invalid";
    // Always six hex digits. `F00f and `FTff0000 are the same colour spelled
    // two ways, and the reference dumper widens its side to match.
    char buf[8];
    std::snprintf(buf, sizeof buf, "%06x", c.rgb & 0xffffff);
    return buf;
}

static const char* align_name(Align a) {
    return a == Align::Center ? "center" : a == Align::Right ? "right" : "left";
}

class Dumper : public Renderer {
public:
    void onText(const char* t, size_t n, const Style& s) override {
        if (n == 0) return;
        std::string fg = color(s.fg), bg = color(s.bg);
        uint32_t hfg, hbg;
        if (s.heading && heading_palette(s.depth, hfg, hbg)) {
            // The page can still override a heading's colour inline, so the
            // theme only fills in what the page left alone.
            if (s.fg.is_default) { char b[8]; std::snprintf(b, sizeof b, "%06x", hfg); fg = b; }
            if (s.bg.is_default) { char b[8]; std::snprintf(b, sizeof b, "%06x", hbg); bg = b; }
        }
        std::printf("TEXT|%c%c%c|%s|%s|%s|%u|%s|%.*s\n",
                    s.bold ? 'b' : '-', s.italic ? 'i' : '-', s.underline ? 'u' : '-',
                    fg.c_str(), bg.c_str(), align_name(s.align),
                    (unsigned)s.depth, s.literal ? "lit" : "-", (int)n, t);
    }
    // The reference routes a link's label around make_part, so links cannot be
    // interleaved with text faithfully. Both sides buffer links and flush them
    // after the line's text. Order within each class is compared; order between
    // them is not.
    std::vector<std::string> links;
    void onLink(const char* l, size_t ln, const char* t, size_t tn, const Style&) override {
        links.push_back("LINK|" + std::string(l, ln) + "|" + std::string(t, tn));
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
    void onLineEnd(const Style&) override {
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
