// EDS parsing tests
#include "../../src/cpp/lib/eds.hpp"
#include <sstream>
#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>

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

void test_save_to_file() {
    std::cout << "Test 9: Save EDS to file... ";

    // Create an EDS
    std::stringstream ss("{ACGT}{A,ACA}{CGT}{T,TG}");
    biofmi::EDS eds(ss);

    // Save to file
    std::filesystem::path temp_path = std::filesystem::temp_directory_path() / "test_eds_save.eds";
    eds.save(temp_path);

    // Verify file exists and read it back
    assert(std::filesystem::exists(temp_path));
    std::ifstream ifs(temp_path);
    std::string content;
    std::getline(ifs, content);
    ifs.close();

    assert(content == "{ACGT}{A,ACA}{CGT}{T,TG}");

    // Clean up
    std::filesystem::remove(temp_path);

    std::cout << "PASSED\n";
}

void test_load_from_file() {
    std::cout << "Test 10: Load EDS from file... ";

    // Create a temporary file with EDS content
    std::filesystem::path temp_path = std::filesystem::temp_directory_path() / "test_eds_load.eds";
    std::ofstream ofs(temp_path);
    ofs << "{AC}{,A,T}{GT}";
    ofs.close();

    // Load from file
    biofmi::EDS eds = biofmi::EDS::load(temp_path);

    assert(eds.length() == 3);
    assert(eds.cardinality() == 5);
    assert(!eds.empty());

    const auto& sets = eds.get_sets();
    assert(sets[0][0] == "AC");
    assert(sets[1].size() == 3);
    assert(sets[1][0] == "");
    assert(sets[1][1] == "A");
    assert(sets[1][2] == "T");

    // Clean up
    std::filesystem::remove(temp_path);

    std::cout << "PASSED\n";
}

void test_roundtrip_file() {
    std::cout << "Test 11: Roundtrip EDS (save → load)... ";

    // Create original EDS
    std::stringstream ss("{ACGT}{A,ACA}{CGT}{T,TG}");
    biofmi::EDS eds1(ss);

    // Save to file
    std::filesystem::path temp_path = std::filesystem::temp_directory_path() / "test_eds_roundtrip.eds";
    eds1.save(temp_path);

    // Load from file
    biofmi::EDS eds2 = biofmi::EDS::load(temp_path);

    // Compare
    assert(eds1.length() == eds2.length());
    assert(eds1.cardinality() == eds2.cardinality());
    assert(eds1.size() == eds2.size());

    const auto& sets1 = eds1.get_sets();
    const auto& sets2 = eds2.get_sets();
    assert(sets1.size() == sets2.size());
    for (size_t i = 0; i < sets1.size(); i++) {
        assert(sets1[i].size() == sets2[i].size());
        for (size_t j = 0; j < sets1[i].size(); j++) {
            assert(sets1[i][j] == sets2[i][j]);
        }
    }

    // Clean up
    std::filesystem::remove(temp_path);

    std::cout << "PASSED\n";
}

void test_load_nonexistent_file() {
    std::cout << "Test 12: Load from nonexistent file (should fail)... ";

    std::filesystem::path nonexistent = "/nonexistent/path/to/file.eds";

    bool caught = false;
    try {
        biofmi::EDS eds = biofmi::EDS::load(nonexistent);
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        caught = (msg.find("Failed to open") != std::string::npos);
    }

    assert(caught);
    std::cout << "PASSED\n";
}

void test_statistics_simple() {
    std::cout << "Test 13: Statistics calculation (simple)... ";

    std::stringstream ss("{ACGT}{A,ACA}{CGT}{T,TG}");
    biofmi::EDS eds(ss);

    auto stats = eds.get_statistics();

    // Structure checks
    assert(stats.num_degenerate_symbols == 2);  // {A,ACA} and {T,TG}
    assert(stats.total_change_size == 2);        // 1 extra in each degenerate set

    // Length checks
    assert(stats.min_context_length == 1);       // "A" and "T"
    assert(stats.max_context_length == 4);       // "ACGT"
    assert(stats.avg_context_length > 2.0 && stats.avg_context_length < 3.0);

    // No empty strings
    assert(stats.num_empty_strings == 0);

    // Common characters in {A,ACA} - "A" is common prefix
    // Common characters in {T,TG} - "T" is common prefix
    assert(stats.num_common_chars == 2);

    std::cout << "PASSED\n";
}

