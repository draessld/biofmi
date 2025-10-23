// EDS parsing tests
#include "../../src/cpp/lib/eds.hpp"
#include <sstream>
#include <iostream>
#include <cassert>

void test_simple_eds() {
    std::cout << "Test 1: Simple EDS parsing... ";
    std::stringstream ss("{ACGT}{A,ACA}{CGT}{T,TG}");

    biofmi::EDS eds(ss);

    assert(eds.length() == 4);        // 4 positions
    assert(eds.cardinality() == 6);   // 6 strings total
    assert(eds.size() == 14);         // 14 characters total
    assert(!eds.empty());

    const auto& sets = eds.get_sets();
    const auto& is_deg = eds.get_is_degenerate();

    // Position 0: {ACGT} - regular
    assert(sets[0].size() == 1);
    assert(sets[0][0] == "ACGT");
    assert(!is_deg[0]);

    // Position 1: {A,ACA} - degenerate
    assert(sets[1].size() == 2);
    assert(sets[1][0] == "A");
    assert(sets[1][1] == "ACA");
    assert(is_deg[1]);

    // Position 2: {CGT} - regular
    assert(sets[2].size() == 1);
    assert(sets[2][0] == "CGT");
    assert(!is_deg[2]);

    // Position 3: {T,TG} - degenerate
    assert(sets[3].size() == 2);
    assert(sets[3][0] == "T");
    assert(sets[3][1] == "TG");
    assert(is_deg[3]);

    std::cout << "PASSED\n";
}

void test_empty_strings() {
    std::cout << "Test 2: EDS with empty strings... ";
    std::stringstream ss("{AC}{,A,T}{GT}");

    biofmi::EDS eds(ss);

    assert(eds.length() == 3);        // 3 positions
    assert(eds.cardinality() == 5);   // 5 strings total (including empty)
    assert(!eds.empty());

    const auto& sets = eds.get_sets();
    const auto& is_deg = eds.get_is_degenerate();

    // Position 0: {AC} - regular
    assert(sets[0].size() == 1);
    assert(sets[0][0] == "AC");
    assert(!is_deg[0]);

    // Position 1: {,A,T} - degenerate with empty string
    assert(sets[1].size() == 3);
    assert(sets[1][0] == "");         // Empty string
    assert(sets[1][1] == "A");
    assert(sets[1][2] == "T");
    assert(is_deg[1]);

    // Position 2: {GT} - regular
    assert(sets[2].size() == 1);
    assert(sets[2][0] == "GT");
    assert(!is_deg[2]);

    std::cout << "PASSED\n";
}

void test_single_position() {
    std::cout << "Test 3: Single position EDS... ";
    std::stringstream ss("{ACGT}");

    biofmi::EDS eds(ss);

    assert(eds.length() == 1);
    assert(eds.cardinality() == 1);
    assert(eds.size() == 4);
    assert(!eds.empty());

    const auto& sets = eds.get_sets();
    assert(sets[0].size() == 1);
    assert(sets[0][0] == "ACGT");

    std::cout << "PASSED\n";
}

void test_all_degenerate() {
    std::cout << "Test 4: All degenerate positions... ";
    std::stringstream ss("{A,C}{G,T}{A,C,G,T}");

    biofmi::EDS eds(ss);

    assert(eds.length() == 3);
    assert(eds.cardinality() == 8);   // 2 + 2 + 4 = 8
    assert(!eds.empty());

    const auto& is_deg = eds.get_is_degenerate();
    assert(is_deg[0]);
    assert(is_deg[1]);
    assert(is_deg[2]);

    std::cout << "PASSED\n";
}

void test_whitespace_handling() {
    std::cout << "Test 5: Whitespace handling... ";
    std::stringstream ss("{ ACGT } { A , ACA } { CGT }");

    biofmi::EDS eds(ss);

    assert(eds.length() == 3);
    assert(eds.cardinality() == 4);

    const auto& sets = eds.get_sets();
    assert(sets[0][0] == "ACGT");
    assert(sets[1][0] == "A");
    assert(sets[1][1] == "ACA");

    std::cout << "PASSED\n";
}

void test_empty_input() {
    std::cout << "Test 6: Empty input... ";
    std::stringstream ss("");

    biofmi::EDS eds(ss);

    assert(eds.empty());
    assert(eds.length() == 0);
    assert(eds.cardinality() == 0);
    assert(eds.size() == 0);

    std::cout << "PASSED\n";
}

void test_invalid_format_missing_open() {
    std::cout << "Test 7: Invalid format (missing '{')... ";
    std::stringstream ss("ACGT}");

    bool caught = false;
    try {
        biofmi::EDS eds(ss);
    } catch (const std::runtime_error& e) {
        caught = true;
    }

    assert(caught);
    std::cout << "PASSED\n";
}

void test_invalid_format_missing_close() {
    std::cout << "Test 8: Invalid format (missing '}')... ";
    std::stringstream ss("{ACGT");

    bool caught = false;
    try {
        biofmi::EDS eds(ss);
    } catch (const std::runtime_error& e) {
        caught = true;
    }

    assert(caught);
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "Running EDS parsing tests...\n\n";

    try {
        test_simple_eds();
        test_empty_strings();
        test_single_position();
        test_all_degenerate();
        test_whitespace_handling();
        test_empty_input();
        test_invalid_format_missing_open();
        test_invalid_format_missing_close();

        std::cout << "\n✓ All tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed: " << e.what() << "\n";
        return 1;
    }
}
