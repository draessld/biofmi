/**
 * Randomised differential testing for locate().
 *
 * Every locate() bug found so far hid the same way: the hand-written test EDS
 * happened not to contain the structure that triggers it. The empty-alternative
 * recall bug survived because no test string had an empty alternative; the
 * short-chunk context bug survived because no test searched a chunk shorter than
 * l+1. Both were found within seconds of a test finally covering the case.
 *
 * So this stops hand-writing the input. It generates panels with a seeded RNG,
 * deliberately biased towards the structures that have broken things before —
 * empty alternatives, degenerate symbols at the very start and end, alternatives
 * both shorter and longer than l, minimum-width internal segments — and checks
 * every substring of every path against a brute-force oracle, in both CARTESIAN
 * and LINEAR modes.
 *
 * Failures print the generating seed, so any counterexample is reproducible by
 * running that seed alone.
 */

#include "index/index.hpp"
#include "fuzz_panel.hpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unistd.h>
#include <vector>

using namespace biofmi;
using namespace fuzz;

namespace {

// -------------------------------------------------------------------- oracle

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

void collect_from(const Spelled& s, const std::string& pat, std::set<OccInfo>& out) {
    int plen = (int)pat.size();
    for (int i = 0; i + plen <= (int)s.str.size(); i++) {
        if (s.str.compare(i, plen, pat) != 0) continue;
        OccInfo o;
        o.position = s.pos[i];
        for (int j = i; j < i + plen; j++) {
            if (j > i)
                for (int e : s.empties[j])
                    if (o.changes.empty() || o.changes.back() != e) o.changes.push_back(e);
            if (s.alt[j] >= 0 && (o.changes.empty() || o.changes.back() != s.alt[j]))
                o.changes.push_back(s.alt[j]);
        }
        out.insert(o);
    }
}

std::set<OccInfo> oracle_cartesian(const Panel& p, const std::string& pat) {
    std::set<OccInfo> out;
    for (const auto& c : all_choices(p)) collect_from(spell(p, c), pat, out);
    return out;
}

// Only the strings the panel's paths actually spell — the LINEAR language.
std::set<OccInfo> oracle_linear(const Panel& p, const std::string& pat) {
    std::set<OccInfo> out;
    for (const auto& choice : p.path_choice) collect_from(spell(p, choice), pat, out);
    return out;
}

std::set<OccInfo> collect(const BioFMI::ResultMap& rm) {
    std::set<OccInfo> out;
    for (const auto& [seq, occs] : rm) out.insert({(int)occs.empty() ? 0 : 0, {}});
    out.clear();
    for (const auto& [seq, occs] : rm)
        for (const auto& o : occs) out.insert({(int)o.position, o.changes});
    return out;
}

// Each entry's genomes, keyed by the entry, so two tail modes can be compared
// on what they report per occurrence and not only on which occurrences.
std::map<OccInfo, std::vector<int>> collect_samples(const BioFMI& idx,
                                                    const BioFMI::ResultMap& rm) {
    std::map<OccInfo, std::vector<int>> out;
    for (const auto& [seq, occs] : rm)
        for (const auto& o : occs) out[{(int)o.position, o.changes}] = idx.expand_paths(o.paths);
    return out;
}

// Substrings of every path, at every length in [lo, hi].
std::set<std::string> path_substrings(const Panel& p, size_t lo, size_t hi) {
    std::set<std::string> out;
    for (const auto& c : all_choices(p)) {
        auto s = spell(p, c);
        for (size_t len = lo; len <= hi && len <= s.str.size(); len++)
            for (size_t i = 0; i + len <= s.str.size(); i++)
                out.insert(s.str.substr(i, len));
    }
    return out;
}

int failures = 0;

void report(unsigned seed, int l, const Panel& p, const std::string& pat,
            const char* mode, const std::set<OccInfo>& got, const std::set<OccInfo>& want) {
    if (++failures > 6) return;
    std::cerr << "\n  MISMATCH seed=" << seed << " l=" << l << " mode=" << mode
              << "\n    eds     : " << p.eds_text()
              << "\n    pattern : \"" << pat << "\" (len " << pat.size() << ")"
              << "\n    got " << got.size() << ", want " << want.size() << "\n";
    auto dump = [](const char* tag, const std::set<OccInfo>& s) {
        std::cerr << "    " << tag << ":";
        int n = 0;
        for (const auto& o : s) {
            if (++n > 6) { std::cerr << " ..."; break; }
            std::cerr << " (" << o.position << ",[";
            for (int c : o.changes) std::cerr << c << " ";
            std::cerr << "])";
        }
        std::cerr << "\n";
    };
    dump("got ", got);
    dump("want", want);
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. CARTESIAN: locate() with no sources against the full cartesian language
// ---------------------------------------------------------------------------
void test_fuzz_cartesian() {
    std::cout << "Test 1: randomised CARTESIAN vs brute force... " << std::flush;
    int before = failures, panels = 0, patterns = 0;

    for (unsigned seed = 1; seed <= 60; seed++) {
        std::mt19937 rng(seed);
        for (int l : {3, 4, 5}) {
            Panel p = random_panel(rng, l);
            std::istringstream ss(p.eds_text());
            EDS eds(ss);
            BioFMI idx(std::move(eds), l);
            idx.build();
            idx.set_tail_threshold(0);   // search every tail; the verify pass below sets its own
            panels++;

            for (const auto& pat : path_substrings(p, (size_t)l + 1, (size_t)(2 * l + 3))) {
                auto got = collect(idx.locate(pat));
                auto want = oracle_cartesian(p, pat);
                patterns++;
                if (got != want) report(seed, l, p, pat, "cartesian", got, want);

                // The same pattern with its tail verified rather than searched.
                if (pat.size() % (size_t)(l + 1) != 0) {
                    idx.set_tail_threshold((size_t)l + 1);
                    auto verified = collect(idx.locate(pat));
                    idx.set_tail_threshold(0);
                    if (verified != want)
                        report(seed, l, p, pat, "cartesian/verify", verified, want);
                }
            }
        }
    }
    std::cout << panels << " panels, " << patterns << " patterns... ";
    assert(failures == before && "randomised CARTESIAN locate disagrees with brute force");
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// 2. LINEAR: locate() with sources against only the strings paths spell
// ---------------------------------------------------------------------------
void test_fuzz_linear() {
    std::cout << "Test 2: randomised LINEAR vs brute force... " << std::flush;
    int before = failures, panels = 0, patterns = 0;

    for (unsigned seed = 101; seed <= 160; seed++) {
        std::mt19937 rng(seed);
        for (int l : {3, 4, 5}) {
            Panel p = random_panel(rng, l);
            auto edz = write_edz(p);
            std::istringstream ss(p.eds_text());
            EDS eds(ss);
            BioFMI idx(std::move(eds), l);
            idx.build();
            idx.attach_sources(edz, Sources::Format::EDZ);
            idx.set_tail_threshold(0);   // search every tail; the verify pass below sets its own
            panels++;

            for (const auto& pat : path_substrings(p, (size_t)l + 1, (size_t)(2 * l + 3))) {
                const auto searched = idx.locate(pat);
                auto got = collect(searched);
                auto want = oracle_linear(p, pat);
                patterns++;
                if (got != want) report(seed, l, p, pat, "linear", got, want);

                // Verified tail: the same entries, and the same genomes per entry.
                if (pat.size() % (size_t)(l + 1) != 0) {
                    idx.set_tail_threshold((size_t)l + 1);
                    const auto verified_rm = idx.locate(pat);
                    idx.set_tail_threshold(0);
                    auto verified = collect(verified_rm);
                    if (verified != want) {
                        report(seed, l, p, pat, "linear/verify", verified, want);
                    } else if (collect_samples(idx, verified_rm) != collect_samples(idx, searched)) {
                        report(seed, l, p, pat, "linear/verify sample sets", verified, want);
                    }
                }
            }
        }
    }
    std::cout << panels << " panels, " << patterns << " patterns... ";
    assert(failures == before && "randomised LINEAR locate disagrees with brute force");
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// 3. LINEAR results are always a subset of CARTESIAN on the same index
// ---------------------------------------------------------------------------
void test_fuzz_linear_subset() {
    std::cout << "Test 3: LINEAR <= CARTESIAN on every panel... " << std::flush;
    int bad = 0;
    for (unsigned seed = 201; seed <= 240; seed++) {
        std::mt19937 rng(seed);
        int l = 4;
        Panel p = random_panel(rng, l);
        auto edz = write_edz(p);

        std::istringstream s1(p.eds_text());
        EDS e1(s1); BioFMI cart(std::move(e1), l); cart.build();
        std::istringstream s2(p.eds_text());
        EDS e2(s2); BioFMI lin(std::move(e2), l); lin.build();
        lin.attach_sources(edz, Sources::Format::EDZ);

        for (const auto& pat : path_substrings(p, (size_t)l + 1, (size_t)(2 * l + 2))) {
            auto a = collect(cart.locate(pat));
            auto b = collect(lin.locate(pat));
            for (const auto& o : b)
                if (!a.count(o) && ++bad <= 3)
                    std::cerr << "\n  seed=" << seed << " LINEAR returned an entry CARTESIAN did not,"
                              << " pattern \"" << pat << "\"\n";
        }
    }
    assert(bad == 0 && "attaching sources invented an entry");
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// 4. Sample sets name exactly the paths whose string contains the match
// ---------------------------------------------------------------------------
void test_fuzz_sample_sets() {
    std::cout << "Test 4: reported sample sets vs the paths themselves... " << std::flush;
    int bad = 0;
    for (unsigned seed = 301; seed <= 340; seed++) {
        std::mt19937 rng(seed);
        int l = 4;
        Panel p = random_panel(rng, l);
        auto edz = write_edz(p);
        std::istringstream ss(p.eds_text());
        EDS eds(ss);
        BioFMI idx(std::move(eds), l);
        idx.build();
        idx.attach_sources(edz, Sources::Format::EDZ);

        for (const auto& pat : path_substrings(p, (size_t)l + 1, (size_t)(2 * l + 2))) {
            // Which paths really contain this pattern?
            std::set<int> truth;
            for (int path = 1; path <= p.num_paths; path++)
                if (spell(p, p.path_choice[path - 1]).str.find(pat) != std::string::npos)
                    truth.insert(path);

            std::set<int> named;
            for (const auto& [seq, occs] : idx.locate(pat))
                for (const auto& o : occs)
                    for (int id : idx.expand_paths(o.paths)) named.insert(id);

            // A named path must really carry the pattern. (The converse can fail
            // legitimately: a path may contain the pattern at a position some
            // other entry reports, and entries are per position, not per path.)
            for (int id : named)
                if (!truth.count(id) && ++bad <= 3)
                    std::cerr << "\n  seed=" << seed << " named path " << id
                              << " for \"" << pat << "\" which it does not contain\n";
        }
    }
    assert(bad == 0 && "a reported sample set named a path without the match");
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// 5. Split regular symbols: one segment, whatever the symbol count
// ---------------------------------------------------------------------------
void test_fuzz_split_regular() {
    std::cout << "Test 5: split regular symbols, both modes, vs brute force and count()... " << std::flush;
    int before = failures, panels = 0, patterns = 0, split_runs = 0;

    for (unsigned seed = 401; seed <= 420; seed++) {
        std::mt19937 rng(seed);
        for (int l : {3, 4, 5}) {
            Panel p = random_panel(rng, l);
            split_regular_symbols(p, rng);
            for (size_t i = 0; i < p.symbols.size(); i++) split_runs += p.written_as(i) > 1;
            auto edz = write_edz(p);

            std::istringstream s1(p.eds_text());
            EDS e1(s1);
            BioFMI cart(std::move(e1), l);
            cart.build();
            std::istringstream s2(p.eds_text());
            EDS e2(s2);
            BioFMI lin(std::move(e2), l);
            lin.build();
            lin.attach_sources(edz, Sources::Format::EDZ);
            panels++;

            for (const auto& pat : path_substrings(p, 1, (size_t)(2 * l + 3))) {
                patterns++;
                const auto rm_c = cart.locate(pat);
                auto got_c = collect(rm_c);
                auto want_c = oracle_cartesian(p, pat);
                if (got_c != want_c) report(seed, l, p, pat, "split/cartesian", got_c, want_c);
                const auto rm_l = lin.locate(pat);
                auto got_l = collect(rm_l);
                auto want_l = oracle_linear(p, pat);
                if (got_l != want_l) report(seed, l, p, pat, "split/linear", got_l, want_l);

                // count() runs the same search with a discarding sink, and on a
                // single chunk reads reference hits off the SA range: it must
                // agree with locate() on the run-merged T0 too.
                for (auto* run : {&rm_c, &rm_l}) {
                    size_t entries = 0;
                    for (const auto& [seq, occs] : *run) entries += occs.size();
                    BioFMI& idx = (run == &rm_c) ? cart : lin;
                    if (idx.count(pat) != entries && ++failures <= 6)
                        std::cerr << "\n  MISMATCH seed=" << seed << " l=" << l
                                  << " split count(\"" << pat << "\") = " << idx.count(pat)
                                  << ", locate() has " << entries << " entries\n";
                }
            }
        }
    }
    std::cout << panels << " panels (" << split_runs << " split runs), "
              << patterns << " patterns... ";
    assert(split_runs > 0 && "the generator produced no split run");
    assert(failures == before && "locate on split regular symbols disagrees with brute force");
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// 6. count() and the streaming locate() against locate() and brute force
// ---------------------------------------------------------------------------
//
// Neither builds the result locate() returns: count() never constructs an
// entry, and the streaming locate() hands each one over as the final chunk
// finds it. Both must still produce exactly the entries locate() does —
// compared as a *multiset* with sample sets, not as a set, because count()'s
// contract is the number of entries, duplicates included — and those must be
// the brute-force oracle's. Short patterns (|P| <= l) are where the two
// differ most from the old path (a single chunk, nothing stored at all, and
// count() reading reference hits off the suffix-array range), so every
// length from 1 is covered, with both tail modes for the longer ones.
namespace {

using Entry = std::tuple<int, std::vector<int>, std::vector<int>>;   // pos, changes, genomes

std::multiset<Entry> entries_of(const BioFMI& idx, const BioFMI::ResultMap& rm) {
    std::multiset<Entry> out;
    for (const auto& [seq, occs] : rm)
        for (const auto& o : occs)
            out.insert({(int)o.position, o.changes, idx.expand_paths(o.paths)});
    return out;
}

void check_count_and_stream(BioFMI& idx, const Panel& p, unsigned seed, int l,
                            const std::string& pat, bool linear, int& checked) {
    const auto rm = idx.locate(pat);
    const auto materialised = entries_of(idx, rm);

    std::multiset<Entry> streamed;
    const size_t n_stream = idx.locate(pat, [&](const BioFMI::Occurrence& o) {
        streamed.insert({(int)o.position, o.changes, idx.expand_paths(o.paths)});
    });
    const size_t n_count = idx.count(pat);
    checked++;

    std::set<OccInfo> streamed_set;
    for (const auto& [pos, ch, ids] : streamed) streamed_set.insert({pos, ch});
    const auto want = linear ? oracle_linear(p, pat) : oracle_cartesian(p, pat);
    const char* mode = linear ? "linear/stream" : "cartesian/stream";

    if (streamed_set != want) {
        report(seed, l, p, pat, mode, streamed_set, want);
    } else if (streamed != materialised || n_stream != materialised.size() ||
               n_count != materialised.size()) {
        if (++failures <= 6)
            std::cerr << "\n  MISMATCH seed=" << seed << " l=" << l << " mode=" << mode
                      << "\n    eds     : " << p.eds_text()
                      << "\n    pattern : \"" << pat << "\""
                      << "\n    locate() " << materialised.size() << " entries, streamed "
                      << streamed.size() << " (returned " << n_stream << "), count() "
                      << n_count << ", multisets " << (streamed == materialised ? "equal" : "differ")
                      << "\n";
    }
}

}  // namespace

void test_fuzz_count_and_stream() {
    std::cout << "Test 6: count() and streaming locate() vs locate() and brute force... "
              << std::flush;
    int before = failures, panels = 0, checked = 0;

    // 20 seeds: each is an index per mode and runs every pattern four ways, and
    // this is already the slowest test in the suite.
    for (unsigned seed = 401; seed <= 420; seed++) {
        std::mt19937 rng(seed);
        for (int l : {3, 4, 5}) {
            Panel p = random_panel(rng, l);
            for (bool linear : {false, true}) {
                std::istringstream ss(p.eds_text());
                EDS eds(ss);
                BioFMI idx(std::move(eds), l);
                idx.build();
                if (linear) idx.attach_sources(write_edz(p), Sources::Format::EDZ);
                panels++;

                for (const auto& pat : path_substrings(p, 1, (size_t)(2 * l + 3))) {
                    // Default: every tail verified.
                    check_count_and_stream(idx, p, seed, l, pat, linear, checked);
                    // And searched, for the patterns that have a tail at all.
                    if (pat.size() > (size_t)(l + 1) && pat.size() % (size_t)(l + 1) != 0) {
                        idx.set_tail_threshold(0);
                        check_count_and_stream(idx, p, seed, l, pat, linear, checked);
                        idx.set_tail_threshold(std::numeric_limits<size_t>::max());
                    }
                }
                // Patterns that occur nowhere: count 0, no callback.
                for (const std::string& miss :
                     std::vector<std::string>{"N", "ACGN", std::string(3 * l, 'N')}) {
                    size_t calls = 0;
                    const size_t n = idx.locate(miss, [&](const BioFMI::Occurrence&) { calls++; });
                    if (n != 0 || calls != 0 || idx.count(miss) != 0) {
                        if (++failures <= 6)
                            std::cerr << "\n  seed=" << seed << " absent pattern \"" << miss
                                      << "\" reported " << n << "/" << calls << "\n";
                    }
                }
            }
        }
    }
    std::cout << panels << " indexes, " << checked << " checks... ";
    assert(failures == before && "count() or streaming locate() disagrees with locate()");
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// 7. The streaming path is reusable after a callback throws
// ---------------------------------------------------------------------------
void test_stream_exception() {
    std::cout << "Test 7: a throwing callback leaves the index usable... " << std::flush;
    const int l = 4;
    Panel p;
    for (unsigned seed = 7;; seed++) {   // the first panel with a path long enough
        std::mt19937 rng(seed);
        p = random_panel(rng, l);
        if (spell(p, p.path_choice[0]).str.size() >= (size_t)(2 * l + 2)) break;
    }
    std::istringstream ss(p.eds_text());
    EDS eds(ss);
    BioFMI idx(std::move(eds), l);
    idx.build();

    // Two characters: one chunk, so the abandoned search stored nothing, and
    // a two-chunk pattern, so it abandoned candidate maps as well.
    for (const auto& pat : {std::string("A"), spell(p, p.path_choice[0]).str.substr(0, 2 * l + 2)}) {
        const size_t before = idx.count(pat);
        assert(before > 0);
        bool threw = false;
        try {
            idx.locate(pat, [](const BioFMI::Occurrence&) { throw std::runtime_error("stop"); });
        } catch (const std::runtime_error&) {
            threw = true;
        }
        assert(threw && "the callback's exception must propagate");
        assert(idx.count(pat) == before && "a later count() saw state left by the abandoned search");
        size_t n = 0;
        idx.locate(pat, [&](const BioFMI::Occurrence&) { n++; });
        assert(n == before);
    }
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Randomised differential locate tests\n";
    std::cout << "========================================\n\n";
    try {
        test_fuzz_cartesian();
        test_fuzz_linear();
        test_fuzz_linear_subset();
        test_fuzz_sample_sets();
        test_fuzz_split_regular();
        test_fuzz_count_and_stream();
        test_stream_exception();
        std::error_code ec;   // the per-process sources file, now unused
        std::filesystem::remove(edz_path("biofmi_fuzz_sources"), ec);
        std::cout << "\n========================================\n";
        std::cout << "ALL FUZZ TESTS PASSED\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nFAILED: " << e.what() << "\n";
        return 1;
    }
}
