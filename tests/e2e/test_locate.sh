#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIOFMI_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
source "$SCRIPT_DIR/helpers.sh"

DATA_DIR="$BIOFMI_ROOT/tests/e2e/data"
EXPECTED_DIR="$SCRIPT_DIR/expected/locate"
BUILD_TOOL=$(find_tool "biofmi-build") || { echo "ERROR: biofmi-build not found"; exit 1; }
LOCATE_TOOL=$(find_tool "biofmi-locate") || { echo "ERROR: biofmi-locate not found"; exit 1; }
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# Build shared index used by all locate tests.
"$BUILD_TOOL" -i "$DATA_DIR/simple_l5.eds" -l 5 -o "$TMPDIR/idx" >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: failed to build index for locate tests"
    exit 1
fi

echo "=== biofmi-locate ==="

test_locate_known_pattern_has_results() {
    local out
    out=$("$LOCATE_TOOL" -i "$TMPDIR/idx" -l 5 -p "ACGTTG" 2>/dev/null)
    assert_exit_code 0 $? "exits 0 on matching pattern" || return 1
    assert_contains "$out" "\[" "output contains occurrence lines" || return 1
}

test_locate_no_match_pattern() {
    local out
    out=$("$LOCATE_TOOL" -i "$TMPDIR/idx" -l 5 -p "XXXXXXXXXX" 2>/dev/null)
    assert_exit_code 0 $? "exits 0 on non-matching pattern" || return 1
    assert_contains "$out" "No occurrences found" "no-match message present" || return 1
}

test_locate_output_to_file() {
    "$LOCATE_TOOL" -i "$TMPDIR/idx" -l 5 -p "ACGTTG" -o "$TMPDIR/result.txt" 2>/dev/null
    assert_exit_code 0 $? "exits 0 when writing to file" || return 1
    assert_file_exists "$TMPDIR/result.txt" "output file created" || return 1
    assert_not_empty "$TMPDIR/result.txt" "output file not empty" || return 1
}

test_locate_empty_pattern_errors() {
    local out
    # The empty pattern is the only refused length. Arbitrary lengths landed
    # 2026-08-30 (so 9 characters at l=5 is fine) and the l+1 floor went with
    # the short-chunk guard, so 5 characters at l=5 is a query too.
    out=$("$LOCATE_TOOL" -i "$TMPDIR/idx" -l 5 -p "" 2>/dev/null)
    assert_contains "$out" "Error|error|non-empty" "error message for the empty pattern" || return 1
}

test_locate_below_l_plus_one_is_valid() {
    local out
    # 5 characters, l+1 = 6: one short chunk, searched rather than rejected.
    out=$("$LOCATE_TOOL" -i "$TMPDIR/idx" -l 5 -p "ACGTT" 2>/dev/null)
    assert_exit_code 0 $? "exits 0 below l+1" || return 1
    assert_contains "$out" "\\[" "occurrences reported for a pattern shorter than l+1" || return 1
}

test_locate_non_multiple_length_is_valid() {
    local out
    # 9 characters, l+1 = 6: a chunk of 6 and a tail of 3.
    out=$("$LOCATE_TOOL" -i "$TMPDIR/idx" -l 5 -p "ACGTTACGT" 2>/dev/null)
    assert_exit_code 0 $? "exits 0 on a non-multiple length" || return 1
    assert_contains "$out" "Pattern: ACGTTACGT" "pattern was searched, not rejected" || return 1
}

test_locate_missing_index_fails() {
    "$LOCATE_TOOL" -i "$TMPDIR/nonexistent_idx" -l 5 -p "ACGTT" 2>/dev/null
    local code=$?
    [ $code -ne 0 ] || { echo -e "  ${RED}FAIL${NC}: missing index — expected non-zero exit, got 0"; return 1; }
}

test_locate_pattern_file() {
    printf "ACGTTG\nXXXXXXXXXX\n" > "$TMPDIR/patterns.txt"
    "$LOCATE_TOOL" -i "$TMPDIR/idx" -l 5 -P "$TMPDIR/patterns.txt" -o "$TMPDIR/batch_result.txt" 2>/dev/null
    assert_exit_code 0 $? "exits 0 on pattern file input" || return 1
    assert_file_exists "$TMPDIR/batch_result.txt" "batch result file created" || return 1
    assert_file_contains "$TMPDIR/batch_result.txt" "ACGTTG" "result contains ACGTTG pattern header" || return 1
    assert_file_contains "$TMPDIR/batch_result.txt" "No occurrences found" "result contains no-match entry for XXXXXXXXXX" || return 1
}

