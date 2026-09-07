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

## The oracle is `nomadnet==1.4.0`, and urwid is not pinned

**Pin nomadnet.** It is the grammar of record, and the versions differ in ways
that matter: 1.4.0 is 1,334 lines and carries `parse_image`, while the GitHub
default branch is 1,048 lines and has no images at all. A floating oracle would
change what parity means without anyone touching this parser.

**urwid is deliberately not pinned.** Measured across 2.6.16, 3.0.5 and 4.1.1:
20,752 events, zero reference errors, byte-identical output, same SHA256. It
does not affect the dump, so pinning it would be a constraint that buys nothing
and rots.

If `reference_dump.py` ever reports a `REFERENCE_ERROR`, read the note beside
that handler before blaming NomadNet or a dependency.

## Two corpora

`run_parity.sh` is **the gate**, 8 pages, green, blocking.

`run_full_corpus.sh` is **the thermometer**: 82 real pages pinned from
`thicketgarden/micron-cpp-corpus`, reporting a difference count rather than
passing or failing. It is promoted to blocking when it reaches zero.

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

## Compared

Text with full style, links with labels, targets and fields, table alignment,
width and rows, image alt text, URL, dimensions and alignment, and partial URL,
refresh and fields. All against the reference's own parsed values, taken off the
widgets and state it produces rather than re-derived.

Dividers and fields are compared too, off the widgets the reference builds:
`Divider.div_char`, and a field's `field_name`, `field_value`, label, `_mask`
and checked `state`.

⚠ **They were not, and that is how three parser bugs stayed green.** Skipping a
construct on both sides makes parity pass over it BY CONSTRUCTION, and the
corpus already held every case: four pages with a pre-checked box, five with a
masked field, seven with a multi-byte divider fill. None of it was ever diffed.
Adding corpus pages would not have caught them; only comparing them did.

**Still not compared:** anchors, and a field's width. An anchor is zero-width
with nothing to read back, and the reference carries field width as a column
width rather than on the field.

**Table layout is not compared either**, only the rows going in. The reference
converts them to box-drawing text at a fixed width; this parser reports the rows
and lets the renderer lay them out.

⚠ **Image alignment is compared as ImageWidget's glyph**, because that is all
the reference exposes: unset and `a=c` both become `|`, so the two are
indistinguishable there. The unit tests carry that distinction instead.

## The corpus

Eight pages covering style toggles and nesting, both colour forms, heading
depth and reset, links and anchors, literal blocks and escapes, alignment,
comments and blank lines, and malformed markup. Malformed input matters most:
`MICRON.md` promises a page with a typo loses its formatting and never its
content, and that promise is only worth what a test makes it worth.
