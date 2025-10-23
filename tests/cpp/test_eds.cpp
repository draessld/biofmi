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

        std::cout << "\n✓ All tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed: " << e.what() << "\n";
        return 1;
    }
}
