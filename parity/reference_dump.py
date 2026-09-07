#!/usr/bin/env python3
"""Dump NomadNet's own MicronParser output as comparable events.

The reference is driven, never reimplemented. parse_line() is called for every
line of every corpus page and its internal make_part() is intercepted, so what
comes out is the reference's own styled spans rather than our reading of them.

NomadNet's parser reaches for the running application in four places, all of
them rendering concerns: a theme, an urwid screen to register palette entries
against, and a colour mode. A stub supplies those and nothing else. No parsing
logic is patched. Colour mode is COLORMODE_TRUE so the reference does not
quantise colour to a terminal palette, which is the closest match to a parser
that reports colour as the page asked for it.

  ./reference_dump.py corpus/*.mu > reference.events

Compared today: TEXT spans with full style, and link labels with their targets.
NOT compared: dividers, fields and anchors. The reference returns those as
urwid widgets from parse_line rather than through make_part, so they need a
second extraction path that does not exist yet. Our side emits them; this side
does not, and the differ skips those classes on both sides rather than
pretending they matched.
"""

import sys, os

import nomadnet, urwid
import nomadnet.ui.TextUI as T
import RNS

# RNS logs to stdout, which is where the events go. Silence it, or a stray
# warning becomes an event line and the diff blames the parser.
RNS.loglevel = 0
RNS.compact_log_fmt = True
RNS.log = lambda *a, **k: None


class _Screen:
    """urwid needs a real screen object to register palette entries against."""
    def __init__(self):
        self._s = urwid.raw_display.Screen()
    def __getattr__(self, k):
        return getattr(self._s, k)


class _UI:
    screen = _Screen()
    colormode = T.COLORMODE_TRUE


class _App:
    config = {"textui": {"theme": T.THEME_DARK}}
    ui = _UI()


_APP = _App()
nomadnet.NomadNetworkApp.get_shared_instance = staticmethod(lambda: _APP)

from nomadnet.ui.textui import MicronParser as M  # noqa: E402  (needs the stub first)

class _UrlDelegate:
    """A delegate the reference can actually use.

    Presence alone is not enough. parse_image asks it to resolve an image path
    and reads a glyph table off it, and a missing attribute is caught by the
    reference and logged as its own error, which pollutes the event stream and
    looks like a parser failure."""

    # Glyphs the reference substitutes into image placeholders.
    g = {"image": "[img]", "warning": "[!]", "page": "[p]", "file": "[f]",
         "link": "[l]", "unknown": "[?]"}

    def build_url(self, url, **kwargs): return url

    def resolve_image(self, url, **kwargs):
        # No image is ever loaded here. Returning None takes the reference's
        # own "could not load" path, which is deterministic and needs no files.
        return None


_URL_DELEGATE = _UrlDelegate()
_parts = []
_links = []
_orig_make_part = M.make_part
_orig_make_output = M.make_output


# Snapshot ONLY the style fields, never the whole state. The parse state holds
# urwid widgets for radio groups, and deep-copying a Columns raises inside
# urwid: MonitoredList.append fires _contents_modified before _contents exists.
# That surfaces as an exception attributed to the reference parser, when the
# harness caused it.
_STYLE_KEYS = ("fg_color", "bg_color", "default_fg", "default_bg",
               "align", "depth", "literal")


def _snapshot(state):
    snap = {k: state.get(k) for k in _STYLE_KEYS}
    snap["formatting"] = dict(state.get("formatting", {}))
    return snap


def _spy_make_part(state, part):
    _parts.append((_snapshot(state), part))
    return _orig_make_part(state, part)


def _spy_make_output(state, line, url_delegate, pre_escape=False):
    """Links do not pass through make_part when a delegate is present.

    MicronParser.py:1100-1106, nomadnet 1.4.0 appends (linkspec, link_label) straight to the output
    list instead, so label and target are paired here and nowhere else."""
    out = _orig_make_output(state, line, url_delegate, pre_escape)
    if isinstance(out, list):
        for entry in out:
            if isinstance(entry, tuple) and len(entry) == 2 and isinstance(entry[0], M.LinkSpec):
                # link_fields is stored split on "|" (MicronParser.py:1104, nomadnet 1.4.0)
                # and absent entirely for an ordinary link.
                lf = getattr(entry[0], "link_fields", None)
                _links.append((getattr(entry[0], "link_target", "?"), entry[1],
                               "|".join(lf) if lf else ""))
    return out


