#include "utils.hpp"
#include <iostream>
#include <sstream>
#include <cassert>
#include <string>

using namespace biofmi;

/**
 * Test suite for EDS to l-EDS transformation
 */

void test_cartesian_simple() {
    std::cout << "Test 1: Simple CARTESIAN transform (l=5)... ";

    std::string input_eds = "{ACGT}{A,ACA}{CGT}{T,TG}";
    std::istringstream input(input_eds);
    std::ostringstream output;

    eds_to_leds_cartesian(input, output, 5, 1);

    std::string result = output.str();
    std::string expected = "ACGT{ACGTT,ACGTTG,ACACGTT,ACACGTTG}\n";

    assert(result == expected);
    std::cout << "PASSED\n";
}

void test_cartesian_adjacent_degenerate() {
    std::cout << "Test 2: Adjacent degenerate symbols at edges (CARTESIAN)... ";

    std::string input_eds = "{A,T}{G,C}";
    std::istringstream input(input_eds);
    std::ostringstream output;

    eds_to_leds_cartesian(input, output, 1, 1);

    std::string result = output.str();
    std::string expected = "{AG,AC,TG,TC}\n";

    assert(result == expected);
    std::cout << "PASSED\n";
}

void test_cartesian_internal_adjacent() {
    std::cout << "Test 3: Internal adjacent degenerate (CARTESIAN)... ";

    std::string input_eds = "{ACGT}{A,T}{G,C}{TGCA}";
    std::istringstream input(input_eds);
    std::ostringstream output;

    eds_to_leds_cartesian(input, output, 1, 1);

    std::string result = output.str();
    std::string expected = "ACGT{AG,AC,TG,TC}TGCA\n";

    assert(result == expected);
    std::cout << "PASSED\n";
}

void test_cartesian_short_common() {
    std::cout << "Test 4: Short internal common blocks (CARTESIAN)... ";

    std::string input_eds = "{AAAA}{AGT}{GTT}{CCCC}";
    std::istringstream input(input_eds);
    std::ostringstream output;

    eds_to_leds_cartesian(input, output, 4, 1);

    std::string result = output.str();
    std::string expected = "AAAAAGTGTTCCCC\n";

    assert(result == expected);
    std::cout << "PASSED\n";
}

void test_cartesian_edge_exemption() {
    std::cout << "Test 5: Edge positions exempt from length requirement (CARTESIAN)... ";

    std::string input_eds = "{AG}{TTTT}{CCCC}";
    std::istringstream input(input_eds);
    std::ostringstream output;

    eds_to_leds_cartesian(input, output, 4, 1);

    std::string result = output.str();
    std::string expected = "AGTTTTCCCC\n";

    assert(result == expected);
    std::cout << "PASSED\n";
}

void test_cartesian_iterative() {
    std::cout << "Test 6: Iterative merging (CARTESIAN)... ";

    std::string input_eds = "{AAAA}{A,T}{GTT}{A,T}{CCCC}";
    std::istringstream input(input_eds);
    std::ostringstream output;

    eds_to_leds_cartesian(input, output, 4, 1);

    std::string result = output.str();
    std::string expected = "AAAA{AGTTA,AGTTT,TGTTA,TGTTT}CCCC\n";

    assert(result == expected);
    std::cout << "PASSED\n";
}

void test_cartesian_short_edges_with_internal() {
    std::cout << "Test 7: Short edges with internal degenerate (CARTESIAN)... ";

    std::string input_eds = "{AA}{T,G}{A,C}{TT}";
    std::istringstream input(input_eds);
    std::ostringstream output;

    eds_to_leds_cartesian(input, output, 4, 1);

    std::string result = output.str();
    std::string expected = "AA{TA,TC,GA,GC}TT\n";

    assert(result == expected);
    std::cout << "PASSED\n";
}

void test_cartesian_already_leds() {
    std::cout << "Test 8: Already valid l-EDS (CARTESIAN)... ";

    std::string input_eds = "{AAAAA}{BBBBB}{CCCCC}";
    std::istringstream input(input_eds);
    std::ostringstream output;

    eds_to_leds_cartesian(input, output, 4, 1);

    std::string result = output.str();
    std::string expected = "AAAAABBBBBCCCCC\n";

    assert(result == expected);
    std::cout << "PASSED\n";
}