void test_statistics_with_empty() {
    std::cout << "Test 14: Statistics with empty strings... ";

    std::stringstream ss("{AC}{,A,T}{GT}");
    biofmi::EDS eds(ss);

    auto stats = eds.get_statistics();

    assert(stats.num_degenerate_symbols == 1);   // Only {,A,T}
    assert(stats.total_change_size == 2);         // 2 extra strings in degenerate set
    assert(stats.num_empty_strings == 1);
    assert(stats.min_context_length == 0);        // Empty string

    std::cout << "PASSED\n";
}

void test_statistics_all_regular() {
    std::cout << "Test 15: Statistics all regular (no degenerate)... ";

    std::stringstream ss("{A}{C}{G}{T}");
    biofmi::EDS eds(ss);

    auto stats = eds.get_statistics();

    assert(stats.num_degenerate_symbols == 0);
    assert(stats.total_change_size == 0);
    assert(stats.num_common_chars == 0);
    assert(stats.min_context_length == 1);
    assert(stats.max_context_length == 1);
    assert(stats.avg_context_length == 1.0);

    std::cout << "PASSED\n";
}

void test_print_output() {
    std::cout << "Test 16: Print output... ";

    std::stringstream ss("{ACGT}{A,ACA}");
    biofmi::EDS eds(ss);

    std::stringstream output;
    eds.print(output);

    std::string result = output.str();
    assert(result.find("Set 0") != std::string::npos);
    assert(result.find("Set 1") != std::string::npos);
    assert(result.find("degenerate") != std::string::npos);
    assert(result.find("ACGT") != std::string::npos);

    std::cout << "PASSED\n";
}

void test_print_statistics_output() {
    std::cout << "Test 17: Print statistics output... ";

    std::stringstream ss("{ACGT}{A,ACA}{CGT}");
    biofmi::EDS eds(ss);

    std::stringstream output;
    eds.print_statistics(output);

    std::string result = output.str();
    assert(result.find("EDS Statistics") != std::string::npos);
    assert(result.find("Number of sets") != std::string::npos);
    assert(result.find("Degenerate symbols") != std::string::npos);
    assert(result.find("Context Lengths") != std::string::npos);

    std::cout << "PASSED\n";
}

void test_string_constructor() {
    std::cout << "Test 18: String constructor... ";

    std::string eds_str = "{ACGT}{A,ACA}{CGT}";
    biofmi::EDS eds(eds_str);

    assert(eds.length() == 3);
    assert(eds.cardinality() == 4);
    const auto& sets = eds.get_sets();
    assert(sets[0][0] == "ACGT");
    assert(sets[1][0] == "A");
    assert(sets[1][1] == "ACA");

    std::cout << "PASSED\n";
}

void test_string_with_sources() {
    std::cout << "Test 19: String constructor with sources... ";

    std::string eds_str = "{A}{B,C}";
    std::string seds_str = "{1}{2}{1,2}";

    biofmi::EDS eds(eds_str, seds_str);

    assert(eds.cardinality() == 3);
    assert(eds.has_sources());

    const auto& sources = eds.get_sources();
    assert(sources[0].count(1) == 1);
    assert(sources[1].count(2) == 1);
    assert(sources[2].size() == 2);

    std::cout << "PASSED\n";
}

