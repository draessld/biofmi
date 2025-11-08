#include "../../src/cpp/lib/eds.hpp"
#include <iostream>
#include <cassert>
#include <sstream>

using namespace biofmi;

// Test counter
int test_num = 0;

void test(const std::string& description) {
    test_num++;
    std::cout << "Test " << test_num << ": " << description << "... ";
}

void pass() {
    std::cout << "PASSED\n";
}

// ===== TESTS WITHOUT SOURCES (CARTESIAN MERGE) =====

void test_merge_two_degenerate() {
    test("Merge two degenerate symbols {G,C} + {T}");

    EDS eds("{G,C}{T}");
    EDS merged = eds.merge_adjacent(0, 1);

    // Verify structure
    assert(merged.length() == 1);  // n = 1 (merged into single position)
    assert(merged.cardinality() == 2);  // m = 2 (GT, CT)
    assert(merged.size() == 3);  // N = 3 (G+T=2, C+T=2, total 3 unique chars... wait)

    // Check sets
    const auto& sets = merged.get_sets();
    assert(sets.size() == 1);
    assert(sets[0].size() == 2);
    assert(sets[0][0] == "GT");
    assert(sets[0][1] == "CT");

    // Check metadata
    assert(merged.get_is_degenerate()[0] == true);  // 2 alternatives = degenerate

    pass();
}

void test_merge_degenerate_nondegenerate() {
    test("Merge degenerate + non-degenerate {G,C} + {T}");

    EDS eds("{G,C}{T}");
    EDS merged = eds.merge_adjacent(0, 1);

    assert(merged.length() == 1);
    assert(merged.cardinality() == 2);

    const auto& sets = merged.get_sets();
    assert(sets[0][0] == "GT");
    assert(sets[0][1] == "CT");

    pass();
}

void test_merge_nondegenerate_degenerate() {
    test("Merge non-degenerate + degenerate {T} + {A,C,G}");

    EDS eds("{T}{A,C,G}");
    EDS merged = eds.merge_adjacent(0, 1);

    assert(merged.length() == 1);
    assert(merged.cardinality() == 3);

    const auto& sets = merged.get_sets();
    assert(sets.size() == 1);
    assert(sets[0].size() == 3);
    assert(sets[0][0] == "TA");
    assert(sets[0][1] == "TC");
    assert(sets[0][2] == "TG");

    pass();
}

void test_merge_three_step() {
    test("Multiple merges: {G,C} + {T} + {A,C} = {GTA,GTC,CTA,CTC}");

    EDS eds("{G,C}{T}{A,C}");
    EDS step1 = eds.merge_adjacent(0, 1);  // {GT,CT}{A,C}
    EDS step2 = step1.merge_adjacent(0, 1);  // {GTA,GTC,CTA,CTC}

    assert(step2.length() == 1);
    assert(step2.cardinality() == 4);

    const auto& sets = step2.get_sets();
    assert(sets[0][0] == "GTA");
    assert(sets[0][1] == "GTC");
    assert(sets[0][2] == "CTA");
    assert(sets[0][3] == "CTC");

    pass();
}

void test_merge_with_empty_strings() {
    test("Merge with empty strings {,A} + {T}");

    EDS eds("{,A}{T}");
    EDS merged = eds.merge_adjacent(0, 1);

    assert(merged.cardinality() == 2);

    const auto& sets = merged.get_sets();
    assert(sets[0][0] == "T");   // "" + "T" = "T"
    assert(sets[0][1] == "AT");  // "A" + "T" = "AT"

    pass();
}

void test_merge_metadata_update() {
    test("Verify metadata updates correctly");

    EDS eds("{ACGT}{G,C}{T}");
    EDS merged = eds.merge_adjacent(1, 2);

    // Check dimensions
    assert(merged.length() == 2);  // n: 3 → 2
    assert(merged.cardinality() == 3);  // m: 1 + 2 + 1 = 4 → 1 + 2 = 3

    // Check is_degenerate
    const auto& is_deg = merged.get_is_degenerate();
    assert(is_deg.size() == 2);
    assert(is_deg[0] == false);  // {ACGT} non-degenerate
    assert(is_deg[1] == true);   // {GT,CT} degenerate

    // Check sets
    const auto& sets = merged.get_sets();
    assert(sets[0].size() == 1);
    assert(sets[0][0] == "ACGT");
    assert(sets[1].size() == 2);
    assert(sets[1][0] == "GT");
    assert(sets[1][1] == "CT");

    pass();
}

