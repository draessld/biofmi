/**
 * Structural correctness tests for BioFMI::parse_eds()
 *
 * Verifies that the five internal structures produced by parse_eds() have
 * exactly the expected values for known small EDS inputs:
 *   base_positions  — cumulative T0 length before each degenerate set
 *   set_sizes       — cumulative string count through each degenerate set
 *   offsets         — length of each individual degenerate string
 *   tloc / loc / iloc bit vectors — block boundary positions
 *
 * These tests catch regressions in parse_eds() that would produce silently
 * wrong locate() results without tripping the high-level correctness tests.
 */

#include "index/index.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
#include <vector>
#include <string>

using namespace biofmi;

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------
BioFMI build_index(const std::string& eds_str, int l) {
    std::istringstream ss(eds_str);
    EDS eds(ss);
    BioFMI idx(std::move(eds), l);
    idx.build();
    return idx;
}

template<typename T>
void assert_vec(const std::vector<T>& got, const std::vector<T>& expected,
                const char* name) {
    if (got == expected) return;
    std::cerr << "\n[FAIL] " << name << "\n  expected: [";
    for (size_t i = 0; i < expected.size(); i++) { if (i) std::cerr << ", "; std::cerr << expected[i]; }
    std::cerr << "]\n  got:      [";
    for (size_t i = 0; i < got.size(); i++) { if (i) std::cerr << ", "; std::cerr << got[i]; }
    std::cerr << "]\n";
    std::abort();
}

void assert_str(const std::string& got, const std::string& expected, const char* name) {
    if (got == expected) return;
    std::cerr << "\n[FAIL] " << name << "\n  expected: \"" << expected << "\"\n"
              << "  got:      \"" << got << "\"\n";
    std::abort();
}

