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
    """Presence is what matters; the reference only stores it on a widget."""
    def build_url(self, url, **kwargs): return url


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

    MicronParser.py:820 appends (linkspec, link_label) straight to the output
    list instead, so label and target are paired here and nowhere else."""
    out = _orig_make_output(state, line, url_delegate, pre_escape)
    if isinstance(out, list):
        for entry in out:
            if isinstance(entry, tuple) and len(entry) == 2 and isinstance(entry[0], M.LinkSpec):
                # link_fields is stored split on "|" (MicronParser.py:816-818)
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
        try:
            # A url_delegate must be present or the reference never constructs a
            # LinkSpec (MicronParser.py:813), and link targets never reach the
            # spy. It is only ever stored on a widget here, so a bare sentinel
            # is enough and nothing about parsing changes.
            widgets = M.parse_line(line, state, _URL_DELEGATE)
        except Exception as e:                       # noqa: BLE001
            print(f"REFERENCE_ERROR|{lineno}|{type(e).__name__}: {e}", file=out)
            continue

        for st, part in _parts:
            if part == "":
                continue
            f = st["formatting"]
            flags = ("b" if f["bold"] else "-") + ("i" if f["italic"] else "-") \
                  + ("u" if f["underline"] else "-")
            print("TEXT|{}|{}|{}|{}|{}|{}|{}".format(
                flags,
                _color(st["fg_color"], st["default_fg"]),
                _color(st["bg_color"], st["default_bg"]),
                st["align"], st["depth"],
                "lit" if st["literal"] else "-",
                part), file=out)
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
