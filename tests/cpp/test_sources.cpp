// sEDS (source) parsing tests
#include "../../src/cpp/lib/eds.hpp"
#include <sstream>
#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>

void test_simple_sources() {
    std::cout << "Test 1: Simple sEDS parsing... ";

    // EDS: {ACGT}{A,ACA}{CGT}{T,TG}
    // String IDs: 0=ACGT, 1=A, 2=ACA, 3=CGT, 4=T, 5=TG
    std::stringstream eds_ss("{ACGT}{A,ACA}{CGT}{T,TG}");
    std::stringstream seds_ss("{0}{1,3}{2}{0}{1}{2,3}");

    biofmi::EDS eds(eds_ss, seds_ss);

    assert(eds.has_sources());
    assert(eds.cardinality() == 6);

    const auto& sources = eds.get_sources();
    assert(sources.size() == 6);

    // str0 "ACGT": {0} = all paths
    assert(sources[0].size() == 1);
    assert(sources[0].count(0) == 1);

    // str1 "A": {1,3}
    assert(sources[1].size() == 2);
    assert(sources[1].count(1) == 1);
    assert(sources[1].count(3) == 1);

    // str2 "ACA": {2}
    assert(sources[2].size() == 1);
    assert(sources[2].count(2) == 1);

    // str3 "CGT": {0} = all paths
    assert(sources[3].size() == 1);
    assert(sources[3].count(0) == 1);

    // str4 "T": {1}
    assert(sources[4].size() == 1);
    assert(sources[4].count(1) == 1);

    // str5 "TG": {2,3}
    assert(sources[5].size() == 2);
    assert(sources[5].count(2) == 1);
    assert(sources[5].count(3) == 1);

    std::cout << "PASSED\n";
}

void test_load_sources_separately() {
    std::cout << "Test 2: Load sources separately... ";

    std::stringstream eds_ss("{AC}{,A,T}{GT}");
    biofmi::EDS eds(eds_ss);

    assert(!eds.has_sources());
    assert(eds.cardinality() == 5);

    // Now load sources
    std::stringstream seds_ss("{0}{1}{2}{3}{0}");
    eds.load_sources(seds_ss);

    assert(eds.has_sources());
    const auto& sources = eds.get_sources();
    assert(sources.size() == 5);

    assert(sources[0].count(0) == 1);  // AC: {0}
    assert(sources[1].count(1) == 1);  // "": {1}
    assert(sources[2].count(2) == 1);  // A: {2}
    assert(sources[3].count(3) == 1);  // T: {3}
    assert(sources[4].count(0) == 1);  // GT: {0}

    std::cout << "PASSED\n";
}

void test_save_sources() {
    std::cout << "Test 3: Save sources... ";

    std::stringstream eds_ss("{A}{B,C}");
    std::stringstream seds_ss("{1}{2}{1,2}");

    biofmi::EDS eds(eds_ss, seds_ss);

    // Save sources
    std::stringstream output;
    eds.save_sources(output);

    std::string result = output.str();
    // Remove newline for comparison
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }

    // Should output: {1}{2}{1,2}
    assert(result == "{1}{2}{1,2}");

    std::cout << "PASSED\n";
}

void test_sources_with_whitespace() {
    std::cout << "Test 4: Sources with whitespace... ";

    std::stringstream eds_ss("{A}{B}");
    std::stringstream seds_ss("{ 1 } { 2 , 3 }");

    biofmi::EDS eds(eds_ss, seds_ss);

    const auto& sources = eds.get_sources();
    assert(sources[0].count(1) == 1);
    assert(sources[1].size() == 2);
    assert(sources[1].count(2) == 1);
    assert(sources[1].count(3) == 1);

    std::cout << "PASSED\n";
}

