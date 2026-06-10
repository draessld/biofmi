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
    out=$("$LOCATE_TOOL" -i "$TMPDIR/idx" -l 5 -p "ACGTT" 2>/dev/null)
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
    "$LOCATE_TOOL" -i "$TMPDIR/idx" -l 5 -p "ACGTT" -o "$TMPDIR/result.txt" 2>/dev/null
    assert_exit_code 0 $? "exits 0 when writing to file" || return 1
    assert_file_exists "$TMPDIR/result.txt" "output file created" || return 1
    assert_not_empty "$TMPDIR/result.txt" "output file not empty" || return 1
}

test_locate_invalid_pattern_length_errors() {
    local out
    # 9 characters is not a multiple of l=5
    out=$("$LOCATE_TOOL" -i "$TMPDIR/idx" -l 5 -p "ACGTTACGT" 2>/dev/null)
    assert_contains "$out" "Error|error|must be" "error message for invalid-length pattern" || return 1
}

test_locate_missing_index_fails() {
    "$LOCATE_TOOL" -i "$TMPDIR/nonexistent_idx" -l 5 -p "ACGTT" 2>/dev/null
    local code=$?
    [ $code -ne 0 ] || { echo -e "  ${RED}FAIL${NC}: missing index — expected non-zero exit, got 0"; return 1; }
}

test_locate_pattern_file() {
    printf "ACGTT\nXXXXXXXXXX\n" > "$TMPDIR/patterns.txt"
    "$LOCATE_TOOL" -i "$TMPDIR/idx" -l 5 -P "$TMPDIR/patterns.txt" -o "$TMPDIR/batch_result.txt" 2>/dev/null
    assert_exit_code 0 $? "exits 0 on pattern file input" || return 1
    assert_file_exists "$TMPDIR/batch_result.txt" "batch result file created" || return 1
    assert_file_contains "$TMPDIR/batch_result.txt" "ACGTT" "result contains ACGTT pattern header" || return 1
    assert_file_contains "$TMPDIR/batch_result.txt" "No occurrences found" "result contains no-match entry for XXXXXXXXXX" || return 1
}

test_locate_expected_result() {
    "$LOCATE_TOOL" -i "$TMPDIR/idx" -l 5 -p "ACGTT" -o "$TMPDIR/acgtt_result.txt" 2>/dev/null
    # Sort occurrence lines (skip Pattern: header, strip trailing blank line) for deterministic comparison.
    { head -1 "$TMPDIR/acgtt_result.txt"; tail -n +2 "$TMPDIR/acgtt_result.txt" | grep -v '^$' | sort; } > "$TMPDIR/acgtt_sorted.txt"
    assert_file_equal "$TMPDIR/acgtt_sorted.txt" "$EXPECTED_DIR/simple.l5.ACGTT.txt" "ACGTT locate matches expected" || return 1
}

run_test "locate known pattern returns results"     test_locate_known_pattern_has_results
run_test "locate non-matching pattern reports none" test_locate_no_match_pattern
run_test "locate writes results to output file"     test_locate_output_to_file
run_test "invalid pattern length gives error"       test_locate_invalid_pattern_length_errors
run_test "missing index exits non-zero"             test_locate_missing_index_fails
run_test "batch locate via pattern file"            test_locate_pattern_file
run_test "ACGTT locate result matches expected"     test_locate_expected_result

# ---------------------------------------------------------------------------
# Correctness tests against simple_l3.eds = AAATTT{G,C}AAATTT (l=3)
# Expected values are hand-computed from the locate spec (docs/locate_spec.md).
# These tests FAIL until the position/change-index bugs are fixed.
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

test_l3_ref_only_AAA()      { _l3_pattern_test "AAA" "simple_l3.AAA.txt"; }
test_l3_ref_only_TTT()      { _l3_pattern_test "TTT" "simple_l3.TTT.txt"; }
test_l3_ref_to_change_TTG() { _l3_pattern_test "TTG" "simple_l3.TTG.txt"; }
test_l3_ref_to_change_TTC() { _l3_pattern_test "TTC" "simple_l3.TTC.txt"; }
test_l3_change_start_GAA()  { _l3_pattern_test "GAA" "simple_l3.GAA.txt"; }
test_l3_change_start_CAA()  { _l3_pattern_test "CAA" "simple_l3.CAA.txt"; }

run_test "l3 ref-only:    AAA"         test_l3_ref_only_AAA
run_test "l3 ref-only:    TTT"         test_l3_ref_only_TTT
run_test "l3 ref→change:  TTG"         test_l3_ref_to_change_TTG
run_test "l3 ref→change:  TTC"         test_l3_ref_to_change_TTC
run_test "l3 change-start: GAA"        test_l3_change_start_GAA
run_test "l3 change-start: CAA"        test_l3_change_start_CAA

print_summary
