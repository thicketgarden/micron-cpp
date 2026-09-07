# Micron, as implemented here

Reference: `markqvist/NomadNet`, `nomadnet/ui/textui/MicronParser.py` (1,048
lines), read 2026-08-01. **That file is the grammar of record.** This document
exists because a careful second-hand summary of it was wrong in five places,
and every correction below is a passing test in `test/test_micron/`.

**This file is the single source for the grammar and the edge cases.**
`Micron.h` points here rather than restating any of it. It used to restate a
short version, which drifted: the header claimed four commonly-missed details
and named a different set from the five below. Nothing that appears here gets
copied into a comment.

## Line level

| Construct | Meaning |
|---|---|
| `` `= `` | **toggles a literal block.** Inside one, markup is emitted verbatim; `` \`= `` escapes a literal `` `= `` |
| `#` | comment, whole line dropped |
| `>` `>>` `>>>` | section heading; the count sets depth, which persists across lines. Content indents `depth * 2` |
| `<` | resets depth to 0, then re-parses the rest of the line |
| `-` | horizontal divider. `-x` sets the fill character; default U+2500. Control characters are rejected |
| `\` | leading backslash makes the first character literal, which is how a line beginning `>` or `-` stays text |
| `` `t `` | table open/close, optional `l`/`c`/`r` and a max width. **Not implemented** |
| `` `{ `` | in-page partial. **Not implemented** |

**Heading sanitisation:** a `>` line containing a field (`` `< ``) loses its
heading status, because a heading style can't wrap an editable widget.

## Inline, after a backtick

| | |
|---|---|
| `_` `!` `*` | toggle underline, bold, italic |
| `` ` `` | **reset every attribute** (not a literal toggle) |
| `F<rgb>` / `B<rgb>` | foreground / background, 3 nibbles doubled: `f00` becomes `#ff0000` |
| `FT<rrggbb>` / `BT<rrggbb>` | **true colour, six hex digits** |
| `f` / `b` | foreground / background back to default |
| `c` `l` `r` `a` | centre, left, right, default alignment |
| `:name` | zero-width **anchor** for in-document links |
| `` [label`target] `` | link; with no backtick the whole body is both label & target |
| `<flags\|name`value>` | field. Flags: `^` radio, `?` checkbox, `!` masked, digits set width (default 24) |

## The five things the summary got wrong

Recorded because they are the reason this file exists, not as trivia.

1. `` `= `` is the **literal toggle**, not a divider.
2. A lone backtick is a **style reset**, not the literal toggle.
3. The **divider is `-`** at line start, with an optional fill character.
4. **`FT` / `BT` true-colour forms exist** and were missing entirely.
5. **Checkbox and radio widgets exist**, not just text fields. Tables and
   anchors were omitted too.

## Deliberate deviations

- **Malformed markup drops the marker and keeps the words.** An unterminated
  link or field emits its text rather than swallowing it. A page with a typo
  should lose its formatting, never its content.
- **Unknown commands are consumed**, so a stray backtick doesn't leak.
- **Colour is reported, never resolved.** The parser hands the renderer what
  the page asked for. Deciding what `` `F00f `` means on a two-ink panel is the
  renderer's problem, and on a two-ink panel it is still an open design
  question here.

## Parity with the reference

`parity/run_parity.sh` diffs this parser against NomadNet's own over a corpus,
span for span. **It passes on every compared event**, and it is a blocking CI
job, so a divergence breaks the build rather than being noted somewhere.

Two things it settled, both of which changed this parser:

**Colour digits are consumed, never validated.** `` `F `` takes the next three
characters whatever they are and `` `FT `` takes six, and the reference does no
hex check at all (`MicronParser.py:617-638`). This parser used to validate and
skip, which left `zz` sitting in the sentence where NomadNet showed none. That
is a difference a reader sees, so consumption now matches exactly. The colour is
reported as **set but not valid** rather than as absent, which is a distinction
a renderer needs: "the page asked for nonsense" is not "the page asked for
nothing".

**A line that produces no row is not a line.** `` `Fzzq `` is entirely consumed,
so the reference renders nothing and neither do we. `onLineEnd` fires only when
a row was actually produced, which is what keeps page height identical.

### Where the renderer has to finish the job

**Heading colour comes from a theme, and this parser has no theme.** The
reference paints depth one `222` on `bbb`, depth two `111` on `999`, depth three
`000` on `777`, from `STYLES_DARK`. Colour is reported here and never resolved,
so `Style` carries `depth` and a `heading` flag and the renderer applies its own
palette.

That is not an untested gap. `parity/ours_dump.cpp` applies NomadNet's dark
palette from `depth` and `heading` and the diff passes, which demonstrates the
two fields carry everything needed to reproduce the reference exactly. Get depth
wrong and parity goes red.

There is no `heading4` upstream, so behaviour past depth three is undefined
there and not compared here.

## Not implemented

Tables and partials, both skipped silently rather than emitted as raw markup.
A visible `` `t `` would be worse than a missing table.
