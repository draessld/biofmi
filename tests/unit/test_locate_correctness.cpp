/**
 * Correctness tests for BioFMI::locate() and BioFMI::count()
 *
 * Ground truth: brute-force search over all expanded EDS paths.
 * Spec: docs/locate_spec.md
 *
 * Position semantics (0-based):
 *   - Match starts in reference  -> T0 index of first matching char
 *   - Match starts in a change   -> base_position_of_set + offset_within_alternative
 * Changes: 0-based global indices across all alternatives of all degenerate sets.
 */

#include "index/index.hpp"
#include <cassert>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace biofmi;

// ---------------------------------------------------------------------------
// Brute-force oracle
// ---------------------------------------------------------------------------

struct OccInfo {
    int position;
    std::vector<int> changes;

    bool operator<(const OccInfo& o) const {
        if (position != o.position) return position < o.position;
        return changes < o.changes;
    }
    bool operator==(const OccInfo& o) const {
        return position == o.position && changes == o.changes;
    }
};

struct CharInfo {
    bool is_ref;
    int position;    // T0 pos (is_ref) or base_pos+offset (!is_ref)
    int change_idx;  // -1 if is_ref
};

struct Path {
    std::string str;
    std::vector<CharInfo> chars;
};

/**
 * Expand the EDS into all possible concrete strings, tracking per-character
 * position and change-index metadata.
 */
std::vector<Path> expand_eds(const EDS& eds) {
    std::vector<Path> paths = {{"", {}}};
    int t0_pos = 0;
    int global_change_idx = 0;

    for (size_t sym = 0; sym < eds.length(); sym++) {
        StringSet symbol = eds.read_symbol(sym);
        bool is_degen = (symbol.size() > 1);

        if (!is_degen) {
            // Non-degenerate: append to every existing path
            const std::string& s = symbol[0];
            for (auto& p : paths) {
                for (size_t k = 0; k < s.size(); k++) {
                    p.str += s[k];
                    p.chars.push_back({true, t0_pos + (int)k, -1});
                }
            }
            t0_pos += (int)s.size();
        } else {
            // Degenerate: branch for each alternative
            int base_pos = t0_pos;
            std::vector<Path> new_paths;
            for (size_t alt = 0; alt < symbol.size(); alt++) {
                const std::string& a = symbol[alt];
                int cidx = global_change_idx + (int)alt;
                for (const auto& p : paths) {
                    Path np = p;
                    for (size_t k = 0; k < a.size(); k++) {
                        np.str += a[k];
                        np.chars.push_back({false, base_pos + (int)k, cidx});
                    }
                    new_paths.push_back(std::move(np));
                }
            }
            paths = std::move(new_paths);
            global_change_idx += (int)symbol.size();
        }
    }

    return paths;
}

/**
 * Brute-force locate: search every expanded path for pattern, collect
 * deduplicated (position, changes) occurrences.
 */
std::set<OccInfo> brute_force_locate(const EDS& eds, const std::string& pattern) {
    auto paths = expand_eds(eds);
    std::set<OccInfo> results;
    int plen = (int)pattern.size();

    for (const auto& p : paths) {
        if ((int)p.str.size() < plen) continue;
        for (int i = 0; i <= (int)p.str.size() - plen; i++) {
            if (p.str.substr(i, plen) != pattern) continue;

            OccInfo occ;
            occ.position = p.chars[i].position;

            // Collect ordered, deduplicated change indices in this match
            for (int j = i; j < i + plen; j++) {
                if (!p.chars[j].is_ref) {
                    int cid = p.chars[j].change_idx;
                    if (occ.changes.empty() || occ.changes.back() != cid) {
                        occ.changes.push_back(cid);
                    }
                }
            }
            results.insert(occ);
        }
    }
    return results;
}

/** Convert BioFMI::ResultMap to a comparable set<OccInfo>. */
std::set<OccInfo> collect_result(const BioFMI::ResultMap& rm) {
    std::set<OccInfo> out;
    for (const auto& [seq_id, occs] : rm) {
        for (const auto& [pos, changes] : occs) {
            out.insert({(int)pos, changes});
        }
    }
    return out;
}