void test_mixed_inputs() {
    std::cout << "Test 20: Mixed input types (file + string, string + file)... ";

    // Create temporary file with EDS
    std::filesystem::path temp_eds = std::filesystem::temp_directory_path() / "test_mixed_eds.eds";
    std::ofstream ofs(temp_eds);
    ofs << "{AC}{GT}";
    ofs.close();

    // Test: file EDS + string sEDS
    biofmi::EDS eds1(temp_eds, "{0}{1}");
    assert(eds1.cardinality() == 2);
    assert(eds1.has_sources());

    // Test: string EDS + file sEDS (create file first)
    std::filesystem::path temp_seds = std::filesystem::temp_directory_path() / "test_mixed_seds.seds";
    std::ofstream ofs2(temp_seds);
    ofs2 << "{2}{3}";
    ofs2.close();

    biofmi::EDS eds2("{XY}{ZW}", temp_seds);
    assert(eds2.cardinality() == 2);
    assert(eds2.has_sources());

    // Test: static load with two files
    biofmi::EDS eds3 = biofmi::EDS::load(temp_eds, temp_seds);
    assert(eds3.has_sources());

    // Cleanup
    std::filesystem::remove(temp_eds);
    std::filesystem::remove(temp_seds);

    std::cout << "PASSED\n";
}

void test_compact_format_parsing() {
    std::cout << "Test 21: Compact format parsing... ";

    // Compact format: no brackets on non-degenerate symbols
    std::string compact = "ACGT{A,ACA}CGT{T,TG}";
    biofmi::EDS eds(compact);

    assert(eds.length() == 4);
    assert(eds.cardinality() == 6);

    const auto& sets = eds.get_sets();
    assert(sets[0][0] == "ACGT");
    assert(sets[1].size() == 2);
    assert(sets[1][0] == "A");
    assert(sets[1][1] == "ACA");
    assert(sets[2][0] == "CGT");
    assert(sets[3].size() == 2);

    std::cout << "PASSED\n";
}

void test_compact_format_output() {
    std::cout << "Test 22: Compact format output... ";

    std::stringstream ss("{ACGT}{A,ACA}{CGT}{T,TG}");
    biofmi::EDS eds(ss);

    // Save in compact format
    std::stringstream output;
    eds.save(output, biofmi::EDS::OutputFormat::COMPACT);

    std::string result = output.str();
    // Remove newline
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }

    // Should be: ACGT{A,ACA}CGT{T,TG}
    assert(result == "ACGT{A,ACA}CGT{T,TG}");

    std::cout << "PASSED\n";
}

void test_roundtrip_compact() {
    std::cout << "Test 23: Roundtrip compact format (parse → save → parse)... ";

    // Start with compact format
    std::string compact = "ACGT{A,ACA}CGT";
    biofmi::EDS eds1(compact);

    // Save in compact format
    std::stringstream saved;
    eds1.save(saved, biofmi::EDS::OutputFormat::COMPACT);

    // Parse saved compact format
    biofmi::EDS eds2(saved.str());

    // Compare
    assert(eds1.length() == eds2.length());
    assert(eds1.cardinality() == eds2.cardinality());

    const auto& sets1 = eds1.get_sets();
    const auto& sets2 = eds2.get_sets();

    for (size_t i = 0; i < sets1.size(); i++) {
        assert(sets1[i].size() == sets2[i].size());
        for (size_t j = 0; j < sets1[i].size(); j++) {
            assert(sets1[i][j] == sets2[i][j]);
        }
    }

    std::cout << "PASSED\n";
}

void test_load_sources_string() {
    std::cout << "Test 24: Load sources from string... ";

    biofmi::EDS eds("{A}{B,C}");
    assert(!eds.has_sources());

    std::string seds_str = "{0}{1}{2}";
    eds.load_sources(seds_str);

    assert(eds.has_sources());
    const auto& sources = eds.get_sources();
    assert(sources.size() == 3);
    assert(sources[0].count(0) == 1);

    std::cout << "PASSED\n";
}

void test_generate_patterns() {
    std::cout << "Test 24: Generate patterns... ";

    // Create a simple EDS with enough length for varied patterns
    biofmi::EDS eds("{ACGT}{A,CA}{GG}");

    // Generate patterns
    std::stringstream output;
    eds.generate_patterns(output, 20, 8);

    // Check that we got 20 patterns
    std::string line;
    int count = 0;
    std::set<std::string> unique_patterns;
    while (std::getline(output, line)) {
        if (!line.empty()) {
            count++;
            // Each pattern should be 8 characters long
            assert(line.length() == 8);
            unique_patterns.insert(line);
        }
    }
    assert(count == 20);

    // With random starting positions, we should get at least some variety
    // (not all patterns identical - which would happen if all started at position 0)
    assert(unique_patterns.size() > 1);

    std::cout << "PASSED\n";
}

