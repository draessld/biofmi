#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIOFMI_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
source "$SCRIPT_DIR/helpers.sh"

DATA_DIR="$BIOFMI_ROOT/tests/e2e/data"
TOOL=$(find_tool "biofmi-build") || { echo "ERROR: biofmi-build not found"; exit 1; }
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

echo "=== biofmi-build ==="

test_build_basic_succeeds() {
    "$TOOL" -i "$DATA_DIR/simple_l5.eds" -l 5 -o "$TMPDIR/idx" >/dev/null 2>&1
    assert_exit_code 0 $? "exits 0 on valid l-EDS input" || return 1
}

test_build_creates_index_files() {
    "$TOOL" -i "$DATA_DIR/simple_l5.eds" -l 5 -o "$TMPDIR/idx2" >/dev/null 2>&1
    for ext in ri ci loc iloc tloc abp ss aof meta; do
        assert_file_exists "$TMPDIR/idx2/index.$ext" "index file index.$ext created" || return 1
    done
}

test_build_missing_input_fails() {
    "$TOOL" -i "$TMPDIR/nonexistent.eds" -l 5 -o "$TMPDIR/idx3" 2>/dev/null
    local code=$?
    [ $code -ne 0 ] || { echo -e "  ${RED}FAIL${NC}: missing input — expected non-zero exit, got 0"; return 1; }
}

test_build_missing_context_length_fails() {
    "$TOOL" -i "$DATA_DIR/simple_l5.eds" -o "$TMPDIR/idx4" 2>/dev/null
    local code=$?
    [ $code -ne 0 ] || { echo -e "  ${RED}FAIL${NC}: missing -l — expected non-zero exit, got 0"; return 1; }
}

# The l-EDS check is per context SEGMENT, a maximal run of regular symbols:
# {CGCG}{A}{TGCC} is one context of 9, whichever way it is written.
test_build_split_run_is_one_context() {
    printf '{ACGTA}{A,C}{CGCG}{A}{TGCC}{G,T}{TTTTT}' > "$TMPDIR/split.eds"
    "$TOOL" -i "$TMPDIR/split.eds" -l 5 -o "$TMPDIR/idx_split" >/dev/null 2>&1
    assert_exit_code 0 $? "split run of 9 accepted at l=5" || return 1
    "$TOOL" -i "$TMPDIR/split.eds" -l 10 -o "$TMPDIR/idx_split10" >/dev/null 2>&1
    local code=$?
    [ $code -ne 0 ] || { echo -e "  ${RED}FAIL${NC}: split run of 9 accepted at l=10"; return 1; }
}

test_build_adjacent_degenerate_fails() {
    printf '{ACGTA}{A,C}{G,T}{TTTTT}' > "$TMPDIR/adj.eds"
    "$TOOL" -i "$TMPDIR/adj.eds" -l 3 -o "$TMPDIR/idx_adj" >/dev/null 2>&1
    local code=$?
    [ $code -ne 0 ] || { echo -e "  ${RED}FAIL${NC}: adjacent degenerate symbols accepted"; return 1; }
}

run_test "basic build succeeds"                  test_build_basic_succeeds
run_test "split regular run is one context"      test_build_split_run_is_one_context
run_test "adjacent degenerate symbols rejected"  test_build_adjacent_degenerate_fails
run_test "build creates all index files"         test_build_creates_index_files
run_test "missing input exits non-zero"          test_build_missing_input_fails
run_test "missing context length exits non-zero" test_build_missing_context_length_fails

print_summary