void test_merge_statistics_recalc() {
    test("Verify statistics recalculated");

    EDS eds("{AC}{G,C}{T}");
    EDS merged = eds.merge_adjacent(1, 2);

    auto stats = merged.get_statistics();

    // Should have 1 non-degenerate (AC) and 1 degenerate (GT,CT)
    assert(stats.num_degenerate_symbols == 1);
    assert(stats.min_context_length == 2);  // "AC"
    assert(stats.max_context_length == 2);

    pass();
}

// ===== TESTS WITH SOURCES (LINEAR MERGE) =====

void test_merge_with_valid_intersections() {
    test("Merge with sources - valid intersections");

    // {G,C}{T} with sources
    // String 0 (G) has paths {1,2}, String 1 (C) has paths {2,3}, String 2 (T) has paths {2}
    // Expected: GT with {1,2}∩{2}={2}, CT with {2,3}∩{2}={2}
    EDS eds(std::string("{G,C}{T}"), std::string("{1,2}{2,3}{2}"));
    EDS merged = eds.merge_adjacent(0, 1);

    assert(merged.cardinality() == 2);
    assert(merged.has_sources());

    const auto& sources = merged.get_sources();
    assert(sources.size() == 2);
    assert(sources[0].size() == 1);
    assert(sources[0].count(2) == 1);  // GT has {2}
    assert(sources[1].size() == 1);
    assert(sources[1].count(2) == 1);  // CT has {2}

    pass();
}

void test_merge_with_empty_intersection_filtered() {
    test("Merge with sources - filter empty intersections");

    // {A,B}{C,D} with sources
    // String 0 (A): {1}, String 1 (B): {2}, String 2 (C): {1}, String 3 (D): {3}
    // Valid combinations: AC ({1}∩{1}={1}), BC ({2}∩{1}={}), AD ({1}∩{3}={}), BD ({2}∩{3}={})
    // Only AC should survive
    EDS eds(std::string("{A,B}{C,D}"), std::string("{1}{2}{1}{3}"));
    EDS merged = eds.merge_adjacent(0, 1);

    assert(merged.cardinality() == 1);  // Only AC

    const auto& sets = merged.get_sets();
    assert(sets[0].size() == 1);
    assert(sets[0][0] == "AC");

    const auto& sources = merged.get_sources();
    assert(sources[0].size() == 1);
    assert(sources[0].count(1) == 1);

    pass();
}

void test_merge_with_universal_marker() {
    test("Merge with universal marker {0}");

    // {A,B}{C} with sources
    // String 0 (A): {0} (all paths), String 1 (B): {2}, String 2 (C): {1}
    // Expected: AC with {0}∩{1}={1}, BC with {2}∩{1}={}
    EDS eds(std::string("{A,B}{C}"), std::string("{0}{2}{1}"));
    EDS merged = eds.merge_adjacent(0, 1);

    assert(merged.cardinality() == 1);  // Only AC

    const auto& sets = merged.get_sets();
    assert(sets[0][0] == "AC");

    const auto& sources = merged.get_sources();
    assert(sources[0].count(1) == 1);  // {0} ∩ {1} = {1}

    pass();
}

void test_merge_universal_with_universal() {
    test("Merge {0} with {0}");

    // Both have universal marker
    // String 0 (A): {0}, String 1 (B): {0}
    EDS eds(std::string("{A}{B}"), std::string("{0}{0}"));
    EDS merged = eds.merge_adjacent(0, 1);

    const auto& sources = merged.get_sources();
    assert(sources[0].size() == 1);
    assert(sources[0].count(0) == 1);  // {0} ∩ {0} = {0}

    pass();
}

void test_merge_all_empty_intersections_throws() {
    test("Merge with all empty intersections throws");

    // {A,B}{C,D} where no combination has valid intersection
    // String 0 (A): {1}, String 1 (B): {2}, String 2 (C): {3}, String 3 (D): {4}
    // All intersections empty
    EDS eds(std::string("{A,B}{C,D}"), std::string("{1}{2}{3}{4}"));

    bool threw = false;
    try {
        EDS merged = eds.merge_adjacent(0, 1);
    } catch (const std::runtime_error& e) {
        threw = true;
        std::string msg(e.what());
        assert(msg.find("empty set") != std::string::npos);
    }
    assert(threw);

    pass();
}

