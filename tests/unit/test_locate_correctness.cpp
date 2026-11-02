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
        for (const auto& occ : occs) {
            out.insert({(int)occ.position, occ.changes});
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
// Test 1: Pattern length -> only the empty pattern throws
// ---------------------------------------------------------------------------
void test_error_pattern_too_short() {
    std::cout << "Test 1a: only the empty pattern is refused... ";

    BioFMI idx = build_index("AAATTT{G,C}AAATTT", 3);

    // This used to assert that |P| < l+1 threw. It no longer does: the guarantee
    // a full chunk gave — a changes hit touches the alternative rather than
    // hiding in a context flank — is checked directly by
    // process_changes_matches() on every chunk, the first included, so a pattern
    // that is nothing but a short chunk is the same case as a short tail.
    // test_locate_arbitrary.cpp checks every such length against brute force.
    idx.locate("AA");   // length 2 < l+1 = 4: searched, must not throw

    bool threw = false;
    try {
        idx.locate("");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw && "Expected exception for the empty pattern");
    std::cout << "PASSED\n";
}

void test_error_pattern_not_multiple_of_l() {
    std::cout << "Test 1b: pattern length not a multiple of (l+1) is now allowed... ";

    // This used to assert that such a pattern threw. Arbitrary lengths are
    // supported now: the r = |P| mod (l+1) tail is searched as a short final
    // chunk. Correctness across every length is covered by
    // test_locate_arbitrary.cpp against a brute-force oracle; here we only
    // check that it no longer throws and agrees with brute force.
    BioFMI idx = build_index("AAATTT{G,C}AAATTT", 3);
    EDS eds = make_eds("AAATTT{G,C}AAATTT");

    for (const char* pat : {"AAATT", "AAATTTG", "TTTGAAATT"}) {
        auto got = collect_result(idx.locate(pat));
        auto want = brute_force_locate(eds, pat);
        assert(got == want && "arbitrary-length locate disagrees with brute force");
    }
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// Test 2: Pattern not in EDS -> empty result
// ---------------------------------------------------------------------------
void test_no_match() {
    std::cout << "Test 2: pattern not in EDS -> empty result... ";

    // EDS only contains A,T,G,C — search for a combination that never occurs.
    BioFMI idx = build_index("AAATTT{G,C}AAATTT", 3);

    auto result = idx.locate("GGGG");  // GGGG: only one G ever appears per path
    assert(result.empty() && "GGGG should not be found");
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

    // chunk_size=4: use 4-char patterns
    // "AAAT" appears at T0[0..3] and T0[6..9] (T0="AAATTTAAATTT")
    {
        auto expected = brute_force_locate(eds, "AAAT");
        auto got = collect_result(idx.locate("AAAT"));
        assert_equal(expected, got, "AAAT", "test_reference_only_matches");

        for (const auto& o : got) {
            assert(o.changes.empty() && "Pure reference match should have no changes");
        }
        assert(got.size() == 2);
    }

    // "ATTT" appears at T0[2..5] and T0[8..11]
    {
        auto expected = brute_force_locate(eds, "ATTT");
        auto got = collect_result(idx.locate("ATTT"));
        assert_equal(expected, got, "ATTT", "test_reference_only_matches");
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
    // chunk_size=4: "TTTG" = T(T0[3])+T(T0[4])+T(T0[5])+G(change0) -> (3,[0])
    // "TTTC" = T(T0[3])+T(T0[4])+T(T0[5])+C(change1) -> (3,[1])
    const std::string eds_str = "AAATTT{G,C}AAATTT";
    BioFMI idx = build_index(eds_str, 3);
    EDS eds = make_eds(eds_str);

    {
        auto expected = brute_force_locate(eds, "TTTG");
        auto got = collect_result(idx.locate("TTTG"));
        assert_equal(expected, got, "TTTG", "test_reference_change_boundary");
        assert(got.size() == 1);
        assert(got.begin()->position == 3);
        assert(got.begin()->changes == std::vector<int>{0});
    }

    {
        auto expected = brute_force_locate(eds, "TTTC");
        auto got = collect_result(idx.locate("TTTC"));
        assert_equal(expected, got, "TTTC", "test_reference_change_boundary");
        assert(got.size() == 1);
        assert(got.begin()->position == 3);
        assert(got.begin()->changes == std::vector<int>{1});
    }

    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// Test 5: Match starting inside a degenerate alternative
// ---------------------------------------------------------------------------
void test_match_starts_in_change() {
    std::cout << "Test 5: match starting inside a degenerate alternative... ";

    // EDS: AAATTT{GG,C}AAATTT  l=3  (use "GG" so a chunk can start at offset 1)
    // T0 = AAATTTAAATTT
    // Changes: 0=GG (len 2), 1=C (len 1)
    // chunk_size=4
    //
    // "GAAA" (4 chars): G(change0 offset 1) + A(T0[6]) + A(T0[7]) + A(T0[8])
    //   Starts inside change 0 at offset 1 -> position = base_pos(set0) + 1 = 6 + 1 = 7
    //   changes = [0]
    // "CAAA" (4 chars): C(change1 offset 0) -> position = 6 + 0 = 6, changes = [1]
    const std::string eds_str = "AAATTT{GG,C}AAATTT";
    BioFMI idx = build_index(eds_str, 3);
    EDS eds = make_eds(eds_str);

    {
        auto expected = brute_force_locate(eds, "GAAA");
        auto got = collect_result(idx.locate("GAAA"));
        assert_equal(expected, got, "GAAA", "test_match_starts_in_change");
        assert(got.size() == 1);
        assert(got.begin()->position == 7);
        assert(got.begin()->changes == std::vector<int>{0});
    }

    {
        auto expected = brute_force_locate(eds, "CAAA");
        auto got = collect_result(idx.locate("CAAA"));
        assert_equal(expected, got, "CAAA", "test_match_starts_in_change");
        assert(got.size() == 1);
        assert(got.begin()->position == 6);
        assert(got.begin()->changes == std::vector<int>{1});
    }

    {
        auto expected = brute_force_locate(eds, "GGGG");
        auto got = collect_result(idx.locate("GGGG"));
        assert_equal(expected, got, "GGGG", "test_match_starts_in_change");
        assert(got.empty() && "GGGG should not be found (only 2 G's exist)");
    }

    // A length that is not a multiple of l+1 is searched, not rejected.
    {
        auto expected = brute_force_locate(eds, "GGGGG");
        auto got = collect_result(idx.locate("GGGGG"));
        assert_equal(expected, got, "GGGGG", "test_match_starts_in_change");
        assert(got.empty());
    }

    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// Test 6: Match spanning two degenerate sets (two changes in result)
// ---------------------------------------------------------------------------
void test_match_spanning_two_changes() {
    std::cout << "Test 6: match spanning two degenerate sets... ";

    // EDS: AAA{T,G}TTT{C,A}CCC  l=3, chunk size 4
    // T0 = AAATTTCCC; changes 0=T, 1=G (set 0), 2=C, 3=A (set 1)
    // A match crossing both sets passes ref -> change -> ref -> change -> ref,
    // and records one alternative from each set.
    const std::string eds_str = "AAA{T,G}TTT{C,A}CCC";
    BioFMI idx = build_index(eds_str, 3);
    EDS eds = make_eds(eds_str);

    // Two chunks each.
    // "AATTTTCC": A(T0[1])+A(T0[2])+T(change0)+T(T0[3])+T(T0[4])+T(T0[5])+C(change2)+C(T0[6])
    //   -> (1,[0,2])
    {
        auto expected = brute_force_locate(eds, "AATTTTCC");
        auto got = collect_result(idx.locate("AATTTTCC"));
        assert_equal(expected, got, "AATTTTCC", "test_match_spanning_two_changes");

        OccInfo expected_occ{1, {0, 2}};
        assert(got.count(expected_occ) && "Expected occurrence (1,[0,2]) not found");
    }

    // "AAGTTTCC": A(T0[1])+A(T0[2])+G(change1)+T(T0[3])+T(T0[4])+T(T0[5])+C(change2)+C(T0[6])
    //   -> (1,[1,2])
    {
        auto expected = brute_force_locate(eds, "AAGTTTCC");
        auto got = collect_result(idx.locate("AAGTTTCC"));
        assert_equal(expected, got, "AAGTTTCC", "test_match_spanning_two_changes");

        OccInfo expected_occ{1, {1, 2}};
        assert(got.count(expected_occ) && "Expected occurrence (1,[1,2]) not found");
    }

    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// Test 7: Same position, different change paths -> separate entries
// ---------------------------------------------------------------------------
void test_same_position_different_paths() {
    std::cout << "Test 7: same position, different change paths -> separate entries... ";

    // Two alternatives spelling the same string are still distinct changes, so
    // one position yields one entry per alternative.
    // EDS: AAATTT{GG,GG}AAATTT  l=3, chunk size 4
    // "TTGG" = T0[4..5] + GG -> (4,[0]) via change 0 and (4,[1]) via change 1.
    const std::string eds_str = "AAATTT{GG,GG}AAATTT";
    BioFMI idx = build_index(eds_str, 3);
    EDS eds = make_eds(eds_str);

    {
        auto expected = brute_force_locate(eds, "TTGG");
        auto got = collect_result(idx.locate("TTGG"));
        assert_equal(expected, got, "TTGG", "test_same_position_different_paths");

        assert(got.size() == 2 && "Expected 2 separate entries for same position, different changes");
        assert(got.count({4, {0}}) && "Missing (4,[0])");
        assert(got.count({4, {1}}) && "Missing (4,[1])");
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

    // chunk_size=4 for l=3: one and two whole chunks
    for (const char* pat : {"AAAT", "ATTT", "TTTG", "TTTC", "AATTTTCC", "AATTTTAC"}) {
        auto result = idx.locate(pat);
        size_t total = 0;
        for (const auto& [seq_id, occs] : result) total += occs.size();

        size_t cnt = idx.count(pat);
        if (cnt != total) {
            std::cerr << "  FAIL: pattern='" << pat << "' locate gives "
                      << total << " but count gives " << cnt << "\n";
            assert(false && "count() does not match locate() entry count");
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
    // chunk_size=4 for l=3
    compare_brute_force_vs_index(
        "AAATTT{G,C}AAATTT", 3,
        {"AAAT", "ATTT", "TTTG", "TTTC", "GAAA", "CAAA",
         "AAATTTGA", "AAATTTCA", "TTTGAAAT", "TTTCAAAT"},
        "test_broad_correctness_simple");

    std::cout << "PASSED\n";
}

void test_broad_correctness_multiple_sets() {
    std::cout << "Test 9b: broad correctness, multiple degenerate sets... ";

    // AAA{T,G}TTT{C,A}CCC  l=3
    // chunk_size=4 for l=3
    compare_brute_force_vs_index(
        "AAA{T,G}TTT{C,A}CCC", 3,
        {
            "AAAT", "ATTT", "TCCC",
            "AATT", "AAGT", "TTTC", "TTTA",
            "TTTT", "CCCC",
            "AATTTTCC", "AATTTTAC", "AAGTTTCC", "AAGTTTAC",
        },
        "test_broad_correctness_multiple_sets");

    std::cout << "PASSED\n";
}

void test_broad_correctness_long_alternatives() {
    std::cout << "Test 9c: broad correctness, longer alternatives... ";

    // AAAA{T,GGG}TTTT{CC,A}CCCC  l=4
    // chunk_size=5 for l=4; T0="AAAATTTTCCCC"
    compare_brute_force_vs_index(
        "AAAA{T,GGG}TTTT{CC,A}CCCC", 4,
        {
            // pure reference (length 5)
            "AAAAT", "ATTTT", "TTTCC",
            // boundary / change (length 5)
            "AAAAG", "GGGTT", "TTTTT",
            // not found (length 5)
            "AAAAA", "GGGGG",
            // 2-chunk patterns (length 10)
            "AAAATTTTCC", "GGGTTTTCCC",
        },
        "test_broad_correctness_long_alternatives");

    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// Test 10: Boundary degenerate symbols — no false positives, correct positions
//
// The l-EDS property exempts the leading and trailing non-degenerate segments
// from the length-≥l requirement.  When these segments are shorter than l,
// parse_eds() must pad with sentinels so every stored entry has exactly l
// chars of context on each side.  Without padding, a chunk query
// can hit a truncated entry at the wrong offset and return a spurious match.
// ---------------------------------------------------------------------------
void test_boundary_degenerate_no_false_positives() {
    std::cout << "Test 10a: first symbol degenerate (no left context)... ";

    // EDS: {G,C}AAATTT  l=3, chunk_size=4
    // Leading segment is absent; change entries must be padded on the left.
    {
        const std::string eds = "{G,C}AAATTT";
        compare_brute_force_vs_index(eds, 3,
            {"GAAA", "CAAA", "AAAT", "ATTT", "GGGG", "CCCC"},
            "test_boundary_first_degenerate");
    }

    std::cout << "PASSED\n";
}

void test_boundary_short_leading_segment() {
    std::cout << "Test 10b: leading segment shorter than l... ";

    // EDS: A{G,C}AAATTT  l=3  (leading "A" has length 1 < l=3)
    // chunk_size=4
    {
        const std::string eds = "A{G,C}AAATTT";
        compare_brute_force_vs_index(eds, 3,
            {"GAAA", "CAAA", "AAAT", "ATTT", "AGGG", "ACCC"},
            "test_boundary_short_leading");
    }

    std::cout << "PASSED\n";
}

void test_boundary_short_trailing_segment() {
    std::cout << "Test 10c: trailing segment shorter than l... ";

    // EDS: AAATTT{G,C}A  l=3  (trailing "A" has length 1 < l=3)
    // chunk_size=4
    {
        const std::string eds = "AAATTT{G,C}A";
        compare_brute_force_vs_index(eds, 3,
            {"TTTG", "TTTC", "AAAT", "ATTT", "GAAA", "CAAA"},
            "test_boundary_short_trailing");
    }

    std::cout << "PASSED\n";
}

void test_boundary_last_symbol_degenerate() {
    std::cout << "Test 10d: last symbol degenerate (no right context)... ";

    // EDS: AAATTT{G,C}  l=3  (trailing segment absent; right context fully padded)
    // chunk_size=4
    {
        const std::string eds = "AAATTT{G,C}";
        compare_brute_force_vs_index(eds, 3,
            {"TTTG", "TTTC", "AAAT", "ATTT", "GAAA", "CAAA"},
            "test_boundary_last_degenerate");
    }

    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// Test 1c: reserved characters in the pattern are refused
//
// CHANGE_SEPARATOR delimits blocks and pads short contexts, so a pattern
// holding it used to match those: "AAG#" was found at the end of T0, and on an
// EDS opening with a set "##AG" at position 2^64 - 2 (a negative offset
// wrapped into Position). Whitespace (a CRLF pattern file's '\r') and NUL
// matched nothing and said so, which is as wrong. All are errors now.
// ---------------------------------------------------------------------------
void test_error_reserved_characters() {
    std::cout << "Test 1c: reserved characters in the pattern are refused... ";

    BioFMI idx = build_index("{A,C}GGGTTT{T,G}CCCA", 3);
    for (const std::string& pat : {std::string("##AG"), std::string("TTT#"),
                                  std::string("GG#T"), std::string("GGGT\r"),
                                  std::string("GG T"), std::string("GGG\tT"),
                                  std::string("GG\0T", 4)}) {
        bool threw = false;
        try { idx.locate(pat); } catch (const std::runtime_error&) { threw = true; }
        assert(threw && "locate() must refuse a reserved character");
        threw = false;
        try { idx.count(pat); } catch (const std::runtime_error&) { threw = true; }
        assert(threw && "count() must refuse a reserved character");
    }
    // The same patterns without the reserved character are ordinary queries.
    assert(idx.count("GGGT") == 1);
    assert(idx.count("AGGG") == 1);

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
        test_error_reserved_characters();
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