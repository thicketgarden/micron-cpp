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

## Measured deviations from the reference

Both are found by `parity/run_parity.sh`, which diffs this parser against
NomadNet's own over a corpus. **The harness is red on exactly these two and
nothing else**, which is the point: a deviation you can name and reproduce is a
decision, and one you cannot is a bug you have not found yet.

### Headings carry no colour here

The reference gives headings colours from the active theme, `222222` on
`bbbbbb` at depth one and `111111` on `999999` at depth two. This parser reports
`default` for both.

Those colours come from the **theme**, not from the page. Colour is reported and
never resolved here, and a theme is the clearest case of resolution there is.
A renderer that wants NomadNet's heading palette applies it from `depth`, which
is in the style it already receives.

### Malformed colour is rejected, not consumed

`` `Fzz `` is not valid. The reference does not check: it takes three characters
whatever they are, doubles each, and sets the foreground to `zzzz  `. **That
garbage colour then persists onto every following line**, because the parse
state carries it forward. This parser validates the digits, treats the sequence
as text, and leaves colour alone.

That is a deliberate choice and it follows the rule above it: a page with a typo
should lose its formatting, never its content, and one typo should not recolour
the rest of the page. **Worth reporting upstream**, since the persistence looks
like a bug rather than a decision.

## Not implemented

Tables and partials, both skipped silently rather than emitted as raw markup.
A visible `` `t `` would be worse than a missing table.
