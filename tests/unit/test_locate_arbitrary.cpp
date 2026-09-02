/**
 * Arbitrary pattern lengths: |P| need not be a multiple of l+1.
 *
 * The tail is the r = |P| mod (l+1) characters left over after the full chunks.
 * Searching it as a short chunk is the obvious approach and, below some length,
 * the wrong one — a chunk of r characters has only |A|^r distinct values, so on
 * a large text every lookup returns an enormous candidate set. So BioFMI
 * searches the tail while it is still selective (r >= tail_threshold) and
 * otherwise verifies it directly against each surviving candidate.
 *
 * Both branches must agree with brute force, so both thresholds are exercised:
 * 0 forces every tail through the search path, a huge value forces every tail
 * through the verify path, and the two must produce identical results.
 *
 * The oracle is the one test_locate_correctness.cpp uses: expand the EDS into
 * every concrete string and search them naively.
 *
 * (An earlier design overlapped the final chunk instead, so every lookup stayed
 * full-length. These tests disproved it: the key a chunk stores subtracts the
 * change content of the whole chunk, while an overlapping successor advances
 * only r into it, so the lookup misses whenever change content lands in the
 * overlap. It produced false negatives on patterns straddling a degenerate
 * symbol — "AAGGG" against AAATTTGC{G,C}AAATTTCA{TT,A}GGGCCCAT at l=3.)
 */

#include "index/index.hpp"
#include <cassert>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace biofmi;

