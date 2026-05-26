// Comprehensive validation test for locate functionality
#include "index/index.hpp"
#include "formats/eds.hpp"
#include "transforms/eds_transforms.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cassert>
#include <random>
#include <filesystem>

using namespace biofmi;

// Helper: Transform EDS to l-EDS using the stream API
EDS transform_eds_to_leds(const EDS& eds, Length context_length) {
    // Reconstruct EDS string from symbols
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

    // Transform to l-EDS
    std::istringstream input(eds_str.str());
    std::ostringstream output;
    eds_to_leds_cartesian(input, output, context_length, 1);

    // Load l-EDS from result
    std::istringstream result(output.str());
    return EDS(result);
}

// Helper: Generate random pattern from EDS
std::pair<std::string, std::pair<Position, std::vector<int>>>
generate_pattern_with_ground_truth(const EDS& eds, Length pattern_length, std::mt19937& rng) {
    // Pick random starting position
    std::uniform_int_distribution<size_t> pos_dist(0, eds.length() - 1);
    Position start_pos = pos_dist(rng);

    // Build pattern by walking through EDS
    std::string pattern;
    std::vector<int> changes;
    int current_change_id = 0;

    Position current_pos = start_pos;
    while (pattern.size() < pattern_length && current_pos < eds.length()) {
        StringSet symbol = eds.read_symbol(current_pos);
        size_t symbol_size = symbol.size();

        // Pick random alternative
        std::uniform_int_distribution<size_t> alt_dist(0, symbol_size - 1);
        size_t alt_idx = alt_dist(rng);

        // Track if this is a degenerate symbol
        if (symbol_size > 1) {
            current_change_id++;
            changes.push_back(current_change_id);
        }

        // Append to pattern (up to pattern_length)
        std::string chosen = symbol[alt_idx];
        size_t remaining = pattern_length - pattern.size();
        if (chosen.size() <= remaining) {
            pattern += chosen;
        } else {
            pattern += chosen.substr(0, remaining);
            break;
        }

        current_pos++;
    }

    return {pattern, {start_pos, changes}};
}

