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
_orig_render_table = M.render_table
_tables = []


def _spy_render_table(lines, state, url_delegate):
    """The reference's own table input, captured before it is laid out.

    render_table receives the buffered rows verbatim along with the alignment
    and width the opening `t declared, which is exactly what the C++ parser
    reports. What it produces from them is box-drawing text for a terminal and
    is not compared."""
    _tables.append((list(lines), state.get("table_align"), state.get("table_maxwidth")))
    return _orig_render_table(lines, state, url_delegate)


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
M.render_table = _spy_render_table


def _unwrap(widgets):
    """parse_image pads its widget when depth > 0, keeping the original."""
    if not widgets:
        return None
    w = widgets[0]
    return getattr(w, "_contained_image", w)


def _walk(widgets, depth=0):
    """Every widget in a returned tree. The reference wraps fields in
    FormColumns and pads dividers, so the thing that carries the data is
    usually not the top-level widget."""
    seen = set()
    for w in (widgets or []):
        # A QUEUE, not a stack. A stack visits children in reverse, so fields
        # came out right-to-left and every multi-field line differed on order
        # alone.
        from collections import deque
        stack = deque([(w, 0)])
        while stack:
            o, d = stack.popleft()
            # The same widget is reachable through both contents and
            # original_widget, so identity has to gate it or every field is
            # reported twice.
            if d > 5 or id(o) in seen:
                continue
            seen.add(id(o))
            yield o
            for attr in ("contents", "original_widget", "_original_widget"):
                v = getattr(o, attr, None)
                if v is None:
                    continue
                if isinstance(v, list):
                    for e in v:
                        stack.append((e[0] if isinstance(e, tuple) else e, d + 1))
                elif v is not o:
                    stack.append((v, d + 1))


def _emit_divider(widgets, out):
    for o in _walk(widgets):
        if type(o).__name__ == "Divider":
            print(f"DIVIDER|{getattr(o, 'div_char', '')}", file=out)
            return


def _emit_fields(widgets, out):
    """Fields as the reference actually built them.

    This is the comparison that was missing. Fields and dividers were skipped on
    both sides because the reference returns them as widgets rather than through
    make_part, so parity was green over them BY CONSTRUCTION: the corpus already
    contained masked fields, pre-checked boxes and multi-byte dividers, and none
    of it was ever diffed."""
    for o in _walk(widgets):
        n = type(o).__name__
        if not hasattr(o, "field_name"):
            continue
        name = getattr(o, "field_name", "") or ""
        if n in ("CheckBox", "RadioButton"):
            kind = "check" if n == "CheckBox" else "radio"
            try: label = o.get_label()
            except Exception: label = ""
            print("FIELD|{}|{}|{}|{}|-|{}".format(
                kind, name, getattr(o, "field_value", "") or "", label,
                "checked" if o.get_state() else "-"), file=out)
        else:
            mask = getattr(o, "_mask", None)
            # The real text, not the masked display: masking is a display
            # property and the value underneath is what the parser reports.
            try: val = o.get_edit_text()
            except Exception: val = ""
            print("FIELD|text|{}|{}||{}|-".format(
                name, val or "", "masked" if mask else "-"), file=out)


def _emit_image(widgets, out):
    w = _unwrap(widgets)
    if w is None or not hasattr(w, "image_url"):
        return
    def prop(name):
        v = getattr(w, name, None)
        return "" if v is None else str(v)
    align = getattr(w, "align", None)
    print("IMAGE|{}|{}|{}|{}|{}".format(
        getattr(w, "image_alt", "") or "", getattr(w, "image_url", "") or "",
        prop("width"), prop("height"), align if align else "-"), file=out)


def _emit_partial(widgets, out):
    w = _unwrap(widgets)
    if w is None or not hasattr(w, "partial_url"):
        return
    refresh = getattr(w, "partial_refresh", None)
    # Stored as a float, and the reference discards anything under a second.
    refresh = "" if refresh is None else (str(int(refresh)) if float(refresh).is_integer() else str(refresh))
    fields = getattr(w, "partial_fields", None) or []
    if isinstance(fields, str):
        fields = [fields]
    print("PARTIAL|{}|{}|{}".format(
        getattr(w, "partial_url", "") or "", refresh,
        "|".join(f for f in fields if f)), file=out)


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
            # SUSPECT THIS FILE FIRST. The parse state holds urwid widgets, so
            # anything here that copies it or asks it for a boolean raises from
            # inside urwid and reads as the reference being broken. A reference
            # error is a harness fault until proven otherwise, and it is not a
            # reason to pin a dependency: measure across versions and compare
            # hashes instead. A pin added on a hunch outlives the hunch.
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
            # A table's rows are reported, its LAYOUT is not: the reference
            # turns them into box-drawing text at a fixed width, which is a
            # rendering decision the C++ parser deliberately does not make.
            for rows, align, maxw in _tables:
                print("TABLE_BEGIN|{}|{}".format(
                    {"l": "left", "c": "center", "r": "right"}.get(align, "-"),
                    maxw if maxw else 0), file=out)
                for r in rows:
                    print(f"TABLE_ROW|{r}", file=out)
                print("TABLE_END", file=out)
            _tables.clear()
            continue

        # Only outside a literal block. Inside one these are plain text, and
        # the reference's own dispatch for them sits under `if not literal`.
        if not was_literal and line.startswith("`("):
            _emit_image(widgets, out)
            continue
        if not was_literal and line.startswith("-"):
            _emit_divider(widgets, out)
            # A divider IS a row, and the reference returns a widget for it, so
            # the row marker still belongs here. Skipping it made every divider
            # line differ by one EOL.
            if widgets is not None:
                print("EOL", file=out)
            continue
        # Fields are emitted AFTER the line's text, alongside the links, for
        # the same reason: they come off a widget tree while text comes through
        # make_part, so where they interleave is not recoverable.
        pending_fields = (not was_literal and "`<" in line)
        if not was_literal and line.startswith("`{"):
            _emit_partial(widgets, out)
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
        if pending_fields:
            _emit_fields(widgets, out)
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
