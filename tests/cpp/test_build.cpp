// Index building tests
#include "index/index.hpp"
#include <edsparser/formats/eds.hpp>
#include <sstream>
#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>

void test_simple_index_build() {
    std::cout << "Test 1: Simple index building... ";

    // Create a simple l-EDS
    std::stringstream ss("{ACGT}{A,C}{TG}{G,T}");
    edsparser::EDS eds(ss);

    // Build index with context length 4
    biofmi::BioFMI index(std::move(eds), 4);
    index.build();

    // Verify statistics
    auto stats = index.get_statistics();
    assert(stats.context_length == 4);
    assert(stats.num_changes == 2);  // 2 degenerate symbols
    assert(stats.total_size_mb > 0);  // Index should have non-zero size

    std::cout << "PASSED\n";
}

void test_index_save_load() {
    std::cout << "Test 2: Index save and load... ";

    // Create a simple l-EDS
    std::stringstream ss("{ACGT}{A,C}{TG}{G,T}");
    edsparser::EDS eds(ss);

    // Build and save index
    std::filesystem::path test_dir = std::filesystem::temp_directory_path() / "biofmi_test_index";
    std::filesystem::remove_all(test_dir);  // Clean up any previous test

    biofmi::BioFMI index(std::move(eds), 4);
    index.build();
    index.save(test_dir);

    // Verify files were created
    assert(std::filesystem::exists(test_dir / "index.ri"));
    assert(std::filesystem::exists(test_dir / "index.ci"));
    assert(std::filesystem::exists(test_dir / "index.meta"));
    assert(std::filesystem::exists(test_dir / "index.loc"));
    assert(std::filesystem::exists(test_dir / "index.iloc"));
    assert(std::filesystem::exists(test_dir / "index.tloc"));

    // Load index back
    biofmi::BioFMI loaded_index(test_dir);
    auto stats = loaded_index.get_statistics();

    // Verify statistics match
    assert(stats.context_length == 4);
    assert(stats.num_changes == 2);

    // Clean up
    std::filesystem::remove_all(test_dir);

    std::cout << "PASSED\n";
}

void test_index_with_file() {
    std::cout << "Test 3: Index building from file... ";

    // Create a temporary l-EDS file
    std::filesystem::path test_file = std::filesystem::temp_directory_path() / "test.leds";
    std::ofstream ofs(test_file);
    ofs << "{ACGT}{A,C}{TG}{G,T}";
    ofs.close();

    // Build index from file
    biofmi::BioFMI index(test_file, 4);
    index.build();

    // Verify statistics
    auto stats = index.get_statistics();
    assert(stats.context_length == 4);
    assert(stats.reference_length > 0);

    // Clean up
    std::filesystem::remove(test_file);
    std::filesystem::remove_all(test_file.parent_path() / (test_file.stem().string() + ".index"));

    std::cout << "PASSED\n";
}

void test_complex_eds() {
    std::cout << "Test 4: Index with more complex EDS... ";

    // Create a more complex l-EDS
    std::stringstream ss("{AAAAA}{G,C,T}{TTTT}{A,C}{GGGG}{T,G,A}");
    edsparser::EDS eds(ss);

    // Build index
    biofmi::BioFMI index(std::move(eds), 4);
    index.build();

    // Verify statistics
    auto stats = index.get_statistics();
    assert(stats.context_length == 4);
    assert(stats.num_changes == 3);  // 3 degenerate symbols
    assert(stats.reference_length == 13);  // AAAAA + TTTT + GGGG = 13

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "=== BioFMI Index Building Tests ===\n\n";

    try {
        test_simple_index_build();
        test_index_save_load();
        test_index_with_file();
        test_complex_eds();

        std::cout << "\n✓ All tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed with exception: " << e.what() << "\n";
        return 1;
    }
}
