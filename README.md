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

**It reports colour, it does not resolve it.** A page can ask for `#ff0000`;
whether that means anything on a 1-bit panel is the renderer's problem, and the
parser refuses to guess.

Tables (`` `t ``) and partials (`` `{ ``) are not implemented. Each is a no-op
that leaves the rest of the line intact, because a visible `` `t `` is worse
than a missing table.

## Correctness

`MICRON.md` is the grammar as implemented, including the details secondhand
descriptions get wrong. It's the single source; nothing restates it.

Two test layers, and the difference matters:

- **`test/`, 36 unit tests, blocking.** Hand-written expectations. They prove
  the parser matches *our reading* of the grammar.
- **`parity/`, advisory.** Runs **NomadNet's own parser** over a corpus and
  diffs it against ours, span for span. This is what catches a misreading,
  because a hand-written expectation encodes the same misreading it is meant to
  detect.

The parity job is **allowed to be red**, and today it is red on exactly two
things, both written up in `MICRON.md`: headings take colour from the theme in
the reference and none here, and malformed colour digits are consumed there and
rejected here. A deviation you can name and reproduce is a decision. Run it
before every release and read the diff.

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
