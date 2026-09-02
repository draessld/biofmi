#!/bin/bash
# Regenerate the committed sample panel in this directory.
#
# The output is deterministic: same seed, same tools, same bytes. Run this only
# when the panel is meant to change — the committed files are a fixture that
# tests/e2e/test_sample_panel.sh checks against, so regenerating them silently
# converts a failing test into a passing one.
#
# Keep it small. This directory is committed, and the whole point is that no
# panel large enough to matter ever enters the repository.
#
# The input comes from make_sample_eds.py rather than genrandomeds, whose
# --ref-size-mb is an integer and so cannot go below a 1 Mbp reference — about a
# 2 MB .eds, and a double-digit-MB l-EDS after the merge. Everything *derived*
# is still made by the real tools, so the fixture exercises the real code path.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
TOOLS="$ROOT/external/edsparser/build/tools"

for t in eds2leds edsparser-genpatterns; do
    [ -x "$TOOLS/$t" ] || { echo "Missing $TOOLS/$t — build the submodule first." >&2; exit 1; }
done

SEED=20260902
L=5
REF_BP=8000
GENOMES=8
SITES=700
N_PATTERNS=40
PATTERN_LEN=48          # 8 x (l+1): full chunks, no tail — and long enough to
                        # span several degenerate symbols, which is what makes
                        # CARTESIAN over-report and gives test_sample_panel.sh
                        # something to check beyond recall. At 12 the two modes
                        # returned identical entry counts on this panel.

echo "Provenance of the tools used:"
"$TOOLS/eds2leds" --version
echo

# --min-context L keeps the raw EDS close to l-EDS compliant, so the merge has
# little to do and the committed l-EDS stays small and readable.
python3 "$HERE/make_sample_eds.py" \
    --seed "$SEED" \
    --ref-bp "$REF_BP" \
    --genomes "$GENOMES" \
    --sites "$SITES" \
    --l "$L" \
    --eds "$HERE/sample.eds" \
    --seds "$HERE/sample.seds"

"$TOOLS/eds2leds" \
    -i "$HERE/sample.eds" \
    -s "$HERE/sample.seds" \
    -l "$L" \
    -o "$HERE/sample.l$L.leds"

# -s and --seed are both load-bearing.
#
#   --seed  without it the pattern set is drawn from random_device and differs
#           on every run, so the committed fixture could never be regenerated.
#   -s      without it each pattern picks alternatives independently per symbol,
#           sampling the cartesian product — which can spell a string no genome
#           carries. Such a pattern is found in CARTESIAN mode and correctly
#           *not* found in LINEAR, so test_sample_panel.sh's "LINEAR finds every
#           panel pattern" would fail on a perfectly correct index. With -s each
#           pattern walks a single path and is a real substring of a real genome.
"$TOOLS/edsparser-genpatterns" \
    -i "$HERE/sample.l$L.leds" \
    -s "$HERE/sample.l$L.seds" \
    -n "$N_PATTERNS" \
    -l "$PATTERN_LEN" \
    --seed "$SEED" \
    -o "$HERE/sample.patterns"

echo
echo "Sizes:"
du -h "$HERE"/sample.* | sort -k2
