/**
 * Seeded random l-EDS panels with consistent sources, and the brute-force
 * spelling of a path through one. Shared by the randomised tests
 * (test_locate_fuzz, test_extract).
 *
 * The generator is biased towards the structures that have broken locate()
 * before: empty alternatives, degenerate symbols at the very start and end,
 * alternatives shorter and longer than l, minimum-width internal segments.
 */
#ifndef BIOFMI_TEST_FUZZ_PANEL_HPP
#define BIOFMI_TEST_FUZZ_PANEL_HPP

#include "index/index.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <unistd.h>
#include <vector>

namespace fuzz {

using namespace biofmi;

// ---------------------------------------------------------------- generation

struct Panel {
    std::vector<std::vector<std::string>> symbols;  // symbols[i] = alternatives
    std::vector<bool> is_degen;
    std::vector<std::vector<int>> alt_paths;        // global alt index -> path ids (1-based)
    std::vector<std::vector<int>> path_choice;      // path id-1 -> chosen alt per degen symbol
    int num_paths = 0;
    int l = 0;
    // Optional: a regular symbol written as several consecutive regular
    // symbols, {CGCG}{A}{TGCC}, the way older vcf2eds wrote one segment.
    // Concatenated they are symbols[i][0]; empty = written as one bare run.
    // See split_regular_symbols().
    std::vector<std::vector<std::string>> pieces;

    // Number of regular symbols symbol i is written as.
    size_t written_as(size_t i) const {
        return (i < pieces.size() && !pieces[i].empty()) ? pieces[i].size() : 1;
    }

    std::string eds_text() const {
        std::string out;
        for (size_t i = 0; i < symbols.size(); i++) {
            if (!is_degen[i] && i < pieces.size() && !pieces[i].empty()) {
                for (const auto& piece : pieces[i]) out += "{" + piece + "}";
                continue;
            }
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

inline std::string random_dna(std::mt19937& rng, size_t len) {
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
inline Panel random_panel(std::mt19937& rng, int l) {
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

/**
 * Write every regular symbol of `p` as 1..3 consecutive regular symbols, cut at
 * random points — empty pieces included, as `{}`. The panel spells exactly the
 * same strings, so the oracle is unchanged and every answer, positions
 * included, must be too: a run of regular symbols is one context segment.
 * Only the written form changes — and with it the string ids the sources pair
 * with, which write_edz() follows.
 */
inline void split_regular_symbols(Panel& p, std::mt19937& rng) {
    p.pieces.assign(p.symbols.size(), {});
    for (size_t i = 0; i < p.symbols.size(); i++) {
        if (p.is_degen[i]) continue;
        const std::string& seg = p.symbols[i][0];
        const size_t k = 1 + rng() % 3;
        std::vector<size_t> cuts = {0, seg.size()};
        for (size_t c = 1; c < k; c++) cuts.push_back(rng() % (seg.size() + 1));
        std::sort(cuts.begin(), cuts.end());
        for (size_t c = 0; c + 1 < cuts.size(); c++)
            p.pieces[i].push_back(seg.substr(cuts[c], cuts[c + 1] - cuts[c]));
    }
}

// One concrete string spelled by a choice of alternative per degenerate symbol,
// with enough metadata to recover positions and traversed alternatives.
struct Spelled {
    std::string str;
    std::vector<int> pos;                    // T0 position of each character
    std::vector<int> alt;                    // global alt index, or -1 for reference
    std::vector<std::vector<int>> empties;   // empty alts traversed before this char
};

inline Spelled spell(const Panel& p, const std::vector<int>& choice) {
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

// Every combination of alternatives — the CARTESIAN language.
inline std::vector<std::vector<int>> all_choices(const Panel& p) {
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

// Where write_edz(p, stem) writes: per test binary (`stem`) and per process,
// so neither parallel ctest nor two copies of one suite running at once on a
// shared machine rewrite each other's sources mid-test.
inline std::filesystem::path edz_path(const std::string& stem) {
    return std::filesystem::temp_directory_path() /
           (stem + "." + std::to_string(getpid()) + ".edz");
}

inline std::filesystem::path write_edz(const Panel& p,
                                       const std::string& stem = "biofmi_fuzz_sources") {
    auto path = edz_path(stem);
    std::ofstream os(path, std::ios::binary);
    Sources::write_edz_header(os, (size_t)p.num_paths);
    size_t cardinality = 0;
    for (size_t i = 0; i < p.symbols.size(); i++) {
        if (!p.is_degen[i]) {
            for (size_t k = 0; k < p.written_as(i); k++) {             // common: all paths
                Sources::write_edz_entry(os, PathSet{0}, (size_t)p.num_paths);
                cardinality++;
            }
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

}  // namespace fuzz

#endif  // BIOFMI_TEST_FUZZ_PANEL_HPP