void test_generate_patterns_metadata_only() {
    std::cout << "Test 25: Generate patterns works in METADATA_ONLY mode... ";

    // Create temp file
    std::filesystem::path temp_file = std::filesystem::temp_directory_path() / "test_genpatterns.eds";
    std::ofstream ofs(temp_file);
    ofs << "{ACGT}{A,CA}{GG}";
    ofs.close();

    // Load in METADATA_ONLY mode
    auto eds = biofmi::EDS::load(temp_file.string(), biofmi::EDS::StoringMode::METADATA_ONLY);

    // Should work now (streaming from file)
    std::stringstream output;
    eds.generate_patterns(output, 5, 8);

    // Check that we got 5 patterns
    std::string line;
    int count = 0;
    while (std::getline(output, line)) {
        if (!line.empty()) {
            count++;
            // Each pattern should be 8 characters long
            assert(line.length() == 8);
        }
    }
    assert(count == 5);

    std::filesystem::remove(temp_file);
    std::cout << "PASSED\n";
}

void test_generate_patterns_are_valid() {
    std::cout << "Test 26: Generated patterns are valid (check with check_position)... ";

    // Create EDS with known structure
    std::string eds_str = "{ACGT}{A,CA}{GG}{T,TG}";
    biofmi::EDS eds(eds_str);

    // Generate patterns
    std::stringstream output;
    eds.generate_patterns(output, 10, 6);

    // For each generated pattern, verify it can be found in the EDS
    std::string pattern;
    int validated = 0;
    while (std::getline(output, pattern)) {
        if (pattern.empty()) continue;

        bool found = false;

        // Try all possible common positions
        // Total common chars: ACGT(4) + GG(2) = 6
        for (biofmi::Position common_pos = 0; common_pos < 6 && !found; common_pos++) {
            // Try without degenerate strings first (regular symbols only)
            try {
                if (eds.check_position(common_pos, {}, pattern)) {
                    found = true;
                    break;
                }
            } catch (const std::invalid_argument&) {
                // Pattern needs degenerate choices, continue to try with them
            }

            // Try with one degenerate choice (from symbol 1: {A,CA})
            for (int deg1 = 0; deg1 < 2 && !found; deg1++) {
                try {
                    if (eds.check_position(common_pos, {deg1}, pattern)) {
                        found = true;
                        break;
                    }
                } catch (const std::invalid_argument&) {
                    // Might need more degenerate choices
                } catch (const std::out_of_range&) {
                    // Invalid degenerate string number, skip
                    continue;
                }

                // Try with two degenerate choices (symbol 1 and symbol 3: {T,TG})
                for (int deg2 = 2; deg2 < 4 && !found; deg2++) {
                    try {
                        if (eds.check_position(common_pos, {deg1, deg2}, pattern)) {
                            found = true;
                            break;
                        }
                    } catch (const std::invalid_argument&) {
                        // Wrong combination
                    } catch (const std::out_of_range&) {
                        // Invalid degenerate string number, skip
                        continue;
                    }
                }
            }
        }

        assert(found); // Every generated pattern must be findable
        validated++;
    }

    assert(validated == 10); // All 10 patterns should be validated

    std::cout << "PASSED\n";
}

