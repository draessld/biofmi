// Statistics tests - Testing EDS statistics calculation and source statistics
#include "formats/eds.hpp"
#include <sstream>
#include <iostream>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>

using namespace biofmi;

void test_basic_statistics() {
    std::cout << "Test 1: Basic statistics calculation... ";

    // EDS: {ACGT}{A,ACA}{CGT}{T,TG}
    // Context lengths: 4 (ACGT), 3 (CGT) -> min=3, max=4, avg=3.5
    std::stringstream ss("{ACGT}{A,ACA}{CGT}{T,TG}");
    EDS eds(ss);

    auto stats = eds.get_statistics();

    assert(stats.min_context_length == 3);
    assert(stats.max_context_length == 4);
    assert(std::abs(stats.avg_context_length - 3.5) < 0.01);
    assert(stats.num_degenerate_symbols == 2);  // positions 1 and 3
    assert(stats.num_common_chars == 7);        // ACGT (4) + CGT (3)
    assert(stats.total_change_size == 7);       // A(1) + ACA(3) + T(1) + TG(2)
    assert(stats.num_empty_strings == 0);

    std::cout << "PASSED\n";
}

void test_empty_string_statistics() {
    std::cout << "Test 2: Statistics with empty strings... ";

    // EDS: {AC}{,A,T}{GT}
    // Empty string counts toward statistics
    std::stringstream ss("{AC}{,A,T}{GT}");
    EDS eds(ss);

    auto stats = eds.get_statistics();

    assert(stats.num_empty_strings == 1);
    assert(stats.min_context_length == 2);  // Both AC and GT have length 2
    assert(stats.max_context_length == 2);
    assert(stats.num_degenerate_symbols == 1);

    std::cout << "PASSED\n";
}

void test_all_degenerate() {
    std::cout << "Test 3: Statistics with all degenerate symbols... ";

    // EDS: {A,T}{C,G}{A,T}
    // No common blocks, context should be 0
    std::stringstream ss("{A,T}{C,G}{A,T}");
    EDS eds(ss);

    auto stats = eds.get_statistics();

    assert(stats.min_context_length == 0);
    assert(stats.max_context_length == 0);
    assert(stats.avg_context_length == 0.0);
    assert(stats.num_degenerate_symbols == 3);
    assert(stats.num_common_chars == 0);

    std::cout << "PASSED\n";
}

void test_metadata_statistics() {
    std::cout << "Test 4: Statistics from metadata (METADATA_ONLY mode)... ";

    // Create temporary file
    std::filesystem::path temp_file = "test_metadata_stats.eds";
    {
        std::ofstream ofs(temp_file);
        ofs << "{AAAA}{G,GG}{TTTT}{C,CC}";
    }

    // Load in METADATA_ONLY mode
    EDS eds = EDS::load(temp_file, EDS::StoringMode::METADATA_ONLY);
    auto stats = eds.get_statistics();

    // Context lengths: AAAA (4), TTTT (4) -> min=4, max=4, avg=4.0
    assert(stats.min_context_length == 4);
    assert(stats.max_context_length == 4);
    assert(std::abs(stats.avg_context_length - 4.0) < 0.01);
    assert(stats.num_degenerate_symbols == 2);
    assert(stats.num_common_chars == 8);  // AAAA (4) + TTTT (4)

    // Cleanup
    std::filesystem::remove(temp_file);

    std::cout << "PASSED\n";
}

void test_source_statistics_basic() {
    std::cout << "Test 5: Basic source statistics... ";

    // EDS: {ACGT}{A,ACA}{CGT}{T,TG}
    // sEDS: {0}{1,3}{2}{4,5}  - strings: ACGT, A, ACA, CGT, T, TG
    std::stringstream eds_ss("{ACGT}{A,ACA}{CGT}{T,TG}");
    std::stringstream seds_ss("{0}{1,3}{2}{4,5}{6}{7}");

    EDS eds(eds_ss, seds_ss);
    auto stats = eds.get_statistics();

    // 8 distinct paths: 0, 1, 2, 3, 4, 5, 6, 7
    assert(stats.num_paths == 8);

    // Max paths per string: {1,3} has 2 paths
    assert(stats.max_paths_per_string == 2);

    // Average: (1+2+1+2+1+1)/6 = 8/6 = 1.33...
    assert(std::abs(stats.avg_paths_per_string - 1.333) < 0.01);

    std::cout << "PASSED\n";
}

void test_source_statistics_all_universal() {
    std::cout << "Test 6: Source statistics with universal paths... ";

    // All strings have {0} (universal)
    std::stringstream eds_ss("{AC}{GT}");
    std::stringstream seds_ss("{0}{0}");

    EDS eds(eds_ss, seds_ss);
    auto stats = eds.get_statistics();

    // Only path 0 is used
    assert(stats.num_paths == 1);
    assert(stats.max_paths_per_string == 1);
    assert(std::abs(stats.avg_paths_per_string - 1.0) < 0.01);

    std::cout << "PASSED\n";
}

