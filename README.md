# micron-cpp

A streaming parser for **Micron**, the page markup NomadNet renders, in C++ with
no allocation and no display assumptions.

```cpp
#include "Micron.h"

class MyRenderer : public micron::Renderer {
    void onText(const char* t, size_t n, const micron::Style& s) override {
        // s carries bold, italic, underline, fg, bg, align, depth, literal
    }
    // onLink, onDivider, onField, onAnchor, onLineEnd
};

micron::Parser p;
MyRenderer r;
p.parseLine(line, len, r);   // one line at a time
```

## Why

Micron implementations in C++ exist, and they arrive welded to a toolkit. The
ones in the Reticulum ecosystem parse straight into LVGL objects and assume
PSRAM to hold a widget tree per page. That is a reasonable design on an ESP32
with 8 MB of it, and unusable on a part with 256 KB of SRAM and no external RAM
at all.

This is the parser on its own. It builds no document tree, allocates nothing
per element, and never touches a framebuffer. **You get callbacks; what you draw
is your business.**

## What it does not do

**It reports, it does not resolve.** A page can ask for `#ff0000`; whether that
means anything on a 1-bit panel is the renderer's problem. Tables arrive as
rows, images as an alt text and a URL with dimensions as written, headings as a
depth. Nothing is measured, coloured in, or laid out, because the panel is not
visible from here.

## Correctness

`MICRON.md` is the grammar as implemented, including the details secondhand
descriptions get wrong. It's the single source; nothing restates it.

Two test layers, and the difference matters:

- **`test/`, 38 unit tests.** Hand-written expectations. They prove the parser
  matches *our reading* of the grammar.
- **`parity/`.** Runs **NomadNet's own parser** over a corpus and diffs it
  against ours, span for span. This is what catches a misreading, because a
  hand-written expectation encodes the same misreading it is meant to detect.

**Both block CI. Parity passes on every compared event across 82 real pages.**
Tables, images and partials are excluded from that diff, because the reference
lays them out and this parser deliberately does not; their fields are asserted
by unit tests instead.

Heading colour comes from a theme, and this parser has none. `Style` carries
`depth` and a `heading` flag instead, and the harness applies NomadNet's own
palette from those two fields to show they are sufficient.

The parser also compiles with a bare `c++ -std=c++17 -c src/Micron.cpp` in CI.
If that job ever needs a framework or a board, the dependency claim on this page
has stopped being true.

## Use it

PlatformIO, pinned to a commit like anything else you depend on:

```ini
lib_deps =
    https://github.com/thicketgarden/micron-cpp.git#<full-sha>
```

Anything else: add `src/Micron.cpp` to your build and `src/` to your include
path. There is nothing to configure and nothing to link against.

## Licence and provenance

**Apache-2.0**, so it can be used alongside the Apache-2.0 Reticulum stacks it
is most useful to.

Written against `markqvist/NomadNet`, `nomadnet/ui/textui/MicronParser.py`,
which is the grammar of record. Read, not copied: no NomadNet code is
reproduced here, which is why the licences differ. NomadNet is GPL-3.0 and its
authors deserve the credit for the format.