void test_generate_patterns_validation_with_sources() {
    std::cout << "Test 27: Generated patterns with sources are valid... ";

    // Create EDS with sources
    std::string eds_str = "{ACGT}{A,CA}{GG}";
    std::string seds_str = "{0}{1}{2}{0}";
    biofmi::EDS eds(eds_str, seds_str);

    // Generate patterns
    std::stringstream output;
    eds.generate_patterns(output, 5, 5);

    // For each generated pattern, verify it exists and has valid sources
    std::string pattern;
    int validated = 0;
    while (std::getline(output, pattern)) {
        if (pattern.empty()) continue;

        bool found = false;

        // Try all possible common positions (ACGT=4 + GG=2 = 6 common chars)
        for (biofmi::Position common_pos = 0; common_pos < 6 && !found; common_pos++) {
            // Try without degenerate choices
            try {
                if (eds.check_position(common_pos, {}, pattern)) {
                    found = true;
                    break;
                }
            } catch (const std::invalid_argument&) {
                // Pattern needs degenerate choices
            }

            // Try with degenerate choice from {A,CA} (strings 0,1)
            for (int deg = 0; deg < 2 && !found; deg++) {
                try {
                    if (eds.check_position(common_pos, {deg}, pattern)) {
                        found = true;
                        break;
                    }
                } catch (const std::invalid_argument&) {
                    // Wrong combination
                } catch (const std::out_of_range&) {
                    // Invalid degenerate string number
                    continue;
                }
            }
        }

        // Pattern must exist and have valid source intersection
        assert(found);
        validated++;
    }

    assert(validated == 5);

    std::cout << "PASSED\n";
}

void test_extract_basic() {
    std::cout << "Test 28: Extract basic... ";

    biofmi::EDS eds("{ACGT}{A,CA}{GG}{T,TT}");

    // Extract: position 1-2, selecting first alternative from each
    std::vector<int> changes = {0, 0};
    std::string result = eds.extract(1, 2, changes);
    assert(result == "AGG");  // {A} + {GG}

    // Extract: position 1-2, selecting second alternative from first, first from second
    changes = {1, 0};
    result = eds.extract(1, 2, changes);
    assert(result == "CAGG");  // {CA} + {GG}

    // Extract: position 3, selecting second alternative
    changes = {1};
    result = eds.extract(3, 1, changes);
    assert(result == "TT");  // {TT}

    std::cout << "PASSED\n";
}

void test_extract_empty() {
    std::cout << "Test 29: Extract with zero length... ";

    biofmi::EDS eds("{ACGT}{A,CA}");
    std::vector<int> changes = {};
    std::string result = eds.extract(0, 0, changes);
    assert(result == "");

    std::cout << "PASSED\n";
}

void test_extract_invalid_change_index() {
    std::cout << "Test 30: Extract with invalid change index... ";

    biofmi::EDS eds("{ACGT}{A,CA}");

    // Invalid index (only 0,1 valid for position 1)
    std::vector<int> changes = {5};
    bool threw = false;
    try {
        eds.extract(1, 1, changes);
    } catch (const std::out_of_range& e) {
        threw = true;
    }
    assert(threw);

    std::cout << "PASSED\n";
}

void test_extract_wrong_changes_size() {
    std::cout << "Test 31: Extract with wrong changes vector size... ";

    biofmi::EDS eds("{ACGT}{A,CA}{GG}");

    // Request 2 positions but provide 1 change
    std::vector<int> changes = {0};
    bool threw = false;
    try {
        eds.extract(0, 2, changes);
    } catch (const std::invalid_argument& e) {
        threw = true;
        std::string msg = e.what();
        assert(msg.find("changes vector size") != std::string::npos);
    }
    assert(threw);

    std::cout << "PASSED\n";
}

void test_extract_metadata_only() {
    std::cout << "Test 32: Extract throws in METADATA_ONLY mode... ";

    // Create temp file
    std::filesystem::path temp_file = std::filesystem::temp_directory_path() / "test_extract.eds";
    std::ofstream ofs(temp_file);
    ofs << "{ACGT}{A,CA}";
    ofs.close();

    // Load in METADATA_ONLY mode
    auto eds = biofmi::EDS::load(temp_file.string(), biofmi::EDS::StoringMode::METADATA_ONLY);

    // Should throw
    std::vector<int> changes = {0};
    bool threw = false;
    try {
        eds.extract(0, 1, changes);
    } catch (const std::runtime_error& e) {
        threw = true;
        std::string msg = e.what();
        assert(msg.find("FULL mode") != std::string::npos);
    }
    assert(threw);

    std::filesystem::remove(temp_file);
    std::cout << "PASSED\n";
}

