/**
 * Genome extraction and genome coordinates (TODO §9) against a brute-force
 * oracle that spells each genome's path.
 *
 * Under the partition requirement a genome is a path through the l-EDS, so the
 * oracle is that path spelled out: extract(g, i, j) must equal the substring,
 * genome_length(g) its length, and every occurrence locate() reports must map,
 * for every genome carrying it, to the index in that genome's string where the
 * same (position, changes) occurrence begins — and to -1 for every genome that
 * does not carry it. All of it at several sampling rates, since the sampled
 * prefix sums plus a walk must agree with full sums (b = 1) everywhere.
 *
 * Hand-written panels pin the structures that have broken things before
 * (empty alternatives, a symbol listing the empty string twice, symbols at the
 * very start and end, short boundary segments); seeded random panels from
 * fuzz_panel.hpp cover the rest. Failures print the seed or the EDS.
 */

#include "index/index.hpp"
#include "fuzz_panel.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace biofmi;
using namespace fuzz;

namespace {

int failures = 0;

void fail(const std::string& where, const std::string& what) {
    if (++failures <= 8) std::cerr << "\n  MISMATCH " << where << ": " << what << "\n";
}

// A panel from symbol lists, with one genome per given choice vector.
Panel make_panel(const std::vector<std::vector<std::string>>& symbols, int l,
                 const std::vector<std::vector<int>>& choices) {
    Panel p;
    p.l = l;
    p.symbols = symbols;
    for (const auto& s : symbols) p.is_degen.push_back(s.size() > 1);
    p.num_paths = (int)choices.size();
    p.path_choice = choices;
    int total = 0;
    for (size_t i = 0; i < symbols.size(); i++)
        if (p.is_degen[i]) total += (int)symbols[i].size();
    p.alt_paths.assign(total, {});
    for (int g = 1; g <= p.num_paths; g++) {
        int d = 0;
        for (size_t i = 0; i < symbols.size(); i++)
            if (p.is_degen[i]) p.alt_paths[p.global_alt(i, choices[g - 1][d++])].push_back(g);
    }
    return p;
}

// Every combination of alternatives as its own genome: every path is a genome.
std::vector<std::vector<int>> every_path(const std::vector<std::vector<std::string>>& symbols) {
    std::vector<std::vector<int>> out = {{}};
    for (const auto& s : symbols) {
        if (s.size() < 2) continue;
        std::vector<std::vector<int>> next;
        for (const auto& c : out)
            for (int a = 0; a < (int)s.size(); a++) { auto n = c; n.push_back(a); next.push_back(n); }
        out = std::move(next);
    }
    return out;
}

// The (position, changes) locate() reports for a match of `len` characters
// starting at index i of a spelled path — collect_from()'s rule in the fuzz test.
std::pair<int, std::vector<int>> occurrence_at(const Spelled& s, int i, int len) {
    std::vector<int> changes;
    for (int j = i; j < i + len; j++) {
        if (j > i)
            for (int e : s.empties[j])
                if (changes.empty() || changes.back() != e) changes.push_back(e);
        if (s.alt[j] >= 0 && (changes.empty() || changes.back() != s.alt[j]))
            changes.push_back(s.alt[j]);
    }
    return {s.pos[i], changes};
}

std::unique_ptr<BioFMI> build(const Panel& p) {
    auto edz = write_edz(p, "biofmi_extract_sources");
    std::istringstream ss(p.eds_text());
    EDS eds(ss);
    auto idx = std::make_unique<BioFMI>(std::move(eds), p.l);
    idx->build();
    idx->attach_sources(edz, Sources::Format::EDZ);
    return idx;
}

// Everything checked on one panel at one sampling rate.
void check_panel(BioFMI& idx, const Panel& p, size_t rate, const std::string& tag,
                 bool every_range, size_t max_pat) {
    idx.build_genome_map(rate);
    const std::string where = tag + " b=" + std::to_string(rate);

    std::vector<Spelled> genome;
    for (int g = 1; g <= p.num_paths; g++) genome.push_back(spell(p, p.path_choice[g - 1]));

    // Length and extract.
    for (int g = 1; g <= p.num_paths; g++) {
        const std::string& truth = genome[g - 1].str;
        if (idx.genome_length(g) != truth.size()) {
            fail(where, "genome " + std::to_string(g) + " length " +
                        std::to_string(idx.genome_length(g)) + ", want " + std::to_string(truth.size()));
            continue;
        }
        if (idx.extract(g, 0, truth.size()) != truth)
            fail(where, "genome " + std::to_string(g) + " whole: got \"" +
                        idx.extract(g, 0, truth.size()) + "\", want \"" + truth + "\"");
        for (size_t i = 0; i <= truth.size(); i++) {
            for (size_t j = i; j <= truth.size(); j++) {
                if (!every_range && j != truth.size() && j - i > (size_t)p.l + 3) continue;
                const std::string got = idx.extract(g, i, j);
                if (got != truth.substr(i, j - i))
                    fail(where, "extract(" + std::to_string(g) + "," + std::to_string(i) + "," +
                                std::to_string(j) + ") = \"" + got + "\", want \"" +
                                truth.substr(i, j - i) + "\"");
            }
        }
        bool threw = false;
        try { (void)idx.extract(g, 0, truth.size() + 1); } catch (const std::out_of_range&) { threw = true; }
        if (!threw) fail(where, "extract past the end did not throw");
    }

    // Coordinates: every occurrence of every substring of every genome.
    std::set<std::string> patterns;
    for (const auto& s : genome)
        for (size_t len = 1; len <= max_pat && len <= s.str.size(); len++)
            for (size_t i = 0; i + len <= s.str.size(); i++) patterns.insert(s.str.substr(i, len));

    for (const auto& pat : patterns) {
        // Oracle: where each genome holds each occurrence.
        std::vector<std::map<std::pair<int, std::vector<int>>, int>> at(p.num_paths + 1);
        for (int g = 1; g <= p.num_paths; g++) {
            const Spelled& s = genome[g - 1];
            for (int i = 0; i + (int)pat.size() <= (int)s.str.size(); i++)
                if (s.str.compare(i, pat.size(), pat) == 0)
                    at[g][occurrence_at(s, i, (int)pat.size())] = i;
        }

        size_t reported = 0;
        for (const auto& [seq, occs] : idx.locate(pat)) {
            for (const auto& occ : occs) {
                reported++;
                const std::pair<int, std::vector<int>> key{(int)occ.position, occ.changes};
                std::vector<std::pair<int, int64_t>> carriers;
                for (int g = 1; g <= p.num_paths; g++) {
                    auto it = at[g].find(key);
                    const int64_t want = (it == at[g].end()) ? -1 : it->second;
                    const int64_t got = idx.genome_position(g, occ);
                    if (got != want)
                        fail(where, "\"" + pat + "\" at (" + std::to_string(occ.position) +
                                    ") genome " + std::to_string(g) + ": " + std::to_string(got) +
                                    ", want " + std::to_string(want));
                    if (want >= 0) carriers.emplace_back(g, want);
                }
                if (idx.genome_positions(occ) != carriers)
                    fail(where, "\"" + pat + "\": genome_positions() disagrees with the oracle");
            }
        }
        // Every (genome, offset) occurrence is some reported entry's.
        size_t truth_entries = 0;
        std::set<std::pair<int, std::vector<int>>> distinct;
        for (int g = 1; g <= p.num_paths; g++)
            for (const auto& [k, i] : at[g]) distinct.insert(k);
        truth_entries = distinct.size();
        if (reported != truth_entries)
            fail(where, "\"" + pat + "\": " + std::to_string(reported) + " entries, want " +
                        std::to_string(truth_entries));
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. Hand-written panels, every path a genome, every range, every rate
// ---------------------------------------------------------------------------
void test_hand_written() {
    std::cout << "Test 1: hand-written panels vs spelled genomes... " << std::flush;
    const int before = failures;

    struct Case { const char* name; std::vector<std::vector<std::string>> symbols; };
    const std::vector<Case> cases = {
        {"empty alternatives",
         {{"ACGTA"}, {"C", ""}, {"GGTAC"}, {"", "T", "AA"}, {"CCG"}}},
        {"starts degenerate, empty listed twice",
         {{"AC", "G", ""}, {"TTTGCA"}, {"", "", "ATG"}, {"CAT"}}},
        {"ends degenerate",
         {{"AAACCC"}, {"G", "TTT"}}},
        {"both ends degenerate and empty",
         {{"A", ""}, {"CCCGGG"}, {"T", ""}}},
        {"short boundary segments",
         {{"AC"}, {"GT", "A"}, {"GTTA"}, {"", "C"}, {"T"}}},
        {"alternatives longer than the segments",
         {{"GAT"}, {"ACGTACGTAA", "C"}, {"TGA"}, {"TTTTTTT", "", "G"}, {"CA"}}},
        {"no degenerate symbol",
         {{"ACGTACGTTGCA"}}},
    };

    for (const auto& c : cases) {
        const Panel p = make_panel(c.symbols, 3, every_path(c.symbols));
        auto idx = build(p);
        for (size_t rate : {1, 2, 3, 64})
            check_panel(*idx, p, rate, std::string(c.name) + " [" + p.eds_text() + "]",
                        /*every_range=*/true, /*max_pat=*/10);
    }
    assert(failures == before && "extract/coordinates disagree on a hand-written panel");
    std::cout << cases.size() << " panels, PASSED\n";
}

// ---------------------------------------------------------------------------
// 2. Seeded random panels from the fuzz generator
// ---------------------------------------------------------------------------
void test_random_panels() {
    std::cout << "Test 2: seeded random panels vs spelled genomes... " << std::flush;
    const int before = failures;
    int panels = 0;
    for (unsigned seed = 501; seed <= 580; seed++) {
        std::mt19937 rng(seed);
        for (int l : {3, 4, 5}) {
            const Panel p = random_panel(rng, l);
            auto idx = build(p);
            const size_t rate = 1 + rng() % 4;
            check_panel(*idx, p, rate, "seed=" + std::to_string(seed) + " l=" + std::to_string(l) +
                        " [" + p.eds_text() + "]", /*every_range=*/false, (size_t)(2 * l + 3));
            if (rate != 1)
                check_panel(*idx, p, 1, "seed=" + std::to_string(seed) + " l=" + std::to_string(l),
                            false, (size_t)l + 1);
            panels++;
        }
    }
    assert(failures == before && "extract/coordinates disagree on a random panel");
    std::cout << panels << " panels, PASSED\n";
}

// ---------------------------------------------------------------------------
// 3. Refusals: broken partition, no map, no sources, bad arguments
// ---------------------------------------------------------------------------
void test_refusals() {
    std::cout << "Test 3: refusals... " << std::flush;
    auto throws = [](auto&& f) {
        try { f(); } catch (const std::exception&) { return true; }
        return false;
    };

    const std::vector<std::vector<std::string>> symbols = {{"ACGTA"}, {"C", "G"}, {"GGTAC"}};

    // Genome 2 carries both alternatives: not a path, so no coordinates.
    Panel both = make_panel(symbols, 3, {{0}, {1}});
    both.alt_paths[0] = {1, 2};
    auto idx = build(both);
    assert(throws([&] { idx->build_genome_map(1); }) && "a genome on two alternatives must refuse");

    // Genome 2 carries neither.
    Panel none = make_panel(symbols, 3, {{0}, {1}});
    none.alt_paths[1] = {};
    idx = build(none);
    assert(throws([&] { idx->build_genome_map(4); }));

    Panel ok = make_panel(symbols, 3, {{0}, {1}});
    idx = build(ok);
    assert(throws([&] { (void)idx->extract(1, 0, 1); }) && "no map built yet");
    assert(throws([&] { idx->build_genome_map(0); }) && "rate 0");
    idx->build_genome_map(2);
    assert(idx->extract(2, 3, 8) == "TAGGG");
    assert(throws([&] { (void)idx->extract(0, 0, 1); }) && "genome ids are 1-based");
    assert(throws([&] { (void)idx->extract(3, 0, 1); }) && "past the last genome");
    assert(throws([&] { (void)idx->extract(1, 4, 3); }) && "i > j");
    assert(idx->extract(1, 11, 11).empty());

    // Re-attaching sources drops the map built from the old ones.
    idx->attach_sources(write_edz(ok, "biofmi_extract_sources"), Sources::Format::EDZ);
    assert(!idx->has_genome_map());

    // CARTESIAN: no sources, no genomes to speak of.
    std::istringstream ss(ok.eds_text());
    EDS eds(ss);
    BioFMI cart(std::move(eds), 3);
    cart.build();
    assert(throws([&] { cart.build_genome_map(1); }));
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// 4. Regular symbols written as runs ({CGCG}{A}{TGCC}): one segment each
//
// parse_eds() gathers a run of regular symbols into one T0 block (TODO §7b),
// while string ids — and so .d2g and the sources — stay per written symbol.
// Genome coordinates read T0 through base_positions and the sources through
// .d2g, so a split panel must give exactly the answers of the unsplit one.
// ---------------------------------------------------------------------------
void test_split_regular_runs() {
    std::cout << "Test 4: regular symbols split into runs vs spelled genomes... " << std::flush;
    const int before = failures;
    int panels = 0, split_runs = 0;
    for (unsigned seed = 601; seed <= 640; seed++) {
        std::mt19937 rng(seed);
        for (int l : {3, 4, 5}) {
            Panel p = random_panel(rng, l);
            split_regular_symbols(p, rng);
            for (size_t i = 0; i < p.symbols.size(); i++) split_runs += p.written_as(i) > 1;
            auto idx = build(p);
            const size_t rate = 1 + rng() % 4;
            check_panel(*idx, p, rate, "split seed=" + std::to_string(seed) + " l=" +
                        std::to_string(l) + " [" + p.eds_text() + "]",
                        /*every_range=*/false, (size_t)(2 * l + 3));
            panels++;
        }
    }
    assert(split_runs > 0 && "the generator produced no split run");
    assert(failures == before && "extract/coordinates disagree on a split-run panel");
    std::cout << panels << " panels (" << split_runs << " split runs), PASSED\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Genome extraction and coordinates\n";
    std::cout << "========================================\n\n";
    try {
        test_hand_written();
        test_random_panels();
        test_refusals();
        test_split_regular_runs();
        std::error_code ec;   // the per-process sources file, now unused
        std::filesystem::remove(edz_path("biofmi_extract_sources"), ec);
        std::cout << "\n========================================\n";
        std::cout << "ALL EXTRACT TESTS PASSED\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nFAILED: " << e.what() << "\n";
        return 1;
    }
}
