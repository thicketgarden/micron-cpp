#!/usr/bin/env bash
# Copyright (C) 2026 Thicket contributors
# SPDX-License-Identifier: Apache-2.0
#
# Parity against the full frozen corpus: real pages from deployed nodes, plus
# NomadNet's own Guide. Reports a number rather than passing or failing.
#
#   bash parity/run_full_corpus.sh
#
# NOT A GATE. run_parity.sh is the gate and it must stay green. This one is a
# thermometer: the number falls as the parser gets closer to the reference, and
# it is promoted to blocking when it reaches zero. A job that is always red
# teaches people to ignore red.
#
# The corpus is pinned by SHA, like every other dependency. It is a separate
# repository because part of it is GPL-3.0 and this library is Apache-2.0; see
# that repository's LICENSING.md.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
CORPUS_REPO="https://github.com/thicketgarden/micron-cpp-corpus.git"
CORPUS_SHA="c9f98d69e9a4c50eed03fb1a7c063bcdd8139126"
WORK="${TMPDIR:-/tmp}/micron-corpus.$$"
mkdir -p "$WORK"; trap 'rm -rf "$WORK"' EXIT

PY="${MICRON_PY:-}"
if [[ -z "$PY" ]]; then
  command -v uv >/dev/null || { echo "[corpus] need uv, or MICRON_PY"; exit 1; }
  uv venv "$WORK/venv" -q || exit 1
  # urwid is pinned because the reference's own field path is version-sensitive.
  uv pip install -q --python "$WORK/venv/bin/python" nomadnet 'urwid==2.6.16' || exit 1
  PY="$WORK/venv/bin/python"
fi

git clone -q "$CORPUS_REPO" "$WORK/corpus" || { echo "[corpus] clone failed"; exit 1; }
git -C "$WORK/corpus" checkout -q "$CORPUS_SHA" || { echo "[corpus] bad pin $CORPUS_SHA"; exit 1; }
"$PY" "$WORK/corpus/scripts/generate_tier2.py" --out "$WORK/corpus/tier2-generated" >/dev/null 2>&1 || \
  echo "[corpus] tier 2 not generated (needs gh + rns); continuing without it"

find "$WORK/corpus/tier1-real-world" "$WORK/corpus/tier2-generated" \
     "$WORK/corpus/tier3-canonical" -name '*.mu' 2>/dev/null | sort > "$WORK/pages.txt"
PAGES=$(wc -l < "$WORK/pages.txt" | tr -d ' ')
[[ "$PAGES" -gt 0 ]] || { echo "[corpus] no pages"; exit 1; }

c++ -std=c++17 -O2 -I "$ROOT/src" -o "$WORK/ours_dump" \
    "$HERE/ours_dump.cpp" "$ROOT/src/Micron.cpp" || exit 1

tr '\n' '\0' < "$WORK/pages.txt" | xargs -0 "$PY" "$HERE/reference_dump.py" > "$WORK/ref.events" 2>/dev/null
tr '\n' '\0' < "$WORK/pages.txt" | xargs -0 "$WORK/ours_dump" 2>/dev/null | grep -v '^SKIP_' > "$WORK/ours.events"

ERRORS=$(grep -c REFERENCE_ERROR "$WORK/ref.events" || true)
DIFFS=$(diff -u "$WORK/ref.events" "$WORK/ours.events" | grep -cE '^[+-][^+-]' || true)

echo "[corpus] $PAGES pages · $(wc -l < "$WORK/ref.events" | tr -d ' ') reference events"
echo "[corpus] reference errors: $ERRORS  (must be 0; anything else is a harness fault, not a finding)"
echo "[corpus] DIFFERING LINES: $DIFFS"
if [[ "$DIFFS" -eq 0 && "$ERRORS" -eq 0 ]]; then
  echo "[corpus] full corpus is green. Promote this job to blocking and retire the note in ci.yml."
fi
exit 0
