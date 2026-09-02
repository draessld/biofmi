/**
 * Source-aware (LINEAR) locate() tests — issue B4.
 *
 * Semantics: docs/locate_spec.md § Search modes.
 *
 * Without sources, locate() pairs every alternative of one degenerate symbol
 * with every alternative of the next. That is the CARTESIAN language. On a
 * LINEAR l-EDS it over-reports, because the panel only contains combinations
 * some genome actually carries.
 *
 * The case that matters here is **non-transitivity of non-empty intersection**:
 *
 *     sources(a1) = {1,2}
 *     sources(a2) = {2,3}     a1 n a2 = {2}     non-empty
 *     sources(a3) = {3,4}     a2 n a3 = {3}     non-empty
 *                             a1 n a2 n a3 = {} no path carries the whole match
 *
 * An implementation that validates only the adjacent pair at each stitch passes
 * every check above and still returns the match. Only a running intersection
 * carried along the whole candidate rejects it. Every test below is built to
 * separate those two implementations.
 */

#include "index/index.hpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

using namespace biofmi;

namespace {

// EDS with three degenerate symbols, each a binary choice, separated by
// 6-character common runs so an l=3 index is valid.
//
//   symbol : 0        1     2        3     4        5     6
//   string : AAATTT  {G,C}  AAATTT  {G,C}  AAATTT  {G,C}  AAATTT
//   glob id: 0        1,2   3        4,5   6        7,8   9
const char* kEds = "AAATTT{G,C}AAATTT{G,C}AAATTT{G,C}AAATTT";
constexpr size_t kCardinality = 10;   // every string, common included
constexpr size_t kNumPaths    = 4;

// Source sets by global string id. Common symbols are universal ({0}); the
// three binary choices are wired so that G/G/G is pairwise-consistent but has
// an empty three-way intersection, while G/C/C is carried by path 1 throughout.
//
//   id 1 = G@1 {1,2}    id 4 = G@3 {2,3}    id 7 = G@5 {3,4}
//   id 2 = C@1 {3,4}    id 5 = C@3 {1,4}    id 8 = C@5 {1,2}
const std::vector<PathSet> kSources = {
    {0},        // 0  AAATTT
    {1, 2},     // 1  G
    {3, 4},     // 2  C
    {0},        // 3  AAATTT
    {2, 3},     // 4  G
    {1, 4},     // 5  C
    {0},        // 6  AAATTT
    {3, 4},     // 7  G
    {1, 2},     // 8  C
    {0},        // 9  AAATTT
};

BioFMI build_index(const std::string& eds_str, int l) {
    std::istringstream ss(eds_str);
    EDS eds(ss);
    BioFMI idx(std::move(eds), l);
    idx.build();
    return idx;
}

// Write kSources as a binary EDZ next to the test binary.
std::filesystem::path write_edz() {
    auto path = std::filesystem::temp_directory_path() / "biofmi_test_sources.edz";
    std::ofstream os(path, std::ios::binary);
    assert(os && "cannot open temp EDZ for writing");
    Sources::write_edz_header(os, kNumPaths);
    for (const auto& ps : kSources) {
        Sources::write_edz_entry(os, ps, kNumPaths);
    }
    Sources::write_edz_finalize(os, kCardinality);
    os.close();
    return path;
}

size_t total_entries(const BioFMI::ResultMap& r) {
    size_t n = 0;
    for (const auto& [seq, occs] : r) n += occs.size();
    return n;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. The EDZ round-trips through Sources::load()
// ---------------------------------------------------------------------------
void test_edz_roundtrip() {
    std::cout << "Test 1: EDZ round-trip... ";
    auto path = write_edz();

    auto src = Sources::load(path, Sources::Format::EDZ);
    assert(src && "Sources::load returned null");
    assert(src->cardinality() == kCardinality);
    assert(src->num_paths() == kNumPaths);

    for (size_t i = 0; i < kCardinality; i++) {
        PathSet got = src->read_source(i);
        assert(got == kSources[i] && "EDZ entry does not round-trip");
    }
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// 2. The control: without sources the impossible match IS returned
//
// If this ever stops holding, test 3 proves nothing — it would be asserting
// that a match the index never finds is absent.
// ---------------------------------------------------------------------------
void test_cartesian_admits_impossible_path() {
    std::cout << "Test 2: CARTESIAN admits G/G/G (the control)... ";

    BioFMI idx = build_index(kEds, 3);
    // AAATTT G AAATTT G AAATTT G AAA  = 24 chars = 6 chunks of l+1
    auto r = idx.locate("AAATTTGAAATTTGAAATTTGAAA");

    assert(!idx.has_sources() && "no sources should be attached here");
    assert(total_entries(r) > 0 && "CARTESIAN must find the cross-product match");
    std::cout << "PASSED (" << total_entries(r) << " entries)\n";
}

// ---------------------------------------------------------------------------
// 3. The point of the exercise: non-transitivity
//
// G/G/G is pairwise consistent at both stitches and carried by no path.
// A pairwise implementation returns it; the running intersection rejects it.
// ---------------------------------------------------------------------------
void test_linear_rejects_non_transitive_path() {
    std::cout << "Test 3: LINEAR rejects G/G/G (non-transitive)... ";

    auto path = write_edz();
    BioFMI idx = build_index(kEds, 3);
    idx.attach_sources(path, Sources::Format::EDZ);

    assert(idx.has_sources());
    assert(idx.num_paths() == kNumPaths);

    auto r = idx.locate("AAATTTGAAATTTGAAATTTGAAA");
    assert(total_entries(r) == 0 &&
           "G/G/G is pairwise-consistent but on no path — must not be reported");
    assert(idx.count("AAATTTGAAATTTGAAATTTGAAA") == 0);
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// 4. The other half: a combination that IS on a path survives
//
// Without this, an implementation that rejects everything passes test 3.
// G/C/C intersects to {1} at every step: path 1 carries the whole match.
// ---------------------------------------------------------------------------
void test_linear_keeps_real_path() {
    std::cout << "Test 4: LINEAR keeps G/C/C (carried by path 1)... ";

    auto path = write_edz();
    BioFMI idx = build_index(kEds, 3);
    idx.attach_sources(path, Sources::Format::EDZ);

    auto r = idx.locate("AAATTTGAAATTTCAAATTTCAAA");
    assert(total_entries(r) > 0 &&
           "G/C/C lies on path 1 and must still be found");
    std::cout << "PASSED (" << total_entries(r) << " entries)\n";
}

// ---------------------------------------------------------------------------
// 5. Attaching sources never invents matches: LINEAR results are a subset
// ---------------------------------------------------------------------------
void test_linear_is_subset_of_cartesian() {
    std::cout << "Test 5: LINEAR <= CARTESIAN on every pattern... ";

    auto path = write_edz();
    const std::vector<std::string> patterns = {
        "AAATTTGAAATTTGAAATTTGAAA",
        "AAATTTGAAATTTCAAATTTCAAA",
        "AAATTTCAAATTTGAAATTTCAAA",
        "AAATTTCAAATTTCAAATTTGAAA",
        "AAATTTGAAATTTGAAATTTCAAA",
    };

    for (const auto& p : patterns) {
        BioFMI cart = build_index(kEds, 3);
        size_t n_cart = total_entries(cart.locate(p));

        BioFMI lin = build_index(kEds, 3);
        lin.attach_sources(path, Sources::Format::EDZ);
        size_t n_lin = total_entries(lin.locate(p));

        assert(n_lin <= n_cart &&
               "source-aware search must never return more than CARTESIAN");
    }
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// 6. The degenerate->global map survives save/load
//
// A loaded index has no EDS (load() never populates eds_), so the mapping has
// to come off disk. If .d2g were dropped, source sets would silently attach to
// the wrong alternatives — which is worse than failing.
// ---------------------------------------------------------------------------
void test_sources_after_save_load() {
    std::cout << "Test 6: sources attach to a reloaded index... ";

    auto path = write_edz();
    auto dir = std::filesystem::temp_directory_path() / "biofmi_test_idx_sources";
    std::filesystem::remove_all(dir);

    {
        BioFMI idx = build_index(kEds, 3);
        idx.save(dir);
    }

    BioFMI loaded(dir);
    loaded.attach_sources(path, Sources::Format::EDZ);
    assert(loaded.has_sources());

    assert(total_entries(loaded.locate("AAATTTGAAATTTGAAATTTGAAA")) == 0 &&
           "reloaded index must reject the non-transitive path too");
    assert(total_entries(loaded.locate("AAATTTGAAATTTCAAATTTCAAA")) > 0 &&
           "reloaded index must still find the real path");

    std::filesystem::remove_all(dir);
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// 7. A sources file describing a different EDS is refused
// ---------------------------------------------------------------------------
void test_cardinality_mismatch_throws() {
    std::cout << "Test 7: mismatched sources are refused... ";

    auto path = std::filesystem::temp_directory_path() / "biofmi_test_wrong.edz";
    {
        std::ofstream os(path, std::ios::binary);
        Sources::write_edz_header(os, kNumPaths);
        for (int i = 0; i < 4; i++) Sources::write_edz_entry(os, {0}, kNumPaths);
        Sources::write_edz_finalize(os, 4);   // 4 != kCardinality
    }

    BioFMI idx = build_index(kEds, 3);
    bool threw = false;
    try {
        idx.attach_sources(path, Sources::Format::EDZ);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw && "cardinality mismatch must throw, not silently mis-associate");
    assert(!idx.has_sources());

    std::filesystem::remove(path);
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// 8. The reported sample set is the one path that carries the match
//
// G/C/C intersects to exactly {1}: only path 1 carries all three. The set is a
// by-product of the search, so this checks it is actually correct and not just
// non-empty.
// ---------------------------------------------------------------------------
void test_sample_set_is_reported() {
    std::cout << "Test 8: sample set names the carrying path... ";

    auto path = write_edz();
    BioFMI idx = build_index(kEds, 3);
    idx.attach_sources(path, Sources::Format::EDZ);

    auto r = idx.locate("AAATTTGAAATTTCAAATTTCAAA");
    assert(total_entries(r) > 0);

    for (const auto& [seq, occs] : r) {
        for (const auto& occ : occs) {
            std::vector<int> ids = idx.expand_paths(occ.paths);
            assert(ids == std::vector<int>{1} &&
                   "G/C/C is carried by path 1 alone");
        }
    }
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// 9. Complement encoding is resolved, not iterated
//
// {0} is every path and {0,2} is every path except 2. A caller reading the
// vector directly would report "path 0" and "paths 0 and 2" — both wrong, and
// wrong in the direction that invents genomes.
// ---------------------------------------------------------------------------
void test_expand_paths_handles_complement() {
    std::cout << "Test 9: complement sets expand correctly... ";

    auto path = write_edz();
    BioFMI idx = build_index(kEds, 3);
    idx.attach_sources(path, Sources::Format::EDZ);

    assert((idx.expand_paths({0})     == std::vector<int>{1, 2, 3, 4}));
    assert((idx.expand_paths({0, 2})  == std::vector<int>{1, 3, 4}));
    assert((idx.expand_paths({0, 1, 4}) == std::vector<int>{2, 3}));
    assert((idx.expand_paths({2, 3})  == std::vector<int>{2, 3}));  // explicit
    assert(idx.expand_paths({}).empty());
    std::cout << "PASSED\n";
}

// ---------------------------------------------------------------------------
// 10. Without sources, no genome is named
//
// Every path set is {0} for want of any constraint. Expanding that to "all four
// genomes" would be an assertion the index cannot support, so it returns empty.
// ---------------------------------------------------------------------------
void test_no_samples_without_sources() {
    std::cout << "Test 10: CARTESIAN names no samples... ";

    BioFMI idx = build_index(kEds, 3);
    assert(!idx.has_sources());

    auto r = idx.locate("AAATTTGAAATTTGAAATTTGAAA");
    assert(total_entries(r) > 0);
    for (const auto& [seq, occs] : r)
        for (const auto& occ : occs)
            assert(idx.expand_paths(occ.paths).empty() &&
                   "without sources the index must not name genomes");
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "Source-aware (LINEAR) locate tests\n";
    std::cout << "========================================\n\n";

    try {
        test_edz_roundtrip();
        test_cartesian_admits_impossible_path();
        test_linear_rejects_non_transitive_path();
        test_linear_keeps_real_path();
        test_linear_is_subset_of_cartesian();
        test_sources_after_save_load();
        test_cardinality_mismatch_throws();
        test_sample_set_is_reported();
        test_expand_paths_handles_complement();
        test_no_samples_without_sources();

        std::cout << "\n========================================\n";
        std::cout << "ALL SOURCE-AWARE TESTS PASSED\n";
        std::cout << "========================================\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\nFAILED: " << e.what() << "\n";
        return 1;
    }
}