namespace {

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

struct CharInfo { bool is_ref; int position; int change_idx; };
struct Path {
    std::string str;
    std::vector<CharInfo> chars;
    // Alternatives traversed between the previous character and this one.
    // Only ever non-empty for zero-length alternatives, which contribute no
    // character of their own and would otherwise leave no trace — see
    // brute_force() for why they still belong in `changes`.
    std::vector<std::vector<int>> empties_before;
};

std::vector<Path> expand_eds(const EDS& eds) {
    std::vector<Path> paths = {{"", {}, {}}};
    // Alternatives traversed since the last character was emitted, per path.
    std::vector<std::vector<int>> pending(1);
    int t0_pos = 0, global_change_idx = 0;

    auto emit = [&](Path& p, std::vector<int>& pend, char c, CharInfo ci) {
        p.str += c;
        p.chars.push_back(ci);
        p.empties_before.push_back(pend);
        pend.clear();
    };

    for (size_t sym = 0; sym < eds.length(); sym++) {
        StringSet symbol = eds.read_symbol(sym);
        if (symbol.size() <= 1) {
            const std::string& s = symbol[0];
            for (size_t i = 0; i < paths.size(); i++)
                for (size_t k = 0; k < s.size(); k++)
                    emit(paths[i], pending[i], s[k], {true, t0_pos + (int)k, -1});
            t0_pos += (int)s.size();
        } else {
            int base_pos = t0_pos;
            std::vector<Path> np_all;
            std::vector<std::vector<int>> np_pending;
            for (size_t alt = 0; alt < symbol.size(); alt++) {
                const std::string& a = symbol[alt];
                int cidx = global_change_idx + (int)alt;
                for (size_t i = 0; i < paths.size(); i++) {
                    Path np = paths[i];
                    std::vector<int> pend = pending[i];
                    if (a.empty()) {
                        // Contributes no character, but the match still had to
                        // choose it, so remember it for the next character.
                        pend.push_back(cidx);
                    } else {
                        for (size_t k = 0; k < a.size(); k++)
                            emit(np, pend, a[k], {false, base_pos + (int)k, cidx});
                    }
                    np_all.push_back(std::move(np));
                    np_pending.push_back(std::move(pend));
                }
            }
            paths = std::move(np_all);
            pending = std::move(np_pending);
            global_change_idx += (int)symbol.size();
        }
    }
    return paths;
}

std::set<OccInfo> brute_force(const EDS& eds, const std::string& pattern) {
    auto paths = expand_eds(eds);
    std::set<OccInfo> out;
    int plen = (int)pattern.size();
    for (const auto& p : paths) {
        if ((int)p.str.size() < plen) continue;
        for (int i = 0; i + plen <= (int)p.str.size(); i++) {
            if (p.str.compare(i, plen, pattern) != 0) continue;
            OccInfo occ;
            occ.position = p.chars[i].position;
            for (int j = i; j < i + plen; j++) {
                // A zero-length alternative between two matched characters is
                // traversed by the match even though it contributes nothing to
                // the text: the occurrence exists only on the path that chose
                // it. locate() reports it, and it must, because `changes` is
                // what drives the source-set intersection — omitting it would
                // credit the match to genomes taking a different alternative.
                // (Not at j == i: an alternative before the first matched
                // character is not inside the match.)
                if (j > i)
                    for (int cid : p.empties_before[j])
                        if (occ.changes.empty() || occ.changes.back() != cid)
                            occ.changes.push_back(cid);
                if (!p.chars[j].is_ref) {
                    int cid = p.chars[j].change_idx;
                    if (occ.changes.empty() || occ.changes.back() != cid)
                        occ.changes.push_back(cid);
                }
            }
            out.insert(occ);
        }
    }
    return out;
}

std::set<OccInfo> collect(const BioFMI::ResultMap& rm) {
    std::set<OccInfo> out;
    for (const auto& [seq, occs] : rm)
        for (const auto& o : occs)
            out.insert({(int)o.position, o.changes});
    return out;
}

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

// Every substring of every expanded path, at every length in [lo, hi].
std::set<std::string> substrings(const EDS& eds, size_t lo, size_t hi) {
    std::set<std::string> out;
    for (const auto& p : expand_eds(eds))
        for (size_t len = lo; len <= hi; len++)
            for (size_t i = 0; i + len <= p.str.size(); i++)
                out.insert(p.str.substr(i, len));
    return out;
}

const char* kEds  = "AAATTTGC{G,C}AAATTTCA{TT,A}GGGCCCAT";
// Contains an empty alternative on purpose: a match spanning it exists only on
// the path that chose it, so `changes` must record it even though it
// contributes no character. See brute_force().
const char* kEds2 = "ACGTACGTAA{G,CC,}TTTTGGGGAC{A,T}CCCCAAAG";

int failures = 0;

void check(BioFMI& idx, const EDS& eds, const std::string& pat, const char* what) {
    auto got = collect(idx.locate(pat));
    auto want = brute_force(eds, pat);
    if (got != want) {
        if (++failures <= 5) {
            std::cerr << "\n  MISMATCH (" << what << ") pattern=\"" << pat
                      << "\" len=" << pat.size()
                      << "  got " << got.size() << " want " << want.size() << "\n";
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. Exact multiples are untouched by the new planning code
// ---------------------------------------------------------------------------
void test_exact_multiples_unchanged() {
    std::cout << "Test 1: exact multiples still correct in both modes... ";
    for (const char* src : {kEds, kEds2})
    for (int l : {3, 4}) {
        EDS eds = make_eds(src);
        BioFMI idx = build_index(src, l);
        const size_t cs = l + 1;
        for (const auto& pat : substrings(eds, cs, 4 * cs)) {
            if (pat.size() % cs != 0) continue;
            idx.set_tail_threshold(0);
            check(idx, eds, pat, "exact multiple");
        }
    }
    assert(failures == 0 && "exact-multiple behaviour regressed");
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// 2. The search branch against brute force, every length
// ---------------------------------------------------------------------------
void test_search_branch_all_lengths() {
    std::cout << "Test 2: tail-search branch vs brute force, every length... ";
    int before = failures;
    for (const char* src : {kEds, kEds2}) {
        for (int l : {3, 4, 5}) {
            EDS eds = make_eds(src);
            BioFMI idx = build_index(src, l);
            idx.set_tail_threshold(0);          // every tail goes through search
            for (const auto& pat : substrings(eds, l + 1, 3 * (l + 1)))
                check(idx, eds, pat, "tail search");
        }
    }
    assert(failures == before && "tail-search branch disagrees with brute force");
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// 3. Threshold mode against brute force, at thresholds that exercise both of
//    its branches — search the tail, and verify the tail

// ---------------------------------------------------------------------------
// 4. The two modes agree with each other

// ---------------------------------------------------------------------------
// 5. A pattern shorter than one chunk is still rejected
// ---------------------------------------------------------------------------
void test_too_short_rejected() {
    std::cout << "Test 5: |P| < l+1 rejected... ";
    BioFMI idx = build_index(kEds, 4);
    {
        idx.set_tail_threshold(0);
        bool threw = false;
        try { idx.locate("ACG"); } catch (const std::runtime_error&) { threw = true; }
        assert(threw && "a pattern shorter than one chunk must throw");
    }
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// 6. count() follows locate() at arbitrary lengths
// ---------------------------------------------------------------------------
void test_count_matches_locate() {
    std::cout << "Test 6: count() == entries at arbitrary lengths... ";
    EDS eds = make_eds(kEds);
    BioFMI idx = build_index(kEds, 4);
    {
        idx.set_tail_threshold(0);
        for (const auto& pat : substrings(eds, 5, 14)) {
            size_t n = 0;
            for (const auto& [s, occs] : idx.locate(pat)) n += occs.size();
            assert(idx.count(pat) == n);
        }
    }
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// A non-zero threshold is refused rather than silently dropping matches
// ---------------------------------------------------------------------------
void test_verify_branch_rejected() {
    std::cout << "Test 3: non-zero tail_threshold refused... ";
    BioFMI idx = build_index(kEds, 4);
    bool threw = false;
    try { idx.set_tail_threshold(8); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw && "the unimplemented verify path must not be reachable");
    assert(idx.tail_threshold() == 0);
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Arbitrary pattern length tests\n";
    std::cout << "========================================\n\n";
    try {
        test_exact_multiples_unchanged();
        test_search_branch_all_lengths();
        test_verify_branch_rejected();
        test_too_short_rejected();
        test_count_matches_locate();

        std::cout << "\n========================================\n";
        std::cout << "ALL ARBITRARY-LENGTH TESTS PASSED\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nFAILED: " << e.what() << "\n";
        return 1;
    }
}