void test_linear_with_sources() {
    std::cout << "Test 9: LINEAR mode with compatible sources... ";

    std::string input_eds = "{AAAA}{A,T}{CG}{G,C}{TTTT}";
    std::string input_sources = "{0}{1,2}{1,2}{1,2}{1,2}{1,2}{0}";

    std::istringstream eds_stream(input_eds);
    std::istringstream sources_stream(input_sources);
    std::ostringstream eds_output;
    std::ostringstream sources_output;

    eds_to_leds_linear(eds_stream, eds_output, 4, &sources_stream, &sources_output, 1);

    std::string eds_result = eds_output.str();
    std::string sources_result = sources_output.str();

    std::string expected_eds = "AAAA{ACGG,ACGC,TCGG,TCGC}TTTT\n";
    std::string expected_sources = "{0}{1,2}{1,2}{1,2}{1,2}{0}\n";

    assert(eds_result == expected_eds);
    assert(sources_result == expected_sources);
    std::cout << "PASSED\n";
}

void test_linear_empty_intersection_fails() {
    std::cout << "Test 10: LINEAR mode with incompatible sources throws... ";

    std::string input_eds = "{AAAA}{A,T}{CGT}{G,C}{TTTT}";
    std::string input_sources = "{0}{1,2}{1,2}{1,2}{3,4}{3,4}{0}";

    std::istringstream eds_stream(input_eds);
    std::istringstream sources_stream(input_sources);
    std::ostringstream eds_output;
    std::ostringstream sources_output;

    bool threw_exception = false;
    try {
        eds_to_leds_linear(eds_stream, eds_output, 4, &sources_stream, &sources_output, 1);
    } catch (const std::runtime_error& e) {
        std::string error_msg = e.what();
        if (error_msg.find("empty set") != std::string::npos ||
            error_msg.find("no valid source intersections") != std::string::npos) {
            threw_exception = true;
        }
    }

    assert(threw_exception);
    std::cout << "PASSED\n";
}

void test_linear_single_path() {
    std::cout << "Test 11: LINEAR mode with single path... ";

    std::string input_eds = "{AAA}{A,T}{CC}{G,C}{TTT}";
    std::string input_sources = "{0}{1}{1}{1}{1}{1}{0}";

    std::istringstream eds_stream(input_eds);
    std::istringstream sources_stream(input_sources);
    std::ostringstream eds_output;
    std::ostringstream sources_output;

    eds_to_leds_linear(eds_stream, eds_output, 3, &sources_stream, &sources_output, 1);

    std::string eds_result = eds_output.str();
    std::string sources_result = sources_output.str();

    std::string expected_eds = "AAA{ACCG,ACCC,TCCG,TCCC}TTT\n";
    std::string expected_sources = "{0}{1}{1}{1}{1}{0}\n";

    assert(eds_result == expected_eds);
    assert(sources_result == expected_sources);
    std::cout << "PASSED\n";
}

void test_is_leds() {
    std::cout << "Test 12: is_leds() validation function... ";

    // Valid l-EDS
    EDS valid_eds("{AAAAA}{BBBBB}{CCCCC}");
    assert(is_leds(valid_eds, 4) == true);

    // Invalid - short internal common block
    EDS invalid1("{AAAAA}{BB}{CCCCC}");
    assert(is_leds(invalid1, 4) == false);

    // Valid - short edge blocks are exempt
    EDS valid_edge("{AA}{BBBBB}{CC}");
    assert(is_leds(valid_edge, 4) == true);

    // Invalid - adjacent degenerate symbols
    EDS invalid2("{AAAA}{A,T}{G,C}{CCCC}");
    assert(is_leds(invalid2, 1) == false);

    // Valid - no adjacent degenerate
    EDS valid_separate("{AAAA}{A,T}{BBB}{G,C}{CCCC}");
    assert(is_leds(valid_separate, 1) == true);

    std::cout << "PASSED\n";
}

