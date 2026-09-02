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
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace biofmi;

namespace {

// ---------------------------------------------------------------- generation

struct Panel {
    std::vector<std::vector<std::string>> symbols;  // symbols[i] = alternatives
    std::vector<bool> is_degen;
    std::vector<std::vector<int>> alt_paths;        // global alt index -> path ids (1-based)
    std::vector<std::vector<int>> path_choice;      // path id-1 -> chosen alt per degen symbol
    int num_paths = 0;
    int l = 0;

    std::string eds_text() const {
        std::string out;
        for (size_t i = 0; i < symbols.size(); i++) {
            if (!is_degen[i]) { out += symbols[i][0]; continue; }
            out += '{';
            for (size_t a = 0; a < symbols[i].size(); a++) {
                if (a) out += ',';
                out += symbols[i][a];
            }
            out += '}';
        }
        return out;
    }

    // Global 0-based alternative index of alternative `a` in symbol `sym`.
    int global_alt(size_t sym, size_t a) const {
        int idx = 0;
        for (size_t i = 0; i < sym; i++) if (is_degen[i]) idx += (int)symbols[i].size();
        return idx + (int)a;
    }
};

// Zero-length alternatives are legal EDS, and two of the nine bugs lived there:
// a match crossing one where the junction falls on a chunk boundary (nothing
// straddles it, so only the stitch can bridge it), and a symbol listing the
// empty string twice, which is two alternatives and so two occurrences.
// Both are covered now, so this stays on — turning it off narrows the harness.
constexpr bool kGenerateEmptyAlternatives = true;

std::string random_dna(std::mt19937& rng, size_t len) {
    static const char* A = "ACGT";
    std::string s(len, 'A');
    for (auto& c : s) c = A[rng() % 4];
    return s;
}

/**
 * A random l-EDS.
 *
 * Internal non-degenerate segments are at least l characters, which is what
 * biofmi-build validates. Boundary segments may be shorter — that asymmetry has
 * its own history of bugs, so it is exercised rather than avoided.
 */
Panel random_panel(std::mt19937& rng, int l) {
    Panel p;
    p.l = l;
    p.num_paths = 2 + (int)(rng() % 3);              // 2..4 paths

    const int n_degen = 1 + (int)(rng() % 4);        // 1..4 degenerate symbols
    const bool lead_degen = (rng() % 4) == 0;        // sometimes start degenerate
    const bool trail_degen = (rng() % 4) == 0;       // sometimes end degenerate

    auto push_common = [&](bool boundary) {
        // Boundary segments are allowed to be short; internal ones are not.
        size_t len = boundary ? (1 + rng() % (size_t)(l + 2))
                              : ((size_t)l + rng() % 4);
        p.symbols.push_back({random_dna(rng, len)});
        p.is_degen.push_back(false);
    };
    auto push_degen = [&]() {
        int k = 2 + (int)(rng() % 2);                // 2..3 alternatives
        std::vector<std::string> alts;
        for (int a = 0; a < k; a++) {
            // Length 0 with real probability: empty alternatives are where the
            // recall bug lived, and they are legal EDS.
            size_t len = (kGenerateEmptyAlternatives && rng() % 4 == 0)
                             ? 0 : (1 + rng() % (size_t)(l + 1));
            alts.push_back(random_dna(rng, len));
        }
        // Two identical alternatives make the oracle's dedup ambiguous without
        // testing anything new, so nudge one.
        for (size_t a = 1; a < alts.size(); a++)
            if (alts[a] == alts[0]) alts[a] += random_dna(rng, 1);
        p.symbols.push_back(alts);
        p.is_degen.push_back(true);
    };

    if (lead_degen) push_degen();
    for (int i = 0; i < n_degen; i++) {
        push_common(i == 0 && !lead_degen);
        push_degen();
    }
    if (!trail_degen) push_common(true);

    // Sources: each path picks one alternative per degenerate symbol, and an
    // alternative's source set is the paths that picked it. Deriving sources
    // from paths this way keeps them consistent by construction.
    p.path_choice.assign(p.num_paths, {});
    int total_alts = 0;
    for (size_t i = 0; i < p.symbols.size(); i++)
        if (p.is_degen[i]) total_alts += (int)p.symbols[i].size();
    p.alt_paths.assign(total_alts, {});

    for (int path = 1; path <= p.num_paths; path++) {
        for (size_t i = 0; i < p.symbols.size(); i++) {
            if (!p.is_degen[i]) continue;
            int a = (int)(rng() % p.symbols[i].size());
            p.path_choice[path - 1].push_back(a);
            p.alt_paths[p.global_alt(i, a)].push_back(path);
        }
    }
    return p;
}

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

// One concrete string spelled by a choice of alternative per degenerate symbol,
// with enough metadata to recover positions and traversed alternatives.
struct Spelled {
    std::string str;
    std::vector<int> pos;                    // T0 position of each character
    std::vector<int> alt;                    // global alt index, or -1 for reference
    std::vector<std::vector<int>> empties;   // empty alts traversed before this char
};

Spelled spell(const Panel& p, const std::vector<int>& choice) {
    Spelled s;
    int t0 = 0, di = 0;
    std::vector<int> pending;
    for (size_t i = 0; i < p.symbols.size(); i++) {
        if (!p.is_degen[i]) {
            const std::string& seg = p.symbols[i][0];
            for (size_t k = 0; k < seg.size(); k++) {
                s.str += seg[k];
                s.pos.push_back(t0 + (int)k);
                s.alt.push_back(-1);
                s.empties.push_back(pending);
                pending.clear();
            }
            t0 += (int)seg.size();
        } else {
            int a = choice[di++];
            const std::string& alt = p.symbols[i][a];
            int g = p.global_alt(i, a);
            if (alt.empty()) {
                pending.push_back(g);        // traversed, contributes no character
            } else {
                for (size_t k = 0; k < alt.size(); k++) {
                    s.str += alt[k];
                    s.pos.push_back(t0 + (int)k);
                    s.alt.push_back(g);
                    s.empties.push_back(pending);
                    pending.clear();
                }
            }
        }
    }
    return s;
}

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

// Every combination of alternatives — the CARTESIAN language.
std::vector<std::vector<int>> all_choices(const Panel& p) {
    std::vector<std::vector<int>> out = {{}};
    for (size_t i = 0; i < p.symbols.size(); i++) {
        if (!p.is_degen[i]) continue;
        std::vector<std::vector<int>> next;
        for (const auto& c : out)
            for (size_t a = 0; a < p.symbols[i].size(); a++) {
                auto n = c; n.push_back((int)a); next.push_back(n);
            }
        out = std::move(next);
    }
    return out;
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

std::filesystem::path write_edz(const Panel& p) {
    auto path = std::filesystem::temp_directory_path() / "biofmi_fuzz_sources.edz";
    std::ofstream os(path, std::ios::binary);
    Sources::write_edz_header(os, (size_t)p.num_paths);
    size_t cardinality = 0;
    for (size_t i = 0; i < p.symbols.size(); i++) {
        if (!p.is_degen[i]) {
            Sources::write_edz_entry(os, PathSet{0}, (size_t)p.num_paths);  // common: all paths
            cardinality++;
        } else {
            for (size_t a = 0; a < p.symbols[i].size(); a++) {
                Sources::write_edz_entry(os, p.alt_paths[p.global_alt(i, a)],
                                         (size_t)p.num_paths);
                cardinality++;
            }
        }
    }
    Sources::write_edz_finalize(os, cardinality);
    os.close();
    return path;
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
            panels++;

            for (const auto& pat : path_substrings(p, (size_t)l + 1, (size_t)(2 * l + 3))) {
                auto got = collect(idx.locate(pat));
                auto want = oracle_cartesian(p, pat);
                patterns++;
                if (got != want) report(seed, l, p, pat, "cartesian", got, want);
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
            panels++;

            for (const auto& pat : path_substrings(p, (size_t)l + 1, (size_t)(2 * l + 3))) {
                auto got = collect(idx.locate(pat));
                auto want = oracle_linear(p, pat);
                patterns++;
                if (got != want) report(seed, l, p, pat, "linear", got, want);
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

int main() {
    std::cout << "========================================\n";
    std::cout << "Randomised differential locate tests\n";
    std::cout << "========================================\n\n";
    try {
        test_fuzz_cartesian();
        test_fuzz_linear();
        test_fuzz_linear_subset();
        test_fuzz_sample_sets();
        std::cout << "\n========================================\n";
        std::cout << "ALL FUZZ TESTS PASSED\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nFAILED: " << e.what() << "\n";
        return 1;
    }
}
