#!/bin/bash
# End-to-end check over the committed sample panel in tests/data/.
#
# The other e2e tests use hand-written 40-character EDS strings, which is right
# for checking exact positions but says nothing about a panel with thousands of
# symbols, real source sets, and a merge behind it. This one exercises the whole
# pipeline on committed input:
#
#   sample.l5.leds + sample.l5.seds  ->  index  ->  locate, both modes
#
# The patterns are drawn *from* the panel, so every one of them must be found.
# That is the property worth guarding: a tool that silently produces or consumes
# the wrong thing (a stale eds2leds emitting l-EDS for strings no genome carries
# was a real incident) shows up here as patterns that stop being found, while
# the hand-written fixtures stay green.
#
# The panel is small and committed on purpose — see tests/data/README.md. No
# generated data of any size belongs in the repository.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIOFMI_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
source "$SCRIPT_DIR/helpers.sh"

DATA_DIR="$BIOFMI_ROOT/tests/data"
BUILD_TOOL=$(find_tool "biofmi-build")  || { echo "ERROR: biofmi-build not found"; exit 1; }
LOCATE_TOOL=$(find_tool "biofmi-locate") || { echo "ERROR: biofmi-locate not found"; exit 1; }
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

L=5
LEDS="$DATA_DIR/sample.l5.leds"
SEDS="$DATA_DIR/sample.l5.seds"
PATTERNS="$DATA_DIR/sample.patterns"

echo "=== sample panel (tests/data) ==="

for f in "$LEDS" "$SEDS" "$PATTERNS"; do
    if [ ! -f "$f" ]; then
        echo -e "  ${RED}FAIL${NC}: missing committed input $f"
        echo "  1 test failed"
        exit 1
    fi
done

"$BUILD_TOOL" -i "$LEDS" -l "$L" -o "$TMPDIR/idx" >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo -e "  ${RED}FAIL${NC}: could not build an index from the committed l-EDS"
    echo "  1 test failed"
    exit 1
fi

n_patterns() { grep -c '[^[:space:]]' "$PATTERNS"; }

test_index_files_present() {
    for ext in ri ci loc iloc tloc abp ss aof d2g meta; do
        assert_file_exists "$TMPDIR/idx/index.$ext" "index.$ext built from the sample panel" || return 1
    done
}

# Every pattern was cut out of this panel, so CARTESIAN must find all of them.
test_cartesian_finds_every_pattern() {
    "$LOCATE_TOOL" -i "$TMPDIR/idx" -l "$L" -P "$PATTERNS" -o "$TMPDIR/cart.txt" >/dev/null 2>&1
    assert_exit_code 0 $? "cartesian query exits 0" || return 1
    local misses
    misses=$(grep -c "No occurrences found" "$TMPDIR/cart.txt" || true)
    if [ "$misses" -ne 0 ]; then
        echo -e "  ${RED}FAIL${NC}: $misses of $(n_patterns) panel patterns were not found in CARTESIAN mode"
        return 1
    fi
}

# LINEAR is a subset of CARTESIAN, never a superset — attaching sources removes
# paths no genome carries, it never invents one. Recall is unaffected, so the
# same patterns must still all be found.
test_linear_finds_every_pattern() {
    "$LOCATE_TOOL" -i "$TMPDIR/idx" -l "$L" -s "$SEDS" -P "$PATTERNS" -o "$TMPDIR/lin.txt" >/dev/null 2>&1
    assert_exit_code 0 $? "linear query exits 0" || return 1
    local misses
    misses=$(grep -c "No occurrences found" "$TMPDIR/lin.txt" || true)
    if [ "$misses" -ne 0 ]; then
        echo -e "  ${RED}FAIL${NC}: $misses of $(n_patterns) panel patterns were not found in LINEAR mode"
        return 1
    fi
}

# LINEAR must be a strict subset here, not merely a subset. The patterns are 48
# characters — eight full chunks at l=5 — so each spans several degenerate
# symbols, and CARTESIAN pairs every alternative of one with every alternative of
# the next regardless of whether a genome carries the combination. That is issue
# B4, reproduced on 80 KB. If these counts ever come out equal, the source
# intersection has stopped doing anything, which no recall test would notice.
test_linear_strictly_under_cartesian() {
    local c l
    c=$(grep -c '^[0-9]*\[' "$TMPDIR/cart.txt" || true)
    l=$(grep -c '^[0-9]*\[' "$TMPDIR/lin.txt" || true)
    echo "    entries: cartesian=$c  linear=$l"
    if [ "$l" -gt "$c" ]; then
        echo -e "  ${RED}FAIL${NC}: LINEAR reported $l entries, more than CARTESIAN's $c"
        return 1
    fi
    if [ "$l" -ge "$c" ]; then
        echo -e "  ${RED}FAIL${NC}: LINEAR ($l) did not prune anything from CARTESIAN ($c) —"
        echo -e "         the source intersection is not constraining the search"
        return 1
    fi
}

# A pattern shorter than l+1 is a legal query as of 2026-09-02, and on a panel
# this size it should still answer quickly rather than throw.
test_short_pattern_is_answered() {
    local out
    out=$("$LOCATE_TOOL" -i "$TMPDIR/idx" -l "$L" -p "ACGT" 2>/dev/null)
    assert_exit_code 0 $? "a 4-character pattern at l=5 exits 0" || return 1
    assert_contains "$out" "\[|No occurrences found" "short pattern was searched, not rejected" || return 1
}

run_test "index builds from committed l-EDS"   test_index_files_present
run_test "CARTESIAN finds every panel pattern" test_cartesian_finds_every_pattern
run_test "LINEAR finds every panel pattern"    test_linear_finds_every_pattern
run_test "LINEAR prunes what CARTESIAN admits" test_linear_strictly_under_cartesian
run_test "short pattern answered, not refused" test_short_pattern_is_answered

print_summary