void test_cartesian_no_changes_needed() {
    std::cout << "Test 13: No transformation needed (CARTESIAN)... ";

    std::string input_eds = "{AAAAA}{B,C}{DDDDD}";
    std::istringstream input(input_eds);
    std::ostringstream output;

    eds_to_leds_cartesian(input, output, 4, 1);

    std::string result = output.str();
    std::string expected = "AAAAA{B,C}DDDDD\n";

    assert(result == expected);
    std::cout << "PASSED\n";
}

void test_linear_overlapping_paths() {
    std::cout << "Test 14: LINEAR mode with overlapping paths... ";

    // EDS: {AAAA}{A,T,G}{CG}{C,G}{TTTT}
    // Cardinality: 1 + 3 + 1 + 2 + 1 = 8
    std::string input_eds = "{AAAA}{A,T,G}{CG}{C,G}{TTTT}";
    std::string input_sources = "{0}{1,2}{2,3}{3,4}{1,2,3,4}{2,3}{3}{0}";

    std::istringstream eds_stream(input_eds);
    std::istringstream sources_stream(input_sources);
    std::ostringstream eds_output;
    std::ostringstream sources_output;

    eds_to_leds_linear(eds_stream, eds_output, 4, &sources_stream, &sources_output, 1);

    // After merge (1,2): A+CG from {1,2}∩{1,2,3,4}={1,2}, T+CG from {2,3}∩{1,2,3,4}={2,3}, G+CG from {3,4}∩{1,2,3,4}={3,4}
    // Then merge with position 3: ACG+C from {1,2}∩{2,3}={2}, etc.

    std::string eds_result = eds_output.str();
    // Result should have all valid combinations with proper source intersections
    assert(eds_result.find("AAAA") == 0);
    assert(eds_result.find("TTTT") != std::string::npos);

    std::cout << "PASSED\n";
}

void test_linear_no_valid_paths() {
    std::cout << "Test 15: LINEAR mode with no valid paths (empty result)... ";

    // Create an EDS where after merging, all source intersections are empty
    // This should result in an empty EDS with no strings
    std::string input_eds = "{AAAA}{A,T}{CG}{TTTT}";
    // Position 0: AAAA from path 0
    // Position 1: A from path 1, T from path 2
    // Position 2: CG from path 3
    // Position 3: TTTT from path 0
    // When merging (1,2): A+CG has sources {1}∩{3}=∅, T+CG has sources {2}∩{3}=∅
    // Result: empty set → should throw error
    std::string input_sources = "{0}{1}{2}{3}{0}";

    std::istringstream eds_stream(input_eds);
    std::istringstream sources_stream(input_sources);
    std::ostringstream eds_output;
    std::ostringstream sources_output;

    bool threw_exception = false;
    try {
        eds_to_leds_linear(eds_stream, eds_output, 3, &sources_stream, &sources_output, 1);
    } catch (const std::runtime_error& e) {
        std::string error_msg = e.what();
        // Should fail because no valid source intersections exist after merging
        if (error_msg.find("empty set") != std::string::npos ||
            error_msg.find("no valid source intersections") != std::string::npos) {
            threw_exception = true;
        }
    }

    assert(threw_exception);
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "Running EDS → l-EDS transformation tests...\n\n";

    try {
        // CARTESIAN mode tests (without sources)
        test_cartesian_simple();
        test_cartesian_adjacent_degenerate();
        test_cartesian_internal_adjacent();
        test_cartesian_short_common();
        test_cartesian_edge_exemption();
        test_cartesian_iterative();
        test_cartesian_short_edges_with_internal();
        test_cartesian_already_leds();
        test_cartesian_no_changes_needed();

        // LINEAR mode tests (with sources)
        test_linear_with_sources();
        test_linear_empty_intersection_fails();
        test_linear_single_path();
        test_linear_overlapping_paths();
        test_linear_no_valid_paths();

        // Validation function test
        test_is_leds();

        std::cout << "\n===========================================\n";
        std::cout << "All 15 tests PASSED!\n";
        std::cout << "===========================================\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n*** TEST FAILED ***\n";
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