test_locate_expected_result() {
    "$LOCATE_TOOL" -i "$TMPDIR/idx" -l 5 -p "ACGTTG" -o "$TMPDIR/acgtt_result.txt" 2>/dev/null
    # Sort occurrence lines (skip Pattern: header, strip trailing blank line) for deterministic comparison.
    { head -1 "$TMPDIR/acgtt_result.txt"; tail -n +2 "$TMPDIR/acgtt_result.txt" | grep -v '^$' | sort; } > "$TMPDIR/acgtt_sorted.txt"
    assert_file_equal "$TMPDIR/acgtt_sorted.txt" "$EXPECTED_DIR/simple.l5.ACGTTG.txt" "ACGTTG locate matches expected" || return 1
}

run_test "locate known pattern returns results"     test_locate_known_pattern_has_results
run_test "locate non-matching pattern reports none" test_locate_no_match_pattern
run_test "locate writes results to output file"     test_locate_output_to_file
run_test "empty pattern gives error"                test_locate_empty_pattern_errors
run_test "length below l+1 is accepted"             test_locate_below_l_plus_one_is_valid
run_test "non-multiple length is accepted"          test_locate_non_multiple_length_is_valid
run_test "missing index exits non-zero"             test_locate_missing_index_fails
run_test "batch locate via pattern file"            test_locate_pattern_file
run_test "ACGTTG locate result matches expected"    test_locate_expected_result

# ---------------------------------------------------------------------------
# Correctness tests against simple_l3.eds = AAATTT{G,C}AAATTT (l=3)
# Expected values are hand-computed from the locate spec (docs/locate_spec.md):
# T0 = "AAATTT" + "AAATTT" at 0..11, the symbol's two alternatives are global
# changes 0 (G) and 1 (C), and its base position is 6.
#
# The patterns are four characters, not three. |P| must be at least l+1 = 4;
# these were written when the chunk size was l, and stayed three characters
# through the off-by-one fix, so the whole block was asserting against an error
# message. It went unnoticed because find_tool() resolved PATH first and the
# installed binary was failing them for its own, different reason.
# ---------------------------------------------------------------------------
L3_IDX="$TMPDIR/l3_idx"
"$BUILD_TOOL" -i "$DATA_DIR/simple_l3.eds" -l 3 -o "$L3_IDX" >/dev/null 2>&1

_l3_pattern_test() {
    local pattern="$1" expected_file="$2"
    local result_file="$TMPDIR/l3_${pattern}.txt"
    "$LOCATE_TOOL" -i "$L3_IDX" -l 3 -p "$pattern" -o "$result_file" 2>/dev/null
    # Sort occurrence lines for deterministic comparison (pattern header stays on line 1)
    { head -1 "$result_file"; tail -n +2 "$result_file" | grep -v '^$' | sort; } > "${result_file}.sorted"
    assert_file_equal "${result_file}.sorted" "$EXPECTED_DIR/$expected_file" "l3 pattern '$pattern' matches expected"
}

test_l3_ref_only_AAAT()      { _l3_pattern_test "AAAT" "simple_l3.AAAT.txt"; }
test_l3_ref_only_ATTT()      { _l3_pattern_test "ATTT" "simple_l3.ATTT.txt"; }
test_l3_ref_to_change_TTTG() { _l3_pattern_test "TTTG" "simple_l3.TTTG.txt"; }
test_l3_ref_to_change_TTTC() { _l3_pattern_test "TTTC" "simple_l3.TTTC.txt"; }
test_l3_change_start_GAAA()  { _l3_pattern_test "GAAA" "simple_l3.GAAA.txt"; }
test_l3_change_start_CAAA()  { _l3_pattern_test "CAAA" "simple_l3.CAAA.txt"; }

run_test "l3 ref-only:    AAAT"        test_l3_ref_only_AAAT
run_test "l3 ref-only:    ATTT"        test_l3_ref_only_ATTT
run_test "l3 ref→change:  TTTG"        test_l3_ref_to_change_TTTG
run_test "l3 ref→change:  TTTC"        test_l3_ref_to_change_TTTC
run_test "l3 change-start: GAAA"       test_l3_change_start_GAAA
run_test "l3 change-start: CAAA"       test_l3_change_start_CAAA

print_summary