// ---------------------------------------------------------------------------
// Test 1: AAATTT{G,C}AAATTT  l=3
//
// T0 = "AAATTTAAATTT"  (12 chars)
// Changes: 0=G, 1=C   cl=2
//
// Reference file:  #AAATTT#AAATTT#   (15 chars, positions 0-14)
//   tloc: 1 at every '#' → [0, 7, 14]
//
// Changes file:    #TTGAA#TTCAA#     (13 chars, positions 0-12)
//   left-ctx="TT", right-ctx="AA" for both entries
//   loc:  1 at [0, 6, 12]   (initial # + end-of-content positions)
//   iloc: 1 at [0, 12]      (initial # + end of the one degenerate set)
//
// base_positions = [6, 12]   (T0 length before first set = 6; total T0 = 12)
// set_sizes      = [2]       (one set of 2 alternatives)
// offsets        = [1, 1]    (G has length 1, C has length 1)
// ---------------------------------------------------------------------------
void test_one_degenerate_set() {
    std::cout << "Test 1: AAATTT{G,C}AAATTT  l=3... ";

    auto idx = build_index("AAATTT{G,C}AAATTT", 3);
    auto s = idx.get_snapshot();

    // cl=3: G entry = "TTT"+"G"+"AAA" (7 chars), C entry same → "#TTTGAAA#TTTCAAA#"
    assert_str(s.ref_text,     "#AAATTT#AAATTT#",   "ref_text");
    assert_str(s.changes_text, "#TTTGAAA#TTTCAAA#", "changes_text");

    assert_vec(s.base_positions, {6, 12},    "base_positions");
    assert_vec(s.set_sizes,      {2},        "set_sizes");
    assert_vec(s.offsets,        {1, 1},     "offsets");

    assert_vec(s.tloc_ones, {0, 7, 14},      "tloc_ones");
    assert_vec(s.loc_ones,  {0, 8, 16},      "loc_ones");
    assert_vec(s.iloc_ones, {0, 16},         "iloc_ones");

    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// Test 2: AAA{T,G}TTT{C,A}CCC  l=3
//
// T0 = "AAATTTCCC"  (9 chars)
// Changes: 0=T, 1=G, 2=C, 3=A   cl=2
//
// Reference file:  #AAA#TTT#CCC#   (13 chars, positions 0-12)
//   tloc: [0, 4, 8, 12]
//
// Changes file:    #AATTT#AAGTT#TTCCC#TTACC#   (25 chars, positions 0-24)
//   Set 0 {T,G}: left-ctx="AA", right-ctx="TT"
//     T entry: positions 1-5 ("AATTT"), loc[6]=1
//     G entry: positions 7-11 ("AAGTT"), loc[12]=1, iloc[12]=1
//   Set 1 {C,A}: left-ctx="TT", right-ctx="CC"
//     C entry: positions 13-17 ("TTCCC"), loc[18]=1
//     A entry: positions 19-23 ("TTACC"), loc[24]=1, iloc[24]=1
//   loc:  [0, 6, 12, 18, 24]
//   iloc: [0, 12, 24]
//
// base_positions = [3, 6, 9]
// set_sizes      = [2, 4]
// offsets        = [1, 1, 1, 1]
// ---------------------------------------------------------------------------
void test_two_degenerate_sets() {
    std::cout << "Test 2: AAA{T,G}TTT{C,A}CCC  l=3... ";

    auto idx = build_index("AAA{T,G}TTT{C,A}CCC", 3);
    auto s = idx.get_snapshot();

    // cl=3:
    //   T entry: "AAA"+"T"+"TTT" = A,A,A,T,T,T,T (7 chars) = "AAATTTT"
    //   G entry: "AAA"+"G"+"TTT" = A,A,A,G,T,T,T (7 chars) = "AAAGTTT"
    //   C entry: "TTT"+"C"+"CCC" = T,T,T,C,C,C,C (7 chars) = "TTTCCCC"
    //   A entry: "TTT"+"A"+"CCC" = T,T,T,A,C,C,C (7 chars) = "TTTACCC"
    assert_str(s.ref_text,     "#AAA#TTT#CCC#",                         "ref_text");
    assert_str(s.changes_text, "#AAATTTT#AAAGTTT#TTTCCCC#TTTACCC#",    "changes_text");

    assert_vec(s.base_positions, {3, 6, 9},       "base_positions");
    assert_vec(s.set_sizes,      {2, 4},           "set_sizes");
    assert_vec(s.offsets,        {1, 1, 1, 1},     "offsets");

    assert_vec(s.tloc_ones, {0, 4, 8, 12},          "tloc_ones");
    assert_vec(s.loc_ones,  {0, 8, 16, 24, 32},     "loc_ones");
    assert_vec(s.iloc_ones, {0, 16, 32},             "iloc_ones");

    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// Test 3: Multi-char alternatives — AAAA{T,GGG}TTTT  l=4
//
// T0 = "AAAATTTT"  (8 chars)
// Changes: 0=T (len 1), 1=GGG (len 3)   cl=3
//
// Reference file:  #AAAA#TTTT#   (11 chars, positions 0-10)
//   tloc: [0, 5, 10]
//
// Changes file entries (cl=3):
//   T   entry: left-ctx="AAA", right-ctx="TTT" → "AAATTT"
//   GGG entry: left-ctx="AAA", right-ctx="TTT" → "AAAGGGTT"... wait
//
// Actually:
//   context_left  = last cl=3 chars of "AAAA" = "AAA"
//   context_right = first cl=3 chars of "TTTT" = "TTT"
//
//   T   content: "AAA" + "T"   + "TTT" = "AAATTT"  (7 chars, positions 1-7, loc[7]=1)
//   GGG content: "AAA" + "GGG" + "TTT" = "AAAGGG..."
//     wait: "AAA"+"GGG"+"TTT" = 9 chars, positions 8-16(?), no
//     After T's entry: # at position 8. GGG entry starts at 9.
//     "AAA"(9-11) + "GGG"(12-14) + "TTT"(15-17) = positions 9-17, loc[17]=1, iloc[17]=1
//     # at position 18.
//
// Changes file: #AAATTT#AAAGGG...
// Wait let me recalculate: cl=3, context_length_=4
//
//   init: chan = "#", pos=1
//   T entry: chan << "AAA" << "T" << "TTT" → pos=1+3+1+3=8, loc[8]=1, chan << '#', pos=9
//     Wait: starting at pos=1, "AAA" takes positions 1,2,3, "T" takes position 4,
//     "TTT" takes positions 5,6,7. tellp()=8. loc[8]=1. Write '#'. pos=9.
//   GGG entry: chan << "AAA" << "GGG" << "TTT"
//     positions 9,10,11 = "AAA", 12,13,14 = "GGG", 15,16,17 = "TTT"
//     tellp()=18. loc[18]=1, iloc[18]=1. Write '#'. pos=19.
//
// So:
//   changes_text = "#AAATTT#AAAGGG..." wait, that's "T" + context, let me be precise:
//   Position 0: '#'
//   Positions 1-7: 'A','A','A','T','T','T','T' → "AAATTTT"? No: "AAA" + "T" + "TTT" = "AAATTT" (7 chars), positions 1-7
//   Wait: "AAA" = 3 chars, "T" = 1 char, "TTT" = 3 chars → 7 chars total
//   Position 8: '#' (loc[8]=1)
//   Positions 9-17: "AAA" + "GGG" + "TTT" = 9 chars
//   Position 18: '#' (loc[18]=1, iloc[18]=1)
//
// changes_text = "#AAATTT#AAAGGG..." hmm let me write it:
//   "#" + "AAATTT" (wait that's wrong, T is between the two AAAs and TTTs)
//   Actually: "AAA" + "T" + "TTT" = "AAATTT" (A,A,A,T,T,T,T)? No:
//   "AAA" = A,A,A (3 chars)
//   "T"   = T     (1 char)
//   "TTT" = T,T,T (3 chars)
//   Total: AAATTTT? No wait: A,A,A,T,T,T,T = "AAATTTT"? That's 7 chars but the last 3 are right-ctx "TTT".
//   So it's A-A-A-T-T-T-T (positions 1-7), where position 4 is the 'T' alternative and positions 5-7 are right-ctx "TTT".
//   So the string is "AAATTTT" (7 chars). Oh wait no, "TTT" right-ctx = 3 T's. And the content is "T" (1 T). So:
//   left-ctx: "AAA"
//   content:  "T"
//   right-ctx:"TTT"
//   Combined: "AAATTT" → no, that's "AAA" + "T" + "TTT" = "AAATTTT"?
//   A,A,A = 3 chars
//   T = 1 char (content)
//   T,T,T = 3 chars (right-ctx)
//   = "AAATTTT"? Hmm no. A-A-A-T-T-T-T (7 chars). The full content part is the 4th char (index 3 within the entry, 0-indexed).
//   Actually: position 1='A', 2='A', 3='A', 4='T' (content), 5='T', 6='T', 7='T' (right-ctx).
//   Hmm that's "AAATTTT" with the content 'T' at position 4.
//
//   Wait, "TTTT" is the right segment. first cl=3 chars = "TTT". OK so right-ctx = "TTT". Combined: A,A,A,T,T,T,T = 7 chars, i.e., "AAATTTT".
//
//   And for GGG: A,A,A,G,G,G,T,T,T = 9 chars, "AAAGGG TTT" = "AAAGGGTTT"
//
// changes_text = "#AAATTTT#AAAGGGTTT#" (19 chars, 0-18)
//
// Hmm wait. Let me recount: "#" (1) + "AAATTT" (7) + "#" (1) + "AAAGGGTTT" (9) + "#" (1) = 19 chars (positions 0-18).
// That's #AAATTTT#AAAGGGTTT# which is 19 chars.
//
// Actually "AAATTT" is wrong. Let me count explicitly:
//   A, A, A (left-ctx) = 3 chars
//   T (content) = 1 char
//   T, T, T (right-ctx) = 3 chars
//   = A,A,A,T,T,T,T = "AAATTTT" (7 chars, not "AAATTT" which is 6)
//   Wait I keep confusing myself. Let me count: 3 + 1 + 3 = 7 chars. The string is "AAA" || "T" || "TTT".
//   That's "AAATTT"? No! A-A-A-T-T-T-T has 7 characters. The string is "AAATTTT" (3 A's, then 1 T, then 3 T's = 4 T's total: AAATTTT).
//
// Hmm OK I think I was making an error. Let me be very explicit:
// "AAA" = [A, A, A]
// "T" = [T]
// "TTT" = [T, T, T]
// Concatenation: [A, A, A, T, T, T, T] = "AAATTTT" (length 7)
//
// And for "GGG":
// "AAA" = [A, A, A]
// "GGG" = [G, G, G]
// "TTT" = [T, T, T]
// Concatenation: [A, A, A, G, G, G, T, T, T] = "AAAGGGTTT" (length 9)
//
// changes_text = "#AAATTTT#AAAGGGTTT#"
//   position 0: '#' ← loc[0]=1, iloc[0]=1
//   positions 1-7: "AAATTTT" (7 chars)
//   position 8: '#' ← loc[8]=1
//   positions 9-17: "AAAGGGTTT" (9 chars)
//   position 18: '#' ← loc[18]=1, iloc[18]=1
//
// Changes file total: 19 chars (positions 0-18).
//
// base_positions: l=4, cl=3
//   Before loop: is_degenerate[0]? EDS "AAAA{T,GGG}TTTT"
//     Symbol 0 = "AAAA" (non-degen) → no pre-loop push
//   x=0 "AAAA": base_pos=4, base_positions=[4], tloc at 5, ref="#AAAA#" pos=6
//   x=1 {T,GGG}: set_sizes=[2], offsets=[1,3]
//   x=2 "TTTT": base_pos=8, base_positions=[4,8], tloc at 10, ref="#AAAA#TTTT#" pos=11
//
// Hmm wait. Let me retrace for AAAA{T,GGG}TTTT:
//
// ref_file starts with '#': pos=1
//
// x=0 "AAAA" (non-degen, l=4, cl=3):
//   base_pos += 4 → base_pos=4
//   base_positions.push_back(4) → [4]
//   ref << "AAAA": pos=5, tloc[5]=1
//   ref << '#': pos=6
//   context_left = "AAA" (last 3 of "AAAA")
//
// x=1 {T,GGG} (degen):
//   set_sizes=[2]
//   context_right: next = "TTTT", size=4 ≥ cl=3, context_right = "TTT"
//   i=0 (T): offsets=[1]
//     chan << "AAA" << "T" << "TTT": pos = 1+3+1+3 = 8, loc[8]=1
//     chan << '#': pos=9
//   i=1 (GGG): offsets=[1,3]
//     chan << "AAA" << "GGG" << "TTT": pos = 9+3+3+3 = 18, loc[18]=1, iloc[18]=1
//     chan << '#': pos=19
//
// x=2 "TTTT" (non-degen):
//   base_pos += 4 → base_pos=8
//   base_positions.push_back(8) → [4,8]
//   ref << "TTTT": pos=10, tloc[10]=1
//   ref << '#': pos=11
//   context_left = "TTT"
//
// Final:
//   base_positions = [4, 8]
//   set_sizes = [2]
//   offsets = [1, 3]
//   ref_text = "#AAAA#TTTT#" (11 chars, positions 0-10)
//   changes_text = "#AAATTTT#AAAGGGTTT#" (19 chars, positions 0-18)
//   tloc_ones = [0, 5, 10]
//   loc_ones = [0, 8, 18]
//   iloc_ones = [0, 18]
// ---------------------------------------------------------------------------
void test_multi_char_alternative() {
    std::cout << "Test 3: AAAA{T,GGG}TTTT  l=4 (multi-char alternative)... ";

    auto idx = build_index("AAAA{T,GGG}TTTT", 4);
    auto s = idx.get_snapshot();

    // cl=4:
    //   T   entry: "AAAA"+"T"+"TTTT" = A,A,A,A,T,T,T,T,T (9 chars) = "AAAATTTTT"
    //   GGG entry: "AAAA"+"GGG"+"TTTT" = A,A,A,A,G,G,G,T,T,T,T (11 chars) = "AAAAGGGTTTT"
    assert_str(s.ref_text,     "#AAAA#TTTT#",             "ref_text");
    assert_str(s.changes_text, "#AAAATTTTT#AAAAGGGTTTT#", "changes_text");

    assert_vec(s.base_positions, {4, 8},   "base_positions");
    assert_vec(s.set_sizes,      {2},      "set_sizes");
    assert_vec(s.offsets,        {1, 3},   "offsets");

    assert_vec(s.tloc_ones, {0, 5, 10},    "tloc_ones");
    assert_vec(s.loc_ones,  {0, 10, 22},   "loc_ones");
    assert_vec(s.iloc_ones, {0, 22},       "iloc_ones");

    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// Test 4: Boundary — first symbol degenerate: {G,C}AAATTT  l=3
//
// T0 = "AAATTT" (6 chars)
// Changes: 0=G, 1=C   cl=2
//
// Before loop: is_degenerate[0]=true → base_positions.push_back(0) → [0]
//
// x=0 {G,C} (degen):
//   set_sizes=[2]
//   context_left = "##" (initial 2 sentinels, no preceding reference)
//   context_right: next="AAATTT", first 2 = "AA"
//   G entry: "##" + "G" + "AA" → "##GAA" (5 chars), chan << "##GAA": pos=1+2+1+2=6, loc[6]=1
//   C entry: "##" + "C" + "AA" → "##CAA" (5 chars), pos=6+1+2+1+2=12, loc[12]=1, iloc[12]=1
//
// x=1 "AAATTT" (non-degen):
//   base_pos=6, base_positions=[0,6], ref << "AAATTT": tloc[7]=1, ref << '#': pos=8
//   context_left="TT"
//
// ref_text = "#AAATTT#" (8 chars? wait: initial '#' at pos 0, then "AAATTT" pos 1-6, tloc[7]=1, '#' at pos 7)
//   = "#AAATTT#" (8 chars, positions 0-7)
// changes_text = "#" + "##GAA" + "#" + "##CAA" + "#"
//   But '#' = CHANGE_SEPARATOR, and the "##" padding is also CHANGE_SEPARATOR.
//   The string literally contains '#' for sentinels too.
//   = "###GAA###CAA#" wait:
//     Position 0: '#' (initial)
//     Positions 1-2: '##' (2 sentinel padding)
//     Position 3: 'G'
//     Positions 4-5: 'AA'
//     Position 6: '#' (loc[6]=1)
//     Positions 7-8: '##' (2 sentinel padding)
//     Position 9: 'C'
//     Positions 10-11: 'AA'
//     Position 12: '#' (loc[12]=1, iloc[12]=1)
//   changes_text = "###GAA###CAA#" (13 chars, positions 0-12)
//
// tloc_ones = [0, 7]  (just the initial # and the one after "AAATTT")
// loc_ones = [0, 6, 12]
// iloc_ones = [0, 12]
//
// Note: '#' in changes_text at positions 1,2 are the padding sentinels.
// ---------------------------------------------------------------------------
void test_boundary_first_degenerate_structure() {
    std::cout << "Test 4: {G,C}AAATTT  l=3 (first symbol degenerate)... ";

    auto idx = build_index("{G,C}AAATTT", 3);
    auto s = idx.get_snapshot();

    // cl=3: left-ctx padded to 3 sentinels; right-ctx = first 3 of "AAATTT" = "AAA"
    //   G entry: "###"+"G"+"AAA" = '#','#','#','G','A','A','A' (7 chars)
    //   C entry: "###"+"C"+"AAA" (7 chars)
    //   changes_text = "####GAAA####CAAA#"  (17 chars)
    assert_str(s.ref_text,     "#AAATTT#",          "ref_text");
    assert_str(s.changes_text, "####GAAA####CAAA#", "changes_text");

    assert_vec(s.base_positions, {0, 6},  "base_positions");
    assert_vec(s.set_sizes,      {2},     "set_sizes");
    assert_vec(s.offsets,        {1, 1},  "offsets");

    assert_vec(s.tloc_ones, {0, 7},       "tloc_ones");
    assert_vec(s.loc_ones,  {0, 8, 16},   "loc_ones");
    assert_vec(s.iloc_ones, {0, 16},      "iloc_ones");

    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "===========================================\n";
    std::cout << "INDEX STRUCTURAL TESTS (parse_eds internals)\n";
    std::cout << "===========================================\n\n";

    try {
        test_one_degenerate_set();
        test_two_degenerate_sets();
        test_multi_char_alternative();
        test_boundary_first_degenerate_structure();

        std::cout << "\n===========================================\n";
        std::cout << "ALL STRUCTURAL TESTS PASSED\n";
        std::cout << "===========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nFAILED: " << e.what() << "\n";
        return 1;
    }
}
