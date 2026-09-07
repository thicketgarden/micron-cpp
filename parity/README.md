# Micron parity: our parser against NomadNet's own

`test/test_micron/` has 36 unit tests, and they assert against **hand-written
expected values**. A human read `MicronParser.py` and wrote down what they
believed it does. That proves we agree with our own reading of the grammar. It
cannot catch a misreading, because the expectation encodes the same misreading.

This harness closes that. It drives **NomadNet's actual parser** over a corpus
and diffs its output against ours, span for span.

```sh
bash parity/run_parity.sh          # builds a throwaway venv with uv
MICRON_PY=/path/to/python run_parity.sh        # or reuse one that has nomadnet
```

## How the reference is driven

`parse_line()` is called per line and its internal `make_part()` is
intercepted, so the events are the reference's own styled spans rather than our
reading of them. No parsing logic is patched.

NomadNet reaches for the running application in four places, all rendering
concerns: a theme, an urwid screen to register palette entries against, and a
colour mode. A stub supplies exactly those. Colour mode is `COLORMODE_TRUE` so
the reference doesn't quantise colour to a terminal palette.

Our side compiles with a bare compiler and nothing else:

```sh
c++ -std=c++17 -I src parity/ours_dump.cpp src/Micron.cpp
```

If that ever needs more than a C++17 compiler and the parser, the parser has
grown a dependency it shouldn't have.

## Status: green, and blocking

**8 pages, 98 reference events, zero differences.** It is a blocking CI job. A
divergence is a broken build, not a footnote.

**Never edit the expected output to close a diff.** The reference is the grammar
of record. A difference is resolved by changing this parser, or by writing a
deviation into `MICRON.md` and excluding it deliberately, and by nothing else.
Reaching green here meant changing the parser twice and correcting a unit test
the reference disproved.

### What it caught

- **Colour digits are consumed, not validated.** The parser rejected non-hex and
  left the characters in the text; the reference consumes three characters
  whatever they are. Readers saw different words. Fixed, and a unit test that
  asserted the old behaviour was corrected, because it encoded our misreading.
- **A literal toggle renders no row.** An early dumper printed a row marker once
  per input line rather than once per row the reference produced, which made
  comments, blank lines and `` `= `` toggles all look like rows, and the parser
  was edited to match. Gating the marker on `parse_line` returning a widget
  showed the reverse and the edit was reverted. **A harness that is wrong in the
  direction of agreement is worse than no harness**, because it launders its own
  artifacts into the code under test.
- **A file ending in a newline is not a file with a trailing blank line.**
- **`` `F00f `` and `` `FTff0000 `` are one colour spelled two ways.**

## Not compared yet

Dividers, fields and anchors. The reference returns those as urwid widgets from
`parse_line` rather than through `make_part`, so the dumper can't see them. Our
side emits them with a `SKIP_` prefix and the differ drops those lines on both
sides, so the gap is visible in the dump instead of silently passing.

## The corpus

Eight pages covering style toggles and nesting, both colour forms, heading
depth and reset, links and anchors, literal blocks and escapes, alignment,
comments and blank lines, and malformed markup. Malformed input matters most:
`MICRON.md` promises a page with a typo loses its formatting and never its
content, and that promise is only worth what a test makes it worth.
