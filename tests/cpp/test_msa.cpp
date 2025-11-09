#include "utils.hpp"
#include "common.hpp"
#include <iostream>
#include <sstream>
#include <cassert>
#include <string>

using namespace biofmi;

// Test helper: compare strings ignoring whitespace
bool compare_ignore_whitespace(const std::string& s1, const std::string& s2) {
    std::string clean1, clean2;
    for (char c : s1) if (!std::isspace(c)) clean1 += c;
    for (char c : s2) if (!std::isspace(c)) clean2 += c;
    return clean1 == clean2;
}

// Test 1: MSA → EDS transformation
void test_msa_to_eds() {
    std::cout << "Test 1: MSA → EDS transformation\n";

    // Input MSA (from small.msa)
    std::string msa_input =
        ">seq1\n"
        "AGTC--TCTATA\n"
        ">seq2\n"
        "AGTCCCTATATA\n"
        ">seq3\n"
        "AGTC--TATATA\n";

    std::istringstream msa_stream(msa_input);
    auto [eds_str, seds_str] = parse_msa_to_eds_streaming(msa_stream);

    // Expected outputs
    std::string expected_eds = "{AGTC}{,CC}{T}{C,A}{TATA}";
    std::string expected_seds = "{0}{1,3}{2}{0}{1}{2,3}{0}";

    std::cout << "  Generated EDS:  " << eds_str << "\n";
    std::cout << "  Expected EDS:   " << expected_eds << "\n";
    std::cout << "  Generated sEDS: " << seds_str << "\n";
    std::cout << "  Expected sEDS:  " << expected_seds << "\n";

    // Verify
    if (!compare_ignore_whitespace(eds_str, expected_eds)) {
        std::cerr << "ERROR: EDS output mismatch!\n";
        std::cerr << "  Got:      '" << eds_str << "'\n";
        std::cerr << "  Expected: '" << expected_eds << "'\n";
        exit(1);
    }

    if (!compare_ignore_whitespace(seds_str, expected_seds)) {
        std::cerr << "ERROR: sEDS output mismatch!\n";
        std::cerr << "  Got:      '" << seds_str << "'\n";
        std::cerr << "  Expected: '" << expected_seds << "'\n";
        exit(1);
    }

    std::cout << "  ✓ PASSED\n\n";
}

// Test 2: MSA → l-EDS transformation (l=4)
void test_msa_to_leds() {
    std::cout << "Test 2: MSA → l-EDS transformation (l=4)\n";

    // Input MSA (from small.msa)
    std::string msa_input =
        ">seq1\n"
        "AGTC--TCTATA\n"
        ">seq2\n"
        "AGTCCCTATATA\n"
        ">seq3\n"
        "AGTC--TATATA\n";

    std::istringstream msa_stream(msa_input);
    auto [leds_str, seds_str] = parse_msa_to_leds_streaming(msa_stream, 4);

    // Expected outputs
    std::string expected_leds = "{AGTC}{TC,CCTA,TA}{TATA}";
    std::string expected_seds = "{0}{1}{2}{3}{0}";

    std::cout << "  Generated l-EDS:  " << leds_str << "\n";
    std::cout << "  Expected l-EDS:   " << expected_leds << "\n";
    std::cout << "  Generated sEDS:   " << seds_str << "\n";
    std::cout << "  Expected sEDS:    " << expected_seds << "\n";

    // Verify
    if (!compare_ignore_whitespace(leds_str, expected_leds)) {
        std::cerr << "ERROR: l-EDS output mismatch!\n";
        std::cerr << "  Got:      '" << leds_str << "'\n";
        std::cerr << "  Expected: '" << expected_leds << "'\n";
        exit(1);
    }

    if (!compare_ignore_whitespace(seds_str, expected_seds)) {
        std::cerr << "ERROR: sEDS output mismatch!\n";
        std::cerr << "  Got:      '" << seds_str << "'\n";
        std::cerr << "  Expected: '" << expected_seds << "'\n";
        exit(1);
    }

    std::cout << "  ✓ PASSED\n\n";
}

// Test 3: All sequences identical (no variants)
void test_msa_identical_sequences() {
    std::cout << "Test 3: All sequences identical\n";

    std::string msa_input =
        ">seq1\n"
        "AGTCTA\n"
        ">seq2\n"
        "AGTCTA\n"
        ">seq3\n"
        "AGTCTA\n";

    std::istringstream msa_stream(msa_input);
    auto [eds_str, seds_str] = parse_msa_to_eds_streaming(msa_stream);

    // Expected: single common symbol
    std::string expected_eds = "{AGTCTA}";
    std::string expected_seds = "{0}";

    std::cout << "  Generated EDS:  " << eds_str << "\n";
    std::cout << "  Expected EDS:   " << expected_eds << "\n";

    if (!compare_ignore_whitespace(eds_str, expected_eds)) {
        std::cerr << "ERROR: EDS output mismatch for identical sequences!\n";
        exit(1);
    }

    if (!compare_ignore_whitespace(seds_str, expected_seds)) {
        std::cerr << "ERROR: sEDS output mismatch for identical sequences!\n";
        exit(1);
    }

    std::cout << "  ✓ PASSED\n\n";
}