void test_merge_source_statistics() {
    test("Verify source statistics recalculated");

    // String 0 (A): {1,2}, String 1 (B): {3}, String 2 (C): {1}
    EDS eds(std::string("{A,B}{C}"), std::string("{1,2}{3}{1}"));
    EDS merged = eds.merge_adjacent(0, 1);

    auto stats = merged.get_statistics();
    assert(stats.num_paths >= 1);  // At least path 1 remains

    pass();
}

// ===== EDGE CASES =====

void test_merge_non_adjacent_throws() {
    test("Merge non-adjacent positions throws");

    EDS eds("{A}{B}{C}");

    bool threw = false;
    try {
        EDS merged = eds.merge_adjacent(0, 2);  // Not adjacent
    } catch (const std::invalid_argument& e) {
        threw = true;
        std::string msg(e.what());
        assert(msg.find("adjacent") != std::string::npos);
    }
    assert(threw);

    pass();
}

void test_merge_out_of_bounds_throws() {
    test("Merge out of bounds throws");

    EDS eds("{A}{B}");

    bool threw = false;
    try {
        EDS merged = eds.merge_adjacent(1, 2);  // pos2 >= n
    } catch (const std::out_of_range& e) {
        threw = true;
    }
    assert(threw);

    pass();
}

void test_merge_at_start() {
    test("Merge at start (positions 0,1)");

    EDS eds("{A}{B}{C}");
    EDS merged = eds.merge_adjacent(0, 1);

    assert(merged.length() == 2);

    const auto& sets = merged.get_sets();
    assert(sets[0][0] == "AB");
    assert(sets[1][0] == "C");

    pass();
}

void test_merge_at_end() {
    test("Merge at end (last two positions)");

    EDS eds("{A}{B}{C}");
    EDS merged = eds.merge_adjacent(1, 2);

    assert(merged.length() == 2);

    const auto& sets = merged.get_sets();
    assert(sets[0][0] == "A");
    assert(sets[1][0] == "BC");

    pass();
}

void test_immutability() {
    test("Original EDS unchanged after merge (immutability)");

    EDS original("{A}{B}{C}");
    size_t orig_n = original.length();
    size_t orig_m = original.cardinality();

    EDS merged = original.merge_adjacent(0, 1);

    // Original unchanged
    assert(original.length() == orig_n);
    assert(original.cardinality() == orig_m);

    // Merged is different
    assert(merged.length() == orig_n - 1);

    pass();
}

void test_merge_resulting_in_nondegenerate() {
    test("Merge resulting in non-degenerate (single alternative)");

    EDS eds("{A}{B}");
    EDS merged = eds.merge_adjacent(0, 1);

    assert(merged.cardinality() == 1);

    const auto& is_deg = merged.get_is_degenerate();
    assert(is_deg[0] == false);  // Single alternative = non-degenerate

    pass();
}

// ===== MAIN =====

int main() {
    std::cout << "Running EDS merge_adjacent() tests...\n\n";

    // Without sources (CARTESIAN)
    test_merge_two_degenerate();
    test_merge_degenerate_nondegenerate();
    test_merge_nondegenerate_degenerate();
    test_merge_three_step();
    test_merge_with_empty_strings();
    test_merge_metadata_update();
    test_merge_statistics_recalc();

    // With sources (LINEAR)
    test_merge_with_valid_intersections();
    test_merge_with_empty_intersection_filtered();
    test_merge_with_universal_marker();
    test_merge_universal_with_universal();
    test_merge_all_empty_intersections_throws();
    test_merge_source_statistics();

    // Edge cases
    test_merge_non_adjacent_throws();
    test_merge_out_of_bounds_throws();
    test_merge_at_start();
    test_merge_at_end();
    test_immutability();
    test_merge_resulting_in_nondegenerate();

    std::cout << "\n===========================================\n";
    std::cout << "All " << test_num << " tests PASSED!\n";
    std::cout << "===========================================\n";

    return 0;
}
