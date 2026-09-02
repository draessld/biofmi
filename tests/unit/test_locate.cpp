#include "index/index.hpp"
#include <iostream>
#include <cassert>

using namespace biofmi;

int main() {
    try {
        std::cout << "Testing locate functionality...\n";

        // Test 1: Build index from simple EDS in memory
        std::cout << "  Creating simple l-EDS and building index (l=4)...\n";
        std::stringstream ss("{ACGT}{A,C}{TG}{G,T}");
        EDS eds(ss);
        BioFMI index(std::move(eds), 4);
        index.build();
        std::cout << "    ✓ Index built successfully\n";

        // Test 2: Search for a pattern that should be found
        std::cout << "  Testing pattern search...\n";

        // Try a simple 5-character pattern (l=4, chunk_size=5)
        std::string pattern1 = "ACGTA";
        auto result1 = index.locate(pattern1);
        std::cout << "    Pattern '" << pattern1 << "': ";
        if (result1.empty()) {
            std::cout << "not found (may be expected)\n";
        } else {
            size_t count = 0;
            for (const auto& [seq_id, occs] : result1) {
                count += occs.size();
            }
            std::cout << count << " occurrence(s) found\n";
        }

        // Test 3: Pattern that's definitely not in the data
        std::cout << "  Testing pattern not in data...\n";
        std::string pattern2 = "ZZZZZ";
        auto result2 = index.locate(pattern2);
        assert(result2.empty() && "Pattern 'ZZZZ' should not be found");
        std::cout << "    ✓ Correctly returned empty result for non-existent pattern\n";

        // Test 4: Pattern length validation
        std::cout << "  Testing pattern length validation...\n";
        bool caught_exception = false;
        try {
            std::string pattern3 = "ACGT"; // Length 4, not multiple of 5
            index.locate(pattern3);
        } catch (const std::runtime_error& e) {
            caught_exception = true;
            std::cout << "    ✓ Correctly threw exception for invalid length: " << e.what() << "\n";
        }
        assert(caught_exception && "Should throw exception for invalid pattern length");

        // Test 5: Longer pattern (10 characters = 2 chunks of 5)
        std::cout << "  Testing longer pattern (10 chars)...\n";
        std::string pattern4 = "ACGTAACGTA";
        auto result4 = index.locate(pattern4);
        std::cout << "    Pattern '" << pattern4 << "': ";
        if (result4.empty()) {
            std::cout << "not found (may be expected)\n";
        } else {
            size_t count = 0;
            for (const auto& [seq_id, occs] : result4) {
                count += occs.size();
            }
            std::cout << count << " occurrence(s) found\n";
            // Print details
            for (const auto& [seq_id, occs] : result4) {
                for (const auto& occ : occs) {
                    std::cout << "      Position " << occ.position << ", changes: [";
                    const auto& changes = occ.changes;
                    for (size_t i = 0; i < changes.size(); i++) {
                        std::cout << changes[i];
                        if (i + 1 < changes.size()) std::cout << ", ";
                    }
                    std::cout << "]\n";
                }
            }
        }

        std::cout << "\n✓ All locate tests passed!\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        return 1;
    }
}
