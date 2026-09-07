# Micron, as implemented here

Reference: `markqvist/NomadNet`, `nomadnet/ui/textui/MicronParser.py` (1,048
lines). **That file is the grammar of record.** Every detail below is a passing
test in `test/test_micron/`, and the ones marked as commonly missed are the ones
second-hand summaries of Micron get wrong.

**This file is the single source for the grammar and the edge cases.**
`Micron.h` points here and restates none of it. Nothing on this page gets copied
into a comment, because a list kept in two places drifts.

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

## The six details second-hand summaries get wrong

Each one is a passing test. They are listed because getting any of them wrong
produces a parser that looks right on ordinary pages and mangles real ones.

1. `` `= `` is the **literal toggle**, not a divider.
2. A lone backtick is a **style reset**, not the literal toggle.
3. The **divider is `-`** at line start, with an optional fill character.
4. **`FT` / `BT` true-colour forms exist**, six hex digits rather than three.
5. **Checkbox and radio widgets exist**, not just text fields. So do tables
   and anchors.
6. **A lone backtick resets alignment too**, not just bold, italic, underline
   and colour. Omitting it strands every following line in whatever alignment
   the page last set.

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

Two consequences worth stating outright, because both are easy to get wrong:

**Colour digits are consumed, never validated.** `` `F `` takes the next three
characters whatever they are and `` `FT `` takes six, with no hex check at all
(`MicronParser.py:895-918`, nomadnet 1.4.0). Consuming the same characters is what keeps the
text identical: validating instead leaves `zz` sitting in a sentence where
NomadNet shows none, which is a difference a reader sees. Fewer than three
characters follow and nothing happens at all, colour included.

An unparseable triplet is reported as **set but not valid**, not as absent. A
renderer needs that distinction: "the page asked for nonsense" is not "the page
asked for nothing".

**A line that produces no row is not a line.** `` `Fzzq `` is entirely consumed,
so nothing renders. `onLineEnd` fires only when a row was actually produced,
which is what keeps page height, and therefore scroll position, identical.

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

## Tables, images and partials: structure, never layout

All three are parsed and reported. None is laid out.

**Tables.** `` `t `` opens, `` `t `` closes, and `` `tc80 `` opens one centred at
80 columns. Every line between arrives through `onTableRow` exactly as written.
The parser buffers nothing and measures nothing.

The reference does lay them out: it converts the rows to box-drawing characters
at a fixed width and re-parses the result. That is a rendering decision made for
a terminal. Column widths depend on the panel and the font, and a 400x240 1-bit
display is not a 100-column terminal, so the rows are handed over intact and the
renderer splits on `|` and lays out for the display it has.

**Images.** `` `(alt`w=40`a=c`/path.webp) ``. First part is the alt text, last is
the URL, the parts between are properties: `w`, `h`, `a`. Dimensions are
reported **as written**, `40` or `50%`, because what a width means depends on a
panel the parser cannot see. Fewer than two parts is not an image.

**Partials.** `` `{/page/x.mu`10`a=1|b=2} ``: URL, refresh in seconds, then
pipe-separated fields. Four or more parts is not a partial, the same rule links
follow.

⚠ **Refresh is reported as written, and the reference ignores anything below
1 second.** That rule is the renderer's to apply; parsing a float on a
microcontroller to enforce it here would be the wrong place.

### What this means for parity

The full-corpus run excludes these three constructs on both sides, because
there is no rendered text to compare against a reference that renders. Their
parsed fields are asserted by unit tests read off nomadnet 1.4.0 instead.
**Everything else matches: 82 real pages, zero differences.**

## Reading the reference

⚠ **Cite the version.** The released nomadnet 1.4.0 is 1,334 lines and carries
`parse_image`; the GitHub default branch is 1,048 lines and has no images at
all. A line number means nothing on its own. Every citation here and in the
source names the version it was read in, and the harness pins
`nomadnet==1.4.0`.

urwid is **not** pinned. Measured across 2.6.16, 3.0.5 and 4.1.1, the reference
dump is byte-identical, so constraining it would buy nothing.