void test_all_paths_marker() {
    std::cout << "Test 5: All paths marker {0}... ";

    std::stringstream eds_ss("{ACGT}{A,T}");
    std::stringstream seds_ss("{0}{1}{2}");

    biofmi::EDS eds(eds_ss, seds_ss);

    const auto& sources = eds.get_sources();

    // str0 "ACGT": {0} stored as literal 0
    assert(sources[0].size() == 1);
    assert(sources[0].count(0) == 1);

    // str1 "A": {1}
    assert(sources[1].count(1) == 1);

    // str2 "T": {2}
    assert(sources[2].count(2) == 1);

    std::cout << "PASSED\n";
}

void test_invalid_cardinality_mismatch() {
    std::cout << "Test 6: Invalid - cardinality mismatch... ";

    std::stringstream eds_ss("{A}{B,C}");  // Cardinality = 3
    std::stringstream seds_ss("{1}{2}");   // Only 2 source sets

    bool caught = false;
    try {
        biofmi::EDS eds(eds_ss, seds_ss);
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        caught = (msg.find("cardinality") != std::string::npos);
    }

    assert(caught);
    std::cout << "PASSED\n";
}

void test_invalid_empty_path_set() {
    std::cout << "Test 7: Invalid - empty path set... ";

    std::stringstream eds_ss("{A}");
    std::stringstream seds_ss("{}");  // Empty path set

    bool caught = false;
    try {
        biofmi::EDS eds(eds_ss, seds_ss);
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        caught = (msg.find("Empty path set") != std::string::npos);
    }

    assert(caught);
    std::cout << "PASSED\n";
}

void test_invalid_negative_path_id() {
    std::cout << "Test 8: Invalid - negative path ID... ";

    std::stringstream eds_ss("{A}");
    std::stringstream seds_ss("{-1}");  // Negative path ID (will be caught as invalid character)

    bool caught = false;
    try {
        biofmi::EDS eds(eds_ss, seds_ss);
    } catch (const std::runtime_error& e) {
        // Will catch as "Invalid character" since '-' is not a digit
        caught = true;
    }

    assert(caught);
    std::cout << "PASSED\n";
}

void test_invalid_missing_bracket() {
    std::cout << "Test 9: Invalid - missing bracket... ";

    std::stringstream eds_ss("{A}{B}");
    std::stringstream seds_ss("{1}2}");  // Missing '{'

    bool caught = false;
    try {
        biofmi::EDS eds(eds_ss, seds_ss);
    } catch (const std::runtime_error& e) {
        caught = true;
    }

    assert(caught);
    std::cout << "PASSED\n";
}

void test_save_without_sources() {
    std::cout << "Test 10: Save without sources (should fail)... ";

    std::stringstream eds_ss("{A}");
    biofmi::EDS eds(eds_ss);

    assert(!eds.has_sources());

    bool caught = false;
    try {
        std::stringstream output;
        eds.save_sources(output);
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        caught = (msg.find("no sources") != std::string::npos);
    }

    assert(caught);
    std::cout << "PASSED\n";
}

void test_roundtrip() {
    std::cout << "Test 11: Roundtrip (parse → save → parse)... ";

    std::stringstream eds_ss("{ACGT}{A,ACA}{CGT}");
    std::stringstream seds_ss("{0}{1,2}{3}{0}");

    biofmi::EDS eds1(eds_ss, seds_ss);

    // Save sources
    std::stringstream saved;
    eds1.save_sources(saved);

    // Parse again
    std::stringstream eds_ss2("{ACGT}{A,ACA}{CGT}");
    biofmi::EDS eds2(eds_ss2, saved);

    // Compare
    const auto& sources1 = eds1.get_sources();
    const auto& sources2 = eds2.get_sources();

    assert(sources1.size() == sources2.size());
    for (size_t i = 0; i < sources1.size(); i++) {
        assert(sources1[i] == sources2[i]);
    }

    std::cout << "PASSED\n";
}