void test_check_position_basic() {
    std::cout << "Test 33: check_position basic... ";

    biofmi::EDS eds("{ACGT}{A,ACA}{CGT}{T,TG}");

    // Pattern "ACG" at (0, {})
    assert(eds.check_position(0, {}, "ACG") == true);

    // Pattern "ACG" at (4, {0})
    assert(eds.check_position(4, {0}, "ACG") == true);

    // Pattern "ACG" at (6, {1})
    assert(eds.check_position(6, {1}, "ACG") == true);

    // Pattern "GTT" at (5, {2})
    assert(eds.check_position(5, {2}, "GTT") == true);

    // Pattern "GTT" at (5, {3})
    assert(eds.check_position(5, {3}, "GTT") == true);

    // Pattern "ACGTT" at (4, {0, 2})
    assert(eds.check_position(4, {0, 2}, "ACGTT") == true);

    // Pattern "ACGTT" at (4, {0, 3})
    assert(eds.check_position(4, {0, 3}, "ACGTT") == true);

    std::cout << "PASSED\n";
}

void test_check_position_negative() {
    std::cout << "Test 34: check_position negative cases... ";

    biofmi::EDS eds("{ACGT}{A,ACA}{CGT}{T,TG}");

    // Wrong pattern
    assert(eds.check_position(0, {}, "XYZ") == false);

    // Pattern doesn't match
    assert(eds.check_position(0, {}, "ACGTX") == false);

    // Position beyond range
    assert(eds.check_position(100, {}, "ACG") == false);

    // Wrong degenerate string for position
    assert(eds.check_position(4, {1}, "ACG") == false);  // Should be {0}, not {1}

    std::cout << "PASSED\n";
}

void test_check_position_errors() {
    std::cout << "Test 35: check_position error handling... ";

    biofmi::EDS eds("{ACGT}{A,ACA}{CGT}{T,TG}");

    // Invalid degenerate string number
    bool threw = false;
    try {
        eds.check_position(4, {999}, "ACG");
    } catch (const std::out_of_range&) {
        threw = true;
    }
    assert(threw);

    // Not enough degenerate strings
    threw = false;
    try {
        eds.check_position(4, {}, "ACGTT");  // Should need {0, 2} or similar
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);

    // Wrong symbol for degenerate string
    threw = false;
    try {
        // String 2 belongs to symbol 3, not symbol 1
        eds.check_position(4, {2}, "ACG");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);

    // Negative degenerate string number
    threw = false;
    try {
        eds.check_position(4, {-1}, "ACG");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);

    std::cout << "PASSED\n";
}

void test_check_position_metadata_only() {
    std::cout << "Test 36: check_position in METADATA_ONLY mode... ";

    // Create temp file
    std::filesystem::path temp_file =
        std::filesystem::temp_directory_path() / "test_check_pos.eds";
    std::ofstream ofs(temp_file);
    ofs << "{ACGT}{A,ACA}{CGT}{T,TG}";
    ofs.close();

    // Load in METADATA_ONLY mode
    auto eds = biofmi::EDS::load(temp_file, biofmi::EDS::StoringMode::METADATA_ONLY);

    // Should work same as FULL mode
    assert(eds.check_position(0, {}, "ACG") == true);
    assert(eds.check_position(4, {0}, "ACG") == true);
    assert(eds.check_position(5, {2}, "GTT") == true);
    assert(eds.check_position(0, {}, "XYZ") == false);

    std::filesystem::remove(temp_file);
    std::cout << "PASSED\n";
}

void test_check_position_empty_pattern() {
    std::cout << "Test 37: check_position with empty pattern... ";

    biofmi::EDS eds("{ACGT}{A,ACA}");

    // Empty pattern should always match
    assert(eds.check_position(0, {}, "") == true);
    assert(eds.check_position(3, {}, "") == true);

    std::cout << "PASSED\n";
}