/** Assert two sets are equal; print a diff and abort if not. */
void assert_equal(const std::set<OccInfo>& expected,
                  const std::set<OccInfo>& got,
                  const std::string& pattern,
                  const std::string& test_name) {
    if (expected == got) return;

    std::cerr << "\n[FAIL] " << test_name << "  pattern='" << pattern << "'\n";
    for (const auto& o : expected) {
        if (!got.count(o)) {
            std::cerr << "  MISSING  pos=" << o.position << " changes=[";
            for (int c : o.changes) std::cerr << c << " ";
            std::cerr << "]\n";
        }
    }
    for (const auto& o : got) {
        if (!expected.count(o)) {
            std::cerr << "  SPURIOUS pos=" << o.position << " changes=[";
            for (int c : o.changes) std::cerr << c << " ";
            std::cerr << "]\n";
        }
    }
    std::abort();  // not stripped by -DNDEBUG; ensures ctest sees a real failure
}

// ---------------------------------------------------------------------------
// Helper: build index in-memory from an EDS string
// ---------------------------------------------------------------------------
BioFMI build_index(const std::string& eds_str, int l) {
    std::istringstream ss(eds_str);
    EDS eds(ss);
    BioFMI idx(std::move(eds), l);
    idx.build();
    return idx;
}

EDS make_eds(const std::string& eds_str) {
    std::istringstream ss(eds_str);
    return EDS(ss);
}

// ---------------------------------------------------------------------------
// Test 1: Invalid pattern length -> exception
// ---------------------------------------------------------------------------
void test_error_pattern_too_short() {
    std::cout << "Test 1a: pattern shorter than l -> exception... ";

    // l=3, so minimum pattern length is 3
    BioFMI idx = build_index("AAATTT{G,C}AAATTT", 3);

    bool threw = false;
    try {
        idx.locate("AA");   // length 2 < l=3
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw && "Expected exception for pattern shorter than l");
    std::cout << "PASSED\n";
}

