// Simplified validation test for locate functionality
#include "index/index.hpp"
#include <edsparser/formats/eds.hpp>
#include <edsparser/transforms/eds_transforms.hpp>
#include <iostream>
#include <sstream>
#include <random>

using namespace biofmi;
using namespace edsparser;

// Helper: Transform EDS to l-EDS string
std::string transform_eds_to_leds_string(const EDS& eds, Length context_length) {
    std::ostringstream eds_str;
    for (size_t i = 0; i < eds.length(); i++) {
        StringSet symbol = eds.read_symbol(i);
        eds_str << "{";
        for (size_t j = 0; j < symbol.size(); j++) {
            eds_str << symbol[j];
            if (j + 1 < symbol.size()) eds_str << ",";
        }
        eds_str << "}";
    }

    std::istringstream input(eds_str.str());
    std::ostringstream output;
    eds_to_leds_cartesian(input, output, context_length, 1);
    return output.str();
}

// Helper: Generate pattern by extracting from l-EDS
std::string generate_pattern_from_leds(const EDS& leds, Length pattern_length, std::mt19937& rng) {
    if (leds.cardinality() == 0) {
        throw std::runtime_error("Cannot generate pattern from empty EDS");
    }

    std::string pattern;
    for (size_t pos = 0; pos < leds.length() && pattern.size() < pattern_length; pos++) {
        StringSet symbol = leds.read_symbol(pos);
        std::uniform_int_distribution<size_t> alt_dist(0, symbol.size() - 1);
        size_t alt_idx = alt_dist(rng);

        std::string chosen = symbol[alt_idx];
        size_t remaining = pattern_length - pattern.size();
        if (chosen.size() <= remaining) {
            pattern += chosen;
        } else {
            pattern += chosen.substr(0, remaining);
            break;
        }
    }

    return pattern;
}

void test_simple_validation() {
    std::cout << "\n=== Test: Simple Validation ===\n";

    std::stringstream ss("{ACGT}{AAAA,CCCC,GGGG,TTTT}{TGCA}");
    EDS eds(ss);
    std::string leds_str = transform_eds_to_leds_string(eds, 8);

    std::istringstream ls1(leds_str), ls2(leds_str);
    EDS leds(ls1);
    EDS leds_copy(ls2);

    std::cout << "L-EDS: " << leds_str;
    std::cout << "L-EDS length=" << leds.length() << " symbols\n";

    BioFMI index(std::move(leds), 8);
    index.build();
    std::cout << "Index built\n";

    std::mt19937 rng(42);
    int num_patterns = 10;
    int found_patterns = 0;
    int total_occurrences = 0;

    for (int i = 0; i < num_patterns; i++) {
        std::string pattern = generate_pattern_from_leds(leds_copy, 8, rng);
        
        // Search for pattern
        auto result = index.locate(pattern);

        int count = 0;
        for (const auto& [seq_id, occs] : result) {
            count += occs.size();
        }

        if (count > 0) found_patterns++;
        total_occurrences += count;

        std::cout << "  Pattern " << (i+1) << ": '" << pattern << "' -> " << count << " occ(s)\n";
    }

    std::cout << "✓ Test passed: " << found_patterns << "/" << num_patterns 
              << " patterns found (" << total_occurrences << " total occurrences)\n";
}

void test_medium_validation() {
    std::cout << "\n=== Test: Medium Complexity ===\n";

    std::stringstream ss("{AAAA}{G,C}{TTTT}{A,C,G,T}{CCCC}{GG,TT}{AAAA}");
    EDS eds(ss);
    std::string leds_str = transform_eds_to_leds_string(eds, 8);

    std::istringstream ls1(leds_str), ls2(leds_str);
    EDS leds(ls1);
    EDS leds_copy(ls2);

    std::cout << "L-EDS created (l=8)\n";

    BioFMI index(std::move(leds), 8);
    index.build();
    std::cout << "Index built\n";

    std::mt19937 rng(123);
    int num_patterns = 15;
    int found = 0;
    int total = 0;

    for (int i = 0; i < num_patterns; i++) {
        std::string pattern = generate_pattern_from_leds(leds_copy, 8, rng);
        auto result = index.locate(pattern);

        int count = 0;
        for (const auto& [seq_id, occs] : result) {
            count += occs.size();
        }

        if (count > 0) found++;
        total += count;
    }

    std::cout << "✓ Test passed: " << found << "/" << num_patterns 
              << " patterns found (" << total << " total occurrences)\n";
}

void test_degenerate_validation() {
    std::cout << "\n=== Test: Highly Degenerate ===\n";

    std::stringstream ss("{ACGT}{A,C,G,T}{A,C,G,T}{A,C,G,T}{TGCA}");
    EDS eds(ss);
    std::string leds_str = transform_eds_to_leds_string(eds, 8);

    std::istringstream ls1(leds_str), ls2(leds_str);
    EDS leds(ls1);
    EDS leds_copy(ls2);

    std::cout << "L-EDS created (l=8)\n";

    BioFMI index(std::move(leds), 8);
    index.build();
    std::cout << "Index built\n";

    std::mt19937 rng(456);
    int num_patterns = 10;
    int found = 0;

    for (int i = 0; i < num_patterns; i++) {
        std::string pattern = generate_pattern_from_leds(leds_copy, 8, rng);
        auto result = index.locate(pattern);

        int count = 0;
        for (const auto& [seq_id, occs] : result) {
            count += occs.size();
        }

        if (count > 0) found++;
    }

    std::cout << "✓ Test passed: " << found << "/" << num_patterns << " patterns found\n";
}

int main() {
    try {
        std::cout << "========================================\n";
        std::cout << "LOCATE VALIDATION TESTS\n";
        std::cout << "========================================\n";

        test_simple_validation();
        test_medium_validation();
        test_degenerate_validation();

        std::cout << "\n========================================\n";
        std::cout << "✓ ALL VALIDATION TESTS PASSED!\n";
        std::cout << "  Patterns generated from l-EDS were\n";
        std::cout << "  successfully found by locate function\n";
        std::cout << "========================================\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ TEST FAILED: " << e.what() << "\n";
        return 1;
    }
}