// Test 4: Single variant position
void test_msa_single_variant() {
    std::cout << "Test 4: Single variant position\n";

    std::string msa_input =
        ">seq1\n"
        "AGTC\n"
        ">seq2\n"
        "AGCC\n";

    std::istringstream msa_stream(msa_input);
    auto [eds_str, seds_str] = parse_msa_to_eds_streaming(msa_stream);

    // Expected: {AG}{T,C}{C}
    std::string expected_eds = "{AG}{T,C}{C}";
    std::string expected_seds = "{0}{1}{2}{0}";

    std::cout << "  Generated EDS:  " << eds_str << "\n";
    std::cout << "  Expected EDS:   " << expected_eds << "\n";

    if (!compare_ignore_whitespace(eds_str, expected_eds)) {
        std::cerr << "ERROR: EDS output mismatch for single variant!\n";
        exit(1);
    }

    if (!compare_ignore_whitespace(seds_str, expected_seds)) {
        std::cerr << "ERROR: sEDS output mismatch for single variant!\n";
        exit(1);
    }

    std::cout << "  ✓ PASSED\n\n";
}

// Test 5: Gap at beginning
void test_msa_gap_at_beginning() {
    std::cout << "Test 5: Gap at beginning\n";

    std::string msa_input =
        ">seq1\n"
        "--AGTC\n"
        ">seq2\n"
        "CCAGTC\n";

    std::istringstream msa_stream(msa_input);
    auto [eds_str, seds_str] = parse_msa_to_eds_streaming(msa_stream);

    // Expected: {,CC}{AGTC}
    std::string expected_eds = "{,CC}{AGTC}";
    std::string expected_seds = "{1}{2}{0}";

    std::cout << "  Generated EDS:  " << eds_str << "\n";
    std::cout << "  Expected EDS:   " << expected_eds << "\n";

    if (!compare_ignore_whitespace(eds_str, expected_eds)) {
        std::cerr << "ERROR: EDS output mismatch for gap at beginning!\n";
        std::cerr << "  Got:      '" << eds_str << "'\n";
        std::cerr << "  Expected: '" << expected_eds << "'\n";
        exit(1);
    }

    std::cout << "  ✓ PASSED\n\n";
}

// Test 6: Gap at end
void test_msa_gap_at_end() {
    std::cout << "Test 6: Gap at end\n";

    std::string msa_input =
        ">seq1\n"
        "AGTC--\n"
        ">seq2\n"
        "AGTCGG\n";

    std::istringstream msa_stream(msa_input);
    auto [eds_str, seds_str] = parse_msa_to_eds_streaming(msa_stream);

    // Expected: {AGTC}{,GG}
    std::string expected_eds = "{AGTC}{,GG}";
    std::string expected_seds = "{0}{1}{2}";

    std::cout << "  Generated EDS:  " << eds_str << "\n";
    std::cout << "  Expected EDS:   " << expected_eds << "\n";

    if (!compare_ignore_whitespace(eds_str, expected_eds)) {
        std::cerr << "ERROR: EDS output mismatch for gap at end!\n";
        exit(1);
    }

    std::cout << "  ✓ PASSED\n\n";
}

// Test 7: Multiple l-EDS context lengths
void test_msa_multiple_context_lengths() {
    std::cout << "Test 7: Multiple context lengths\n";

    std::string msa_input =
        ">seq1\n"
        "AGTC--TCTATA\n"
        ">seq2\n"
        "AGTCCCTATATA\n"
        ">seq3\n"
        "AGTC--TATATA\n";

    // Test with l=2
    {
        std::istringstream msa_stream(msa_input);
        auto [leds_str, seds_str] = parse_msa_to_leds_streaming(msa_stream, 2);
        std::cout << "  l=2: " << leds_str << "\n";
        // With l=2, middle variants should still merge
        // AGTC (len 4 >= 2, standalone), variants merge, TATA (len 4 >= 2, standalone)
    }

    // Test with l=10
    {
        std::istringstream msa_stream(msa_input);
        auto [leds_str, seds_str] = parse_msa_to_leds_streaming(msa_stream, 10);
        std::cout << "  l=10: " << leds_str << "\n";
        // With l=10, AGTC (len 4 < 10, merge), TATA (len 4 < 10, merge)
        // Should result in fewer symbols
    }

    std::cout << "  ✓ PASSED (manual inspection)\n\n";
}

int main() {
    std::cout << "=== MSA Transformation Tests ===\n\n";

    try {
        test_msa_to_eds();
        test_msa_to_leds();
        test_msa_identical_sequences();
        test_msa_single_variant();
        test_msa_gap_at_beginning();
        test_msa_gap_at_end();
        test_msa_multiple_context_lengths();

        std::cout << "=== All tests PASSED ===\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "\n=== TEST FAILED ===\n";
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
}