void test_error_pattern_not_multiple_of_l() {
    std::cout << "Test 1b: pattern length not multiple of l -> exception... ";

    BioFMI idx = build_index("AAATTT{G,C}AAATTT", 3);

    bool threw = false;
    try {
        idx.locate("AAAA");  // length 4, not multiple of 3
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw && "Expected exception for pattern length not multiple of l");
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// Test 2: Pattern not in EDS -> empty result
// ---------------------------------------------------------------------------
void test_no_match() {
    std::cout << "Test 2: pattern not in EDS -> empty result... ";

    // EDS only contains A,T,G,C — search for 'X' (not in alphabet) or a
    // combination that never occurs.
    BioFMI idx = build_index("AAATTT{G,C}AAATTT", 3);

    auto result = idx.locate("GGG");  // GGG: only one G ever appears
    assert(result.empty() && "GGG should not be found");
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// Test 3: Pure reference matches (no changes)
// ---------------------------------------------------------------------------
void test_reference_only_matches() {
    std::cout << "Test 3: pure reference matches... ";

    // EDS: AAATTT{G,C}AAATTT  l=3
    // T0 = AAATTTAAATTT
    // Changes: 0=G, 1=C
    const std::string eds_str = "AAATTT{G,C}AAATTT";
    BioFMI idx = build_index(eds_str, 3);
    EDS eds = make_eds(eds_str);

    // "AAA" appears at T0[0] and T0[6]
    {
        auto expected = brute_force_locate(eds, "AAA");
        auto got = collect_result(idx.locate("AAA"));
        assert_equal(expected, got, "AAA", "test_reference_only_matches");

        // Both occurrences should have empty changes list
        for (const auto& o : got) {
            assert(o.changes.empty() && "Pure reference match should have no changes");
        }
        assert(got.size() == 2);
    }

    // "TTT" appears at T0[3] and T0[9]
    {
        auto expected = brute_force_locate(eds, "TTT");
        auto got = collect_result(idx.locate("TTT"));
        assert_equal(expected, got, "TTT", "test_reference_only_matches");
        assert(got.size() == 2);
    }

    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// Test 4: Match spanning reference + change boundary
// ---------------------------------------------------------------------------
void test_reference_change_boundary() {
    std::cout << "Test 4: match spanning reference-change boundary... ";

    // EDS: AAATTT{G,C}AAATTT  l=3
    // T0 = AAATTTAAATTT
    // Changes: 0=G, 1=C
    // "TTG" = T(T0[4])+T(T0[5])+G(change0) -> (4,[0])
    // "TTC" = T(T0[4])+T(T0[5])+C(change1) -> (4,[1])
    const std::string eds_str = "AAATTT{G,C}AAATTT";
    BioFMI idx = build_index(eds_str, 3);
    EDS eds = make_eds(eds_str);

    {
        auto expected = brute_force_locate(eds, "TTG");
        auto got = collect_result(idx.locate("TTG"));
        assert_equal(expected, got, "TTG", "test_reference_change_boundary");
        // Expect exactly one occurrence at (4,[0])
        assert(got.size() == 1);
        assert(got.begin()->position == 4);
        assert(got.begin()->changes == std::vector<int>{0});
    }

    {
        auto expected = brute_force_locate(eds, "TTC");
        auto got = collect_result(idx.locate("TTC"));
        assert_equal(expected, got, "TTC", "test_reference_change_boundary");
        assert(got.size() == 1);
        assert(got.begin()->position == 4);
        assert(got.begin()->changes == std::vector<int>{1});
    }

    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// Test 5: Match starting inside a degenerate alternative
// ---------------------------------------------------------------------------
void test_match_starts_in_change() {
    std::cout << "Test 5: match starting inside a degenerate alternative... ";

    // EDS: AAAA{T,GGG}TTTT  l=4
    // T0 = AAAATTTT
    // Changes: 0=T, 1=GGG
    // base_positions: after AAAA=4, after TTTT=8
    //
    // Pattern "GGGT" (l=4):
    //   G(change1 offset 0) + G(change1 offset 1) + G(change1 offset 2) + T(T0[4])
    //   Starts in change 1 at offset 0 -> position = base_pos(set0) + 0 = 4 + 0 = 4
    //   changes = [1]
    const std::string eds_str = "AAAA{T,GGG}TTTT";
    BioFMI idx = build_index(eds_str, 4);
    EDS eds = make_eds(eds_str);

    {
        auto expected = brute_force_locate(eds, "GGGT");
        auto got = collect_result(idx.locate("GGGT"));
        assert_equal(expected, got, "GGGT", "test_match_starts_in_change");
        assert(got.size() == 1);
        assert(got.begin()->position == 4);
        assert(got.begin()->changes == std::vector<int>{1});
    }

    // Pattern "GGG" would be length 3 (not multiple of 4) -> tested elsewhere
    // Pattern "GGGG" (entirely within change 1 with one ref char?):
    //   GGG has only 3 chars; GGGG would need a 4th G not present.
    {
        auto expected = brute_force_locate(eds, "GGGG");
        auto got = collect_result(idx.locate("GGGG"));
        assert_equal(expected, got, "GGGG", "test_match_starts_in_change");
        assert(got.empty() && "GGGG should not be found (GGG is only 3 chars)");
    }

    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// Test 6: Match spanning two degenerate sets (two changes in result)
// ---------------------------------------------------------------------------
void test_match_spanning_two_changes() {
    std::cout << "Test 6: match spanning two degenerate sets... ";

    // EDS: AAAA{T,G}TTTT{C,A}CCCC  l=4
    // T0 = AAAATTTTCCCC
    // Changes: 0=T, 1=G, 2=C, 3=A
    // base_positions: after AAAA=4, after TTTT=8, after CCCC=12
    //
    // 8-char pattern "AAATTTCA" (2 chunks of 4):
    //   A(T0[1])+A(T0[2])+A(T0[3])+T(change0)+T(T0[4])+T(T0[5])+T(T0[6])+...
    //   Actually let me use a pattern we know exists:
    //   "AAATTTCC" length 8 doesn't span two changes.
    //
    // Let's use a pattern that goes ref->change->ref->change:
    //   "TTTTCCCC" length 8: pure reference T(4..7)+C(8..11) -> (4,[])
    //
    // For spanning changes, we need something like:
    //   T(T0[3]) + T(change0) + T(T0[4]) + T(T0[5]) + T(T0[6]) + T(T0[7]) ...
    //   or
    //   A(T0[2]) + A(T0[3]) + T(change0) + T(T0[4]) + T(T0[5]) + T(T0[6]) + C(change2) + C(T0[8])
    //   = "AATTTTTCC"? That's 9 chars. Not multiple of 4.
    //
    // Let's use l=3 instead for this test to get smaller chunks:
    // EDS: AAA{T,G}TTT{C,A}CCC  l=3
    // T0 = AAATTTCCC
    // Changes: 0=T, 1=G, 2=C, 3=A
    // base_positions: after AAA=3, after TTT=6, after CCC=9
    //
    // Pattern "TTCA" (not multiple of 3)... need 6-char patterns.
    // "TTTCCC" = T(T0[3])+T(T0[4])+T(T0[5])+C(T0[6])+C(T0[7])+C(T0[8]) pure ref -> (3,[])
    // "TTTACC" = T(T0[3..5]) + A(change3) + C(T0[6]) + C(T0[7]) -> (3,[3])
    // "TTGCCC" = no: T0[3..5]=TTT, so "TTG" would need G (change1) in there
    //   T(T0[3])+T(T0[4])+G(change1)+C(T0[6])+C(T0[7])+C(T0[8])
    //   but that's TTG+CCC = "TTGCCC" - does this span two changes? No, only change1.
    //   Wait: T(T0[3])+T(T0[4]) are references, G(change1) is a change, C+C+C are references.
    //   Only change1 used. -> (3,[1])
    //   Starts at T0[3]=T. Position=3. changes=[1]. -> (3,[1])
    //
    // For two changes: "ATGCCC" = A(T0[2])+T(change0)+G... no, only one change at each set.
    //   Actually with l=3: {T,G} is one set (alternatives T and G). Can't use both T and G
    //   in the same path. So for two changes, we need two different degenerate SETS.
    //
    // "ATTTCA" (6 chars): A(T0[2])+T(change0)+T(T0[3])+T(T0[4])+C(change2)+A?
    //   Hmm: T0=AAATTTCCC. After {T,G} comes TTT. After TTT comes {C,A}. After {C,A} comes CCC.
    //   So: A(T0[2]) + T(change0) + T(T0[3]) + T(T0[4]) + C(change2) + C(T0[6]) = "ATTTCC"?
    //     Wait: A(T0[2])=A, T(change0)=T, T(T0[3])=T, T(T0[4])=T, C(change2)=C, C(T0[6])=C
    //     = "ATTTCC" (6 chars) ✓  but T(T0[3]) is T0's 4th char (AAATTTCCC: T0[3]=T) and
    //     T(T0[4])=T, T0[5]=T, T0[6]=C, T0[7]=C, T0[8]=C.
    //     So the pattern traverses: ref(A) + change0(T) + ref(T,T) + change2(C) + ref(C)
    //     = "ATTTCC". Uses changes [0,2].
    //     Position: starts at T0[2]=A. -> (2,[0,2])

    const std::string eds_str = "AAA{T,G}TTT{C,A}CCC";
    BioFMI idx = build_index(eds_str, 3);
    EDS eds = make_eds(eds_str);

    // "ATTTCC" spans two degenerate sets
    {
        auto expected = brute_force_locate(eds, "ATTTCC");
        auto got = collect_result(idx.locate("ATTTCC"));
        assert_equal(expected, got, "ATTTCC", "test_match_spanning_two_changes");

        // Must include (2,[0,2])
        OccInfo expected_occ{2, {0, 2}};
        assert(got.count(expected_occ) && "Expected occurrence (2,[0,2]) not found");
    }

    // "AGTTCC" uses changes [1,2]
    {
        auto expected = brute_force_locate(eds, "AGTTCC");
        auto got = collect_result(idx.locate("AGTTCC"));
        assert_equal(expected, got, "AGTTCC", "test_match_spanning_two_changes");

        OccInfo expected_occ{2, {1, 2}};
        assert(got.count(expected_occ) && "Expected occurrence (2,[1,2]) not found");
    }

    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// Test 7: Same position, different change paths -> separate entries
// ---------------------------------------------------------------------------
void test_same_position_different_paths() {
    std::cout << "Test 7: same position, different change paths -> separate entries... ";

    // EDS: AAATTT{G,C}AAATTT  l=3
    // "TTG" -> (4,[0])  and  "TTC" -> (4,[1])  are different patterns.
    // For same position with different changes, we need both alternatives to produce
    // the same pattern string at the same position.
    // EDS: AAA{A,A}TTT l=3 would be degenerate with identical alternatives (degenerate
    // but same string) — not useful.
    //
    // Use: AAA{T,T}TTT — both alternatives are T, so same string, but change 0 and change 1
    // are distinct indices. Pattern "ATTT" (l=3... hmm 4 chars not multiple of 3).
    //
    // Better: AAATTT{GG,GG}AAATTT l=3: both alternatives identical.
    // Pattern "TGG" (l=3): T(T0[5]) + G(change0 offset 0) + G(change0 offset 1)
    //                       T(T0[5]) + G(change1 offset 0) + G(change1 offset 1)
    // Both start at position T0[5]=T, -> (5,[0]) and (5,[1]) -- two separate entries.
    const std::string eds_str = "AAATTT{GG,GG}AAATTT";
    BioFMI idx = build_index(eds_str, 3);
    EDS eds = make_eds(eds_str);

    {
        auto expected = brute_force_locate(eds, "TGG");
        auto got = collect_result(idx.locate("TGG"));
        assert_equal(expected, got, "TGG", "test_same_position_different_paths");

        // Should have two entries: (5,[0]) and (5,[1])
        assert(got.size() == 2 && "Expected 2 separate entries for same position, different changes");
        assert(got.count({5, {0}}) && "Missing (5,[0])");
        assert(got.count({5, {1}}) && "Missing (5,[1])");
    }

    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// Test 8: count() equals number of locate() entries
// ---------------------------------------------------------------------------
void test_count_matches_locate() {
    std::cout << "Test 8: count() == number of locate() entries... ";

    const std::string eds_str = "AAA{T,G}TTT{C,A}CCC";
    BioFMI idx = build_index(eds_str, 3);

    for (const std::string& pat : {"AAA", "TTT", "TTG", "TTC", "AAATTT", "ATTTCC"}) {
        try {
            auto result = idx.locate(pat);
            size_t total = 0;
            for (const auto& [seq_id, occs] : result) total += occs.size();

            size_t cnt = idx.count(pat);
            if (cnt != total) {
                std::cerr << "  FAIL: pattern='" << pat << "' locate gives "
                          << total << " but count gives " << cnt << "\n";
                assert(false && "count() does not match locate() entry count");
            }
        } catch (const std::runtime_error&) {
            // Invalid length — skip
        }
    }
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// Test 9: Broad brute-force vs index comparison (multiple EDS, many patterns)
// ---------------------------------------------------------------------------
void compare_brute_force_vs_index(const std::string& eds_str, int l,
                                   const std::vector<std::string>& patterns,
                                   const std::string& test_name) {
    BioFMI idx = build_index(eds_str, l);
    EDS eds = make_eds(eds_str);

    for (const auto& pat : patterns) {
        auto expected = brute_force_locate(eds, pat);
        auto got = collect_result(idx.locate(pat));
        assert_equal(expected, got, pat, test_name);
    }
}

void test_broad_correctness_simple() {
    std::cout << "Test 9a: broad correctness, simple EDS... ";

    // AAATTT{G,C}AAATTT  l=3
    compare_brute_force_vs_index(
        "AAATTT{G,C}AAATTT", 3,
        {"AAA", "TTT", "TTG", "TTC", "GAA", "CAA", "AAATTT", "TTTGAA", "TTTCAA"},
        "test_broad_correctness_simple");

    std::cout << "PASSED\n";
}

void test_broad_correctness_multiple_sets() {
    std::cout << "Test 9b: broad correctness, multiple degenerate sets... ";

    // AAA{T,G}TTT{C,A}CCC  l=3
    compare_brute_force_vs_index(
        "AAA{T,G}TTT{C,A}CCC", 3,
        {
            "AAA", "TTT", "CCC",
            "AAT", "AAG", "TTC", "TTA",
            "GAA", "CAA",
            "TTG", "TTG",
            "AATTTC", "AATTTA", "AAGTTC", "AAGTTA",
            "ATTTCC", "ATTTAC", "AGTTCC", "AGTTAC",
        },
        "test_broad_correctness_multiple_sets");

    std::cout << "PASSED\n";
}

void test_broad_correctness_long_alternatives() {
    std::cout << "Test 9c: broad correctness, longer alternatives... ";

    // AAAA{T,GGG}TTTT{CC,A}CCCC  l=4
    compare_brute_force_vs_index(
        "AAAA{T,GGG}TTTT{CC,A}CCCC", 4,
        {
            "AAAA", "TTTT", "CCCC",
            "AAAT", "AAAG", "AAAG",
            "GGGT", "GGGG",
            "TTCC", "TTAC",
            "TTTTCCCC", "TTTTACCC",
            "AAAATTTT",
        },
        "test_broad_correctness_long_alternatives");

    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// Test 10: Boundary degenerate symbols — no false positives, correct positions
//
// The l-EDS property exempts the leading and trailing non-degenerate segments
// from the length-≥l requirement.  When these segments are shorter than cl
// (= l-1), parse_eds() must pad with sentinels so every stored entry has
// exactly cl chars of context on each side.  Without padding, a chunk query
// can hit a truncated entry at the wrong offset and return a spurious match.
// ---------------------------------------------------------------------------
void test_boundary_degenerate_no_false_positives() {
    std::cout << "Test 10a: first symbol degenerate (no left context)... ";

    // EDS: {G,C}AAATTT  l=3
    // Leading segment is absent; change entries must be padded on the left.
    // "GAA" should be found (G + first two chars of reference).
    // "CAA" likewise.  "GGG" should not be found.
    {
        const std::string eds = "{G,C}AAATTT";
        compare_brute_force_vs_index(eds, 3,
            {"GAA", "CAA", "AAA", "TTT", "GGG", "CCC"},
            "test_boundary_first_degenerate");
    }

    std::cout << "PASSED\n";
}

void test_boundary_short_leading_segment() {
    std::cout << "Test 10b: leading segment shorter than cl... ";

    // EDS: A{G,C}AAATTT  l=3  (leading "A" has length 1 < cl=2)
    // Without padding the left context is "A" (1 char), not "##A" (2 chars).
    // "GAA" is in the EDS (G + first two chars of AAATTT).
    // "AGG" is not (only one G in this EDS).
    {
        const std::string eds = "A{G,C}AAATTT";
        compare_brute_force_vs_index(eds, 3,
            {"GAA", "CAA", "AAA", "TTT", "AGG", "ACC"},
            "test_boundary_short_leading");
    }

    std::cout << "PASSED\n";
}

void test_boundary_short_trailing_segment() {
    std::cout << "Test 10c: trailing segment shorter than cl... ";

    // EDS: AAATTT{G,C}A  l=3  (trailing "A" has length 1 < cl=2)
    // Without padding the right context is "A" (1 char), not "A#" (padded).
    // "TTG" and "TTC" are in the EDS.  "GAA" is not (only one A after G).
    {
        const std::string eds = "AAATTT{G,C}A";
        compare_brute_force_vs_index(eds, 3,
            {"TTG", "TTC", "AAA", "TTT", "GAA", "CAA"},
            "test_boundary_short_trailing");
    }

    std::cout << "PASSED\n";
}

void test_boundary_last_symbol_degenerate() {
    std::cout << "Test 10d: last symbol degenerate (no right context)... ";

    // EDS: AAATTT{G,C}  l=3  (trailing segment absent)
    // Without padding the right context is "", causing the entry to be short.
    // "TTG" and "TTC" cross the boundary; "GAA" does not exist.
    {
        const std::string eds = "AAATTT{G,C}";
        compare_brute_force_vs_index(eds, 3,
            {"TTG", "TTC", "AAA", "TTT", "GAA", "CAA"},
            "test_boundary_last_degenerate");
    }

    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "========================================\n";
    std::cout << "LOCATE CORRECTNESS TESTS (spec-driven)\n";
    std::cout << "========================================\n\n";

    try {
        test_error_pattern_too_short();
        test_error_pattern_not_multiple_of_l();
        test_no_match();
        test_reference_only_matches();
        test_reference_change_boundary();
        test_match_starts_in_change();
        test_match_spanning_two_changes();
        test_same_position_different_paths();
        test_count_matches_locate();
        test_broad_correctness_simple();
        test_broad_correctness_multiple_sets();
        test_broad_correctness_long_alternatives();
        test_boundary_degenerate_no_false_positives();
        test_boundary_short_leading_segment();
        test_boundary_short_trailing_segment();
        test_boundary_last_symbol_degenerate();

        std::cout << "\n========================================\n";
        std::cout << "ALL CORRECTNESS TESTS PASSED\n";
        std::cout << "========================================\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\nFAILED: " << e.what() << "\n";
        return 1;
    }
}