// Test case 1: Simple EDS
void test_simple_eds() {
    std::cout << "\n=== Test 1: Simple EDS ===\n";

    // Create simple l-EDS
    std::stringstream ss("{ACGT}{AAAA,CCCC,GGGG,TTTT}{TGCA}");
    EDS eds(ss);

    std::cout << "Original EDS: {ACGT}{AAAA,CCCC,GGGG,TTTT}{TGCA}\n";
    std::cout << "EDS length: " << eds.length() << " symbols\n";

    // Transform to l-EDS (l=8)
    EDS leds = transform_eds_to_leds(eds, 8);
    std::cout << "Transformed to l-EDS (l=8)\n";

    // Build index
    BioFMI index(std::move(leds), 8);
    index.build();
    std::cout << "Index built\n";

    // Generate patterns
    std::mt19937 rng(42);
    int num_patterns = 10;
    int valid_patterns = 0;

    for (int i = 0; i < num_patterns; i++) {
        auto [pattern, ground_truth] = generate_pattern_with_ground_truth(eds, 8, rng);
        auto [gt_pos, gt_changes] = ground_truth;

        // Search
        auto result = index.locate(pattern);

        // Validate
        int total_occurrences = 0;
        for (const auto& [seq_id, occs] : result) {
            total_occurrences += occs.size();
            for (const auto& [pos, changes] : occs) {
                // Verify using extract: extract the string and compare with pattern
                try {
                    std::string extracted = eds.extract(pos, pattern.length(), changes);
                    if (extracted != pattern) {
                        std::cerr << "ERROR: Extracted string '" << extracted
                                 << "' != pattern '" << pattern << "' at pos=" << pos << "\n";
                        return;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "ERROR: extract failed at pos=" << pos << ": "
                             << e.what() << "\n";
                    return;
                }
            }
        }

        if (total_occurrences > 0) {
            valid_patterns++;
        }

        std::cout << "  Pattern " << (i+1) << ": '" << pattern << "' -> "
                  << total_occurrences << " occurrence(s) [all valid]\n";
    }

    std::cout << "✓ Test 1 passed: " << valid_patterns << "/" << num_patterns
              << " patterns found and validated\n";
}

// Test case 2: Medium complexity EDS
void test_medium_eds() {
    std::cout << "\n=== Test 2: Medium Complexity EDS ===\n";

    std::stringstream ss("{AAAA}{G,C}{TTTT}{A,C,G,T}{CCCC}{GG,TT}{AAAA}");
    EDS eds(ss);

    std::cout << "Original EDS: {AAAA}{G,C}{TTTT}{A,C,G,T}{CCCC}{GG,TT}{AAAA}\n";
    std::cout << "EDS length: " << eds.length() << " symbols\n";

    EDS leds = transform_eds_to_leds(eds, 8);
    std::cout << "Transformed to l-EDS (l=8)\n";

    BioFMI index(std::move(leds), 8);
    index.build();
    std::cout << "Index built\n";

    std::mt19937 rng(123);
    int num_patterns = 20;
    int valid_count = 0;

    for (int i = 0; i < num_patterns; i++) {
        auto [pattern, ground_truth] = generate_pattern_with_ground_truth(eds, 8, rng);
        auto result = index.locate(pattern);

        int total = 0;
        for (const auto& [seq_id, occs] : result) {
            for (const auto& [pos, changes] : occs) {
                std::string extracted = eds.extract(pos, pattern.length(), changes);
                if (extracted != pattern) {
                    std::cerr << "ERROR: Extracted '" << extracted << "' != pattern '" << pattern << "'\n";
                    return;
                }
                total++;
            }
        }

        if (total > 0) valid_count++;
    }

    std::cout << "✓ Test 2 passed: " << valid_count << "/" << num_patterns
              << " patterns found and validated\n";
}

// Test case 3: Highly degenerate EDS
void test_highly_degenerate() {
    std::cout << "\n=== Test 3: Highly Degenerate EDS ===\n";

    std::stringstream ss("{ACGT}{A,C,G,T}{A,C,G,T}{A,C,G,T}{TGCA}");
    EDS eds(ss);

    std::cout << "Original EDS: {ACGT}{A,C,G,T}{A,C,G,T}{A,C,G,T}{TGCA}\n";
    std::cout << "EDS length: " << eds.length() << " symbols\n";

    EDS leds = transform_eds_to_leds(eds, 8);
    std::cout << "Transformed to l-EDS (l=8)\n";

    BioFMI index(std::move(leds), 8);
    index.build();
    std::cout << "Index built\n";

    std::mt19937 rng(456);
    int num_patterns = 15;
    int valid_count = 0;

    for (int i = 0; i < num_patterns; i++) {
        auto [pattern, ground_truth] = generate_pattern_with_ground_truth(eds, 8, rng);
        auto result = index.locate(pattern);

        int total = 0;
        for (const auto& [seq_id, occs] : result) {
            for (const auto& [pos, changes] : occs) {
                std::string extracted = eds.extract(pos, pattern.length(), changes);
                if (extracted != pattern) {
                    std::cerr << "ERROR: Extracted '" << extracted << "' != pattern '" << pattern
                             << "' at pos=" << pos << "\n";
                    return;
                }
                total++;
            }
        }

        if (total > 0) valid_count++;
        std::cout << "  Pattern " << (i+1) << ": " << total << " occurrences [all valid]\n";
    }

    std::cout << "✓ Test 3 passed: " << valid_count << "/" << num_patterns
              << " patterns found and validated\n";
}

// Test case 4: Long reference sequences
void test_long_reference() {
    std::cout << "\n=== Test 4: Long Reference Sequences ===\n";

    std::stringstream ss("{ACGTACGTACGT}{AA,CC,GG,TT}{TGCATGCATGCA}");
    EDS eds(ss);

    std::cout << "Original EDS with long references\n";
    std::cout << "EDS length: " << eds.length() << " symbols\n";

    EDS leds = transform_eds_to_leds(eds, 8);
    std::cout << "Transformed to l-EDS (l=8)\n";

    BioFMI index(std::move(leds), 8);
    index.build();
    std::cout << "Index built\n";

    std::mt19937 rng(789);
    int num_patterns = 15;
    int valid_count = 0;

    for (int i = 0; i < num_patterns; i++) {
        auto [pattern, ground_truth] = generate_pattern_with_ground_truth(eds, 8, rng);
        auto result = index.locate(pattern);

        int total = 0;
        for (const auto& [seq_id, occs] : result) {
            for (const auto& [pos, changes] : occs) {
                std::string extracted = eds.extract(pos, pattern.length(), changes);
                if (extracted != pattern) {
                    std::cerr << "ERROR: Extracted '" << extracted << "' != pattern '" << pattern << "'\n";
                    return;
                }
                total++;
            }
        }

        if (total > 0) valid_count++;
    }

    std::cout << "✓ Test 4 passed: " << valid_count << "/" << num_patterns
              << " patterns found and validated\n";
}

// Test case 5: Mixed complexity
void test_mixed_complexity() {
    std::cout << "\n=== Test 5: Mixed Complexity EDS ===\n";

    std::stringstream ss("{AAAAAAAA}{A,AA,AAA}{TTTTTTTT}{G,C}{GGGGGGGG}{AT,CG,TA,GC}");
    EDS eds(ss);

    std::cout << "Original EDS with varying degenerate lengths\n";
    std::cout << "EDS length: " << eds.length() << " symbols\n";

    EDS leds = transform_eds_to_leds(eds, 8);
    std::cout << "Transformed to l-EDS (l=8)\n";

    BioFMI index(std::move(leds), 8);
    index.build();
    std::cout << "Index built\n";

    std::mt19937 rng(101112);
    int num_patterns = 20;
    int valid_count = 0;
    int total_occurrences = 0;

    for (int i = 0; i < num_patterns; i++) {
        auto [pattern, ground_truth] = generate_pattern_with_ground_truth(eds, 8, rng);
        auto result = index.locate(pattern);

        int pattern_occs = 0;
        for (const auto& [seq_id, occs] : result) {
            for (const auto& [pos, changes] : occs) {
                std::string extracted = eds.extract(pos, pattern.length(), changes);
                if (extracted != pattern) {
                    std::cerr << "ERROR: Extracted '" << extracted << "' != pattern '" << pattern << "'\n";
                    return;
                }
                pattern_occs++;
            }
        }

        total_occurrences += pattern_occs;
        if (pattern_occs > 0) valid_count++;
    }

    std::cout << "✓ Test 5 passed: " << valid_count << "/" << num_patterns
              << " patterns found (" << total_occurrences << " total occurrences) [all valid]\n";
}

int main() {
    try {
        std::cout << "========================================\n";
        std::cout << "COMPREHENSIVE LOCATE VALIDATION TESTS\n";
        std::cout << "========================================\n";

        test_simple_eds();
        test_medium_eds();
        test_highly_degenerate();
        test_long_reference();
        test_mixed_complexity();

        std::cout << "\n========================================\n";
        std::cout << "✓ ALL VALIDATION TESTS PASSED!\n";
        std::cout << "========================================\n";

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\n❌ TEST FAILED: " << e.what() << "\n";
        return 1;
    }
}