void test_check_position_empty_eds() {
    std::cout << "Test 38: check_position with empty EDS... ";

    biofmi::EDS eds("");

    // Empty EDS should return false
    assert(eds.check_position(0, {}, "ACG") == false);

    std::cout << "PASSED\n";
}

void test_check_position_offset() {
    std::cout << "Test 39: check_position with offset in symbol... ";

    biofmi::EDS eds("{ACGT}{A,ACA}{CGT}{T,TG}");

    // Start at position 1 ('C' in ACGT)
    assert(eds.check_position(1, {}, "CG") == true);
    assert(eds.check_position(1, {}, "CGT") == true);

    // Start at position 2 ('G' in ACGT)
    assert(eds.check_position(2, {}, "GT") == true);

    // Start at position 3 ('T' in ACGT)
    assert(eds.check_position(3, {}, "T") == true);

    std::cout << "PASSED\n";
}

void test_check_position_pattern_spans_multiple() {
    std::cout << "Test 40: check_position pattern spanning multiple symbols... ";

    biofmi::EDS eds("{ACGT}{A,ACA}{CGT}{T,TG}");

    // Full pattern spanning all symbols
    assert(eds.check_position(0, {0, 2}, "ACGTACGTT") == true);
    assert(eds.check_position(0, {0, 3}, "ACGTACGTTG") == true);
    assert(eds.check_position(0, {1, 2}, "ACGTACACGTT") == true);

    std::cout << "PASSED\n";
}

void test_check_position_with_sources_valid() {
    std::cout << "Test 41: check_position with sources (valid paths)... ";

    // EDS:  {ACGT}{A,ACA}{CGT}{T,TG}
    // sEDS: {0}{1,3}{2}{0}{1}{2,3}
    //       str0  str1  str2 str3 str4  str5
    std::string eds_str = "{ACGT}{A,ACA}{CGT}{T,TG}";
    std::string seds_str = "{0}{1,3}{2}{0}{1}{2,3}";

    biofmi::EDS eds(eds_str, seds_str);

    // Pattern "ACGTT" at (4, {0, 2})
    // Uses string 0 "A" with sources {1,3}
    // Uses string 2 "T" with sources {1}
    // Intersection: {1,3} ∩ {1} = {1} ✓
    assert(eds.check_position(4, {0, 2}, "ACGTT") == true);

    // Pattern "ACGTG" at (4, {0, 3})
    // Uses string 0 "A" with sources {1,3}
    // Uses string 3 "TG" with sources {2,3}
    // Intersection: {1,3} ∩ {2,3} = {3} ✓
    assert(eds.check_position(4, {0, 3}, "ACGTTG") == true);

    // Pattern "ACACGTT" at (4, {1, 2})
    // Uses string 1 "ACA" with sources {2}
    // Uses string 2 "T" with sources {1}
    // Intersection: {2} ∩ {1} = {} EMPTY
    assert(eds.check_position(4, {1, 2}, "ACACGTT") == false);

    std::cout << "PASSED\n";
}

void test_check_position_with_sources_universal() {
    std::cout << "Test 42: check_position with sources (universal marker)... ";

    // EDS with universal markers
    std::string eds_str = "{ACGT}{A,ACA}{CGT}";
    std::string seds_str = "{0}{1}{2}{0}";

    biofmi::EDS eds(eds_str, seds_str);

    // Universal {0} should not restrict intersection
    // Pattern "ACGTACGT" using string 0 "A"
    // Sources: {0} ∩ {1} ∩ {0} = {1}
    assert(eds.check_position(0, {0}, "ACGTACGT") == true);

    // Pattern "ACGTACACGT" using string 1 "ACA"
    // Sources: {0} ∩ {2} ∩ {0} = {2}
    assert(eds.check_position(0, {1}, "ACGTACACGT") == true);

    std::cout << "PASSED\n";
}

void test_check_position_without_sources() {
    std::cout << "Test 43: check_position without sources loaded... ";

    biofmi::EDS eds("{ACGT}{A,ACA}{CGT}{T,TG}");

    // Without sources, any valid pattern should match
    assert(eds.check_position(4, {0, 2}, "ACGTT") == true);
    assert(eds.check_position(4, {1, 2}, "ACACGTT") == true);

    // Pattern still needs to match the strings
    assert(eds.check_position(4, {0, 2}, "WRONG") == false);

    std::cout << "PASSED\n";
}