void test_source_statistics_single_string_multi_paths() {
    std::cout << "Test 7: Source statistics with single string having multiple paths... ";

    // One string with many paths
    std::stringstream eds_ss("{ACGT}");
    std::stringstream seds_ss("{1,2,3,4,5}");

    EDS eds(eds_ss, seds_ss);
    auto stats = eds.get_statistics();

    assert(stats.num_paths == 5);
    assert(stats.max_paths_per_string == 5);
    assert(std::abs(stats.avg_paths_per_string - 5.0) < 0.01);

    std::cout << "PASSED\n";
}

void test_source_statistics_file_mode() {
    std::cout << "Test 8: Source statistics from file (FULL mode)... ";

    // Create temporary files
    std::filesystem::path eds_file = "test_source_stats.eds";
    std::filesystem::path seds_file = "test_source_stats.seds";

    {
        std::ofstream eds_ofs(eds_file);
        eds_ofs << "{ACGT}{A,ACA}{CGT}";

        std::ofstream seds_ofs(seds_file);
        seds_ofs << "{0}{1,2}{3}{4,5}";
    }

    // Load with sources in FULL mode
    EDS eds = EDS::load(eds_file, seds_file, EDS::StoringMode::FULL);
    auto stats = eds.get_statistics();

    // Paths: 0, 1, 2, 3, 4, 5 = 6 distinct paths
    assert(stats.num_paths == 6);

    // Max: {1,2} has 2, {4,5} has 2
    assert(stats.max_paths_per_string == 2);

    // Average: (1+2+1+2+1)/5 = 7/5 = 1.4
    assert(std::abs(stats.avg_paths_per_string - 1.4) < 0.01);

    // Cleanup
    std::filesystem::remove(eds_file);
    std::filesystem::remove(seds_file);

    std::cout << "PASSED\n";
}

void test_statistics_without_sources() {
    std::cout << "Test 9: Statistics without sources should have zero source stats... ";

    std::stringstream ss("{ACGT}{A,ACA}{CGT}");
    EDS eds(ss);

    auto stats = eds.get_statistics();

    // Source statistics should be zero when no sources loaded
    assert(stats.num_paths == 0);
    assert(stats.max_paths_per_string == 0);
    assert(stats.avg_paths_per_string == 0.0);

    std::cout << "PASSED\n";
}

void test_metadata_preservation() {
    std::cout << "Test 10: Metadata contains all statistics fields... ";

    std::stringstream eds_ss("{ACGT}{A,T}{GGG}");
    std::stringstream seds_ss("{0}{1,2}{3}{4}");

    EDS eds(eds_ss, seds_ss);
    auto metadata = eds.get_metadata();

    // Check all fields exist and are accessible
    assert(metadata.min_context_length > 0);
    assert(metadata.max_context_length > 0);
    assert(metadata.avg_context_length > 0);
    assert(metadata.num_degenerate_symbols >= 0);
    assert(metadata.num_common_chars > 0);
    assert(metadata.total_change_size >= 0);
    assert(metadata.num_empty_strings >= 0);

    // Source statistics
    assert(metadata.num_paths > 0);
    assert(metadata.max_paths_per_string > 0);
    assert(metadata.avg_paths_per_string > 0);

    std::cout << "PASSED\n";
}

void test_large_path_numbers() {
    std::cout << "Test 11: Source statistics with large path numbers... ";

    // Test with path numbers like 100, 200, etc.
    std::stringstream eds_ss("{A}{T}");
    std::stringstream seds_ss("{100,200,300}{400,500}");

    EDS eds(eds_ss, seds_ss);
    auto stats = eds.get_statistics();

    // 5 distinct paths: 100, 200, 300, 400, 500
    assert(stats.num_paths == 5);
    assert(stats.max_paths_per_string == 3);  // First string has 3 paths

    // Average: (3+2)/2 = 2.5
    assert(std::abs(stats.avg_paths_per_string - 2.5) < 0.01);

    std::cout << "PASSED\n";
}

void test_single_path_coverage() {
    std::cout << "Test 12: Source statistics with overlapping paths... ";

    // Same path appears in multiple strings
    std::stringstream eds_ss("{A}{T}{G}");
    std::stringstream seds_ss("{1}{1,2}{1}");

    EDS eds(eds_ss, seds_ss);
    auto stats = eds.get_statistics();

    // Only 2 distinct paths even though path 1 appears 3 times
    assert(stats.num_paths == 2);
    assert(stats.max_paths_per_string == 2);

    // Average: (1+2+1)/3 = 4/3 = 1.33...
    assert(std::abs(stats.avg_paths_per_string - 1.333) < 0.01);

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "===========================================\n";
    std::cout << "Running Statistics Tests\n";
    std::cout << "===========================================\n\n";

    try {
        // Basic statistics tests
        test_basic_statistics();
        test_empty_string_statistics();
        test_all_degenerate();
        test_metadata_statistics();

        // Source statistics tests
        test_source_statistics_basic();
        test_source_statistics_all_universal();
        test_source_statistics_single_string_multi_paths();
        test_source_statistics_file_mode();
        test_statistics_without_sources();

        // Integration tests
        test_metadata_preservation();
        test_large_path_numbers();
        test_single_path_coverage();

        std::cout << "\n===========================================\n";
        std::cout << "All statistics tests PASSED! ✓\n";
        std::cout << "===========================================\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\n\nFATAL ERROR: " << e.what() << "\n";
        return 1;
    }
}