M.make_part = _spy_make_part
M.make_output = _spy_make_output


def _color(value, default):
    """Normalise to six hex digits so `F00f and `FTff0000 compare equal.

    Micron spells the same colour two ways and the reference keeps the
    spelling. A renderer cares about the colour, not which form the page used,
    so both sides are widened here rather than our parser being asked to carry
    a distinction it has no use for."""
    if value is None or value == default:
        return "default"
    v = str(value)
    # Micron does not validate colour digits, so the reference happily stores
    # non-hex here. That is not a colour, and calling it one would be a lie in
    # both directions: report it as invalid, which is what our parser reports.
    if not all(ch in "0123456789abcdefABCDEF" for ch in v):
        return "invalid"
    if len(v) == 3:
        v = "".join(ch * 2 for ch in v)
    return v.lower()


def dump(path, out):
    state = M.default_state()
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    # A file ending in a newline is not a file with a trailing blank line.
    if text.endswith("\n"):
        text = text[:-1]
    lines = text.split("\n")

    print(f"# page {os.path.basename(path)}", file=out)
    for lineno, line in enumerate(lines, 1):
        _parts.clear()
        _links.clear()
        was_table = state.get("table_mode", False)
        was_literal = state.get("literal", False)
        try:
            # A url_delegate must be present or the reference never constructs a
            # LinkSpec (MicronParser.py:1100, nomadnet 1.4.0), and link targets never reach the
            # spy. It is only ever stored on a widget here, so a bare sentinel
            # is enough and nothing about parsing changes.
            widgets = M.parse_line(line, state, _URL_DELEGATE)
        except Exception as e:                       # noqa: BLE001
            print(f"REFERENCE_ERROR|{lineno}|{type(e).__name__}: {e}", file=out)
            continue

        # Tables, images and partials are excluded on both sides. The
        # reference lays a table out into box-drawing characters at a fixed
        # width and re-parses the result, and renders an image into a widget.
        # The C++ parser reports all three as structure and lays out nothing,
        # by design, so there is no rendered text to compare. Its unit tests
        # assert the parsed fields instead.
        now_table = state.get("table_mode", False)
        if was_table or now_table:
            # A table's own lines, and the closing `t that emits the whole
            # rendered table at once.
            continue
        # Only outside a literal block. Inside one these are plain text, and
        # the reference's own dispatch for them sits under `if not literal`.
        if not was_literal and (line.startswith("`(") or line.startswith("`{")):
            continue

        # Merge adjacent runs in the same style, symmetrically with the C++
        # side. See ours_dump.cpp: a backslash escape splits a run there and
        # not here, because that parser copies nothing and the two halves are
        # not contiguous in the source. Characters and styles are compared;
        # where the parser happened to break a run is not.
        merged = []
        for st, part in _parts:
            if part == "":
                continue
            f = st["formatting"]
            key = "{}|{}|{}|{}|{}|{}".format(
                ("b" if f["bold"] else "-") + ("i" if f["italic"] else "-")
                + ("u" if f["underline"] else "-"),
                _color(st["fg_color"], st["default_fg"]),
                _color(st["bg_color"], st["default_bg"]),
                st["align"], st["depth"],
                "lit" if st["literal"] else "-")
            if merged and merged[-1][0] == key:
                merged[-1][1] += part
            else:
                merged.append([key, part])
        for key, text in merged:
            print(f"TEXT|{key}|{text}", file=out)
        # Text first, then links. The reference routes a link's label around
        # make_part, so the two classes cannot be interleaved faithfully. Order
        # WITHIN each class is preserved and compared; order BETWEEN them is not.
        for target, label, fields in _links:
            print(f"LINK|{label}|{target}|{fields}", file=out)
        # EOL means the reference PRODUCED A ROW for this line, which is what
        # decides page height. It is not one-per-input-line: parse_line returns
        # None for a line that renders nothing. Printing it unconditionally
        # would make every line look like a row and would silently invent
        # agreement about layout.
        # `is not None`, never truthiness. parse_line returns None for a line
        # that renders nothing, but for a field line it returns an urwid widget
        # whose __bool__ walks its contents and raises. Asking "is this true?"
        # of a widget breaks the oracle and looks exactly like a reference bug.
        if widgets is not None:
            print("EOL", file=out)


def main(argv):
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    for path in argv[1:]:
        dump(path, sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