void test_check_position_sources_all_paths() {
    std::cout << "Test 42: check_position sources with all universal... ";

    // All strings have universal paths
    std::string eds_str = "{ACGT}{A,ACA}";
    std::string seds_str = "{0}{0}{0}";

    biofmi::EDS eds(eds_str, seds_str);

    // All intersections should be {0}
    assert(eds.check_position(4, {0}, "A") == true);
    assert(eds.check_position(4, {1}, "ACA") == true);

    std::cout << "PASSED\n";
}

void test_check_position_sources_disjoint() {
    std::cout << "Test 43: check_position sources disjoint paths... ";

    // Create EDS where some combinations have disjoint paths
    std::string eds_str = "{AC}{A,C}{GT}";
    std::string seds_str = "{0}{1}{2}{0}";

    biofmi::EDS eds(eds_str, seds_str);

    // Valid: {0} ∩ {1} ∩ {0} = {1}
    assert(eds.check_position(0, {0}, "ACAGT") == true);

    // Valid: {0} ∩ {2} ∩ {0} = {2}
    assert(eds.check_position(0, {1}, "ACCGT") == true);

    std::cout << "PASSED\n";
}

void test_check_position_sources_metadata_only() {
    std::cout << "Test 44: check_position with sources in METADATA_ONLY mode... ";

    // Create temp files
    std::filesystem::path temp_eds =
        std::filesystem::temp_directory_path() / "test_check_pos_sources.eds";
    std::filesystem::path temp_seds =
        std::filesystem::temp_directory_path() / "test_check_pos_sources.seds";

    std::ofstream ofs_eds(temp_eds);
    ofs_eds << "{ACGT}{A,ACA}{CGT}{T,TG}";
    ofs_eds.close();

    std::ofstream ofs_seds(temp_seds);
    ofs_seds << "{0}{1,3}{2}{0}{1}{2,3}";
    ofs_seds.close();

    // Load in METADATA_ONLY mode
    auto eds = biofmi::EDS::load(temp_eds, temp_seds,
                                 biofmi::EDS::StoringMode::METADATA_ONLY);

    // Should work same as FULL mode with source validation
    assert(eds.check_position(4, {0, 2}, "ACGTT") == true);   // Valid path
    assert(eds.check_position(4, {1, 2}, "ACACGTT") == false); // Empty intersection

    std::filesystem::remove(temp_eds);
    std::filesystem::remove(temp_seds);

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
        test_save_to_file();
        test_load_from_file();
        test_roundtrip_file();
        test_load_nonexistent_file();
        test_statistics_simple();
        test_statistics_with_empty();
        test_statistics_all_regular();
        test_print_output();
        test_print_statistics_output();
        test_string_constructor();
        test_string_with_sources();
        test_mixed_inputs();
        test_compact_format_parsing();
        test_compact_format_output();
        test_roundtrip_compact();
        test_load_sources_string();
        test_generate_patterns();
        test_generate_patterns_metadata_only();
        test_generate_patterns_are_valid();
        test_generate_patterns_validation_with_sources();
        test_extract_basic();
        test_extract_empty();
        test_extract_invalid_change_index();
        test_extract_wrong_changes_size();
        test_extract_metadata_only();
        test_check_position_basic();
        test_check_position_negative();
        test_check_position_errors();
        test_check_position_metadata_only();
        test_check_position_empty_pattern();
        test_check_position_empty_eds();
        test_check_position_offset();
        test_check_position_pattern_spans_multiple();
        test_check_position_with_sources_valid();
        test_check_position_with_sources_universal();
        test_check_position_without_sources();
        test_check_position_sources_all_paths();
        test_check_position_sources_disjoint();
        test_check_position_sources_metadata_only();

        std::cout << "\n✓ All tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed: " << e.what() << "\n";
        return 1;
    }
}