void test_save_sources_to_file() {
    std::cout << "Test 12: Save sources to file... ";

    // Create EDS with sources
    std::stringstream eds_ss("{A}{B,C}");
    std::stringstream seds_ss("{1}{2}{1,2}");
    biofmi::EDS eds(eds_ss, seds_ss);

    // Save to file
    std::filesystem::path temp_path = std::filesystem::temp_directory_path() / "test_seds_save.seds";
    eds.save_sources(temp_path);

    // Verify file exists and read it back
    assert(std::filesystem::exists(temp_path));
    std::ifstream ifs(temp_path);
    std::string content;
    std::getline(ifs, content);
    ifs.close();

    assert(content == "{1}{2}{1,2}");

    // Clean up
    std::filesystem::remove(temp_path);

    std::cout << "PASSED\n";
}

void test_load_sources_from_file() {
    std::cout << "Test 13: Load sources from file... ";

    // Create EDS without sources
    std::stringstream eds_ss("{AC}{,A,T}{GT}");
    biofmi::EDS eds(eds_ss);
    assert(!eds.has_sources());

    // Create a temporary file with sEDS content
    std::filesystem::path temp_path = std::filesystem::temp_directory_path() / "test_seds_load.seds";
    std::ofstream ofs(temp_path);
    ofs << "{0}{1}{2}{3}{0}";
    ofs.close();

    // Load sources from file
    eds.load_sources(temp_path);

    assert(eds.has_sources());
    const auto& sources = eds.get_sources();
    assert(sources.size() == 5);
    assert(sources[0].count(0) == 1);
    assert(sources[1].count(1) == 1);
    assert(sources[2].count(2) == 1);
    assert(sources[3].count(3) == 1);
    assert(sources[4].count(0) == 1);

    // Clean up
    std::filesystem::remove(temp_path);

    std::cout << "PASSED\n";
}

void test_roundtrip_sources_file() {
    std::cout << "Test 14: Roundtrip sources (save → load)... ";

    // Create original EDS with sources
    std::stringstream eds_ss1("{ACGT}{A,ACA}{CGT}");
    std::stringstream seds_ss("{0}{1,2}{3}{0}");
    biofmi::EDS eds1(eds_ss1, seds_ss);

    // Save sources to file
    std::filesystem::path temp_path = std::filesystem::temp_directory_path() / "test_seds_roundtrip.seds";
    eds1.save_sources(temp_path);

    // Create new EDS and load sources from file
    std::stringstream eds_ss2("{ACGT}{A,ACA}{CGT}");
    biofmi::EDS eds2(eds_ss2);
    eds2.load_sources(temp_path);

    // Compare
    const auto& sources1 = eds1.get_sources();
    const auto& sources2 = eds2.get_sources();

    assert(sources1.size() == sources2.size());
    for (size_t i = 0; i < sources1.size(); i++) {
        assert(sources1[i] == sources2[i]);
    }

    // Clean up
    std::filesystem::remove(temp_path);

    std::cout << "PASSED\n";
}

void test_load_sources_nonexistent_file() {
    std::cout << "Test 15: Load sources from nonexistent file (should fail)... ";

    std::stringstream eds_ss("{A}");
    biofmi::EDS eds(eds_ss);

    std::filesystem::path nonexistent = "/nonexistent/path/to/file.seds";

    bool caught = false;
    try {
        eds.load_sources(nonexistent);
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        caught = (msg.find("Failed to open") != std::string::npos);
    }

    assert(caught);
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "Running sEDS (source) parsing tests...\n\n";

    try {
        test_simple_sources();
        test_load_sources_separately();
        test_save_sources();
        test_sources_with_whitespace();
        test_all_paths_marker();
        test_invalid_cardinality_mismatch();
        test_invalid_empty_path_set();
        test_invalid_negative_path_id();
        test_invalid_missing_bracket();
        test_save_without_sources();
        test_roundtrip();
        test_save_sources_to_file();
        test_load_sources_from_file();
        test_roundtrip_sources_file();
        test_load_sources_nonexistent_file();

        std::cout << "\n✓ All source tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed: " << e.what() << "\n";
        return 1;
    }
}
