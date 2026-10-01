// Index building tests
#include "index/index.hpp"
#include <edsparser/formats/eds.hpp>
#include <sstream>
#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

// ---------------------------------------------------------------- .meta format
//
// .meta records the index format version and the SDSL flavour after its four
// counts, so an index from the other SDSL line fails by name instead of with
// "Width of int_vector<1> was specified as 0" from deep inside sdsl.

static std::filesystem::path save_small_index(const std::string& name) {
    std::stringstream ss("{AAAAA}{G,C,T}{TTTT}{A,C}{GGGG}{T,G,A}");
    edsparser::EDS eds(ss);
    std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    biofmi::BioFMI index(std::move(eds), 4);
    index.build();
    index.save(dir);
    return dir;
}

static std::vector<std::string> read_lines(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line); ) lines.push_back(line);
    return lines;
}

static void write_lines(const std::filesystem::path& p, const std::vector<std::string>& lines) {
    std::ofstream out(p, std::ios::trunc);
    for (const auto& l : lines) out << l << "\n";
}

// Load must throw, and its message must contain every one of `needles`.
static void expect_load_throws(const std::filesystem::path& dir,
                               const std::vector<std::string>& needles) {
    try {
        biofmi::BioFMI loaded(dir);
    } catch (const std::runtime_error& e) {
        const std::string what = e.what();
        for (const auto& n : needles) {
            if (what.find(n) == std::string::npos) {
                std::cerr << "\nmessage lacks '" << n << "': " << what << "\n";
                assert(false);
            }
        }
        return;
    }
    assert(false && "load() accepted an index it should have refused");
}

void test_meta_round_trip() {
    std::cout << "Test 5: .meta records format version and SDSL flavour... ";
    auto dir = save_small_index("biofmi_test_meta_rt");

    auto lines = read_lines(dir / "index.meta");
    assert(lines.size() == 6);
    assert(lines[0] == "4");
    assert(lines[4] == "format " + std::to_string(biofmi::BioFMI::index_format_version()));
    assert(lines[5] == "sdsl " + biofmi::BioFMI::sdsl_flavour());
    assert(biofmi::BioFMI::index_format_version() >= 2);

    biofmi::BioFMI loaded(dir);
    auto stats = loaded.get_statistics();
    assert(stats.context_length == 4);
    assert(stats.num_changes == 3);
    assert(loaded.count("AAAAAGTTTT") == 1);

    std::filesystem::remove_all(dir);
    std::cout << "PASSED\n";
}

void test_meta_legacy() {
    std::cout << "Test 6: legacy .meta (four integers) loads as format 1, SDSL v2... ";
    auto dir = save_small_index("biofmi_test_meta_legacy");
    auto lines = read_lines(dir / "index.meta");
    lines.resize(4);
    write_lines(dir / "index.meta", lines);

    if (biofmi::BioFMI::sdsl_flavour() == "v2") {
        // Exactly what a binary from before the field wrote: it must load.
        biofmi::BioFMI loaded(dir);
        assert(loaded.get_statistics().context_length == 4);
        assert(loaded.count("AAAAAGTTTT") == 1);
    } else {
        // A legacy index was necessarily written by SDSL v2, so another
        // flavour must refuse it by name rather than misread it.
        expect_load_throws(dir, {"SDSL v2", "predates", biofmi::BioFMI::sdsl_flavour()});
    }

    std::filesystem::remove_all(dir);
    std::cout << "PASSED\n";
}

void test_meta_mismatch() {
    std::cout << "Test 7: mismatched format version or SDSL flavour throws by name... ";
    auto dir = save_small_index("biofmi_test_meta_mismatch");
    const auto good = read_lines(dir / "index.meta");

    // A format newer than this binary reads.
    auto lines = good;
    const std::string future = std::to_string(biofmi::BioFMI::index_format_version() + 1);
    lines[4] = "format " + future;
    write_lines(dir / "index.meta", lines);
    expect_load_throws(dir, {"index format version " + future, "reads versions up to"});

    // The other SDSL line.
    const std::string other = biofmi::BioFMI::sdsl_flavour() == "v2" ? "v3" : "v2";
    lines = good;
    lines[5] = "sdsl " + other;
    write_lines(dir / "index.meta", lines);
    expect_load_throws(dir, {"SDSL " + other, "SDSL " + biofmi::BioFMI::sdsl_flavour(),
                             "Rebuild the index"});

    // A garbled version is a read error, not a silent legacy load.
    lines = good;
    lines[4] = "format x2";
    write_lines(dir / "index.meta", lines);
    expect_load_throws(dir, {"bad format version"});

    // And the untouched file still loads, so the throws above were the edits.
    write_lines(dir / "index.meta", good);
    biofmi::BioFMI loaded(dir);
    assert(loaded.get_statistics().context_length == 4);

    std::filesystem::remove_all(dir);
    std::cout << "PASSED\n";
}

int main() {
    std::cout << "=== BioFMI Index Building Tests ===\n\n";

    try {
        test_simple_index_build();
        test_index_save_load();
        test_index_with_file();
        test_complex_eds();
        test_meta_round_trip();
        test_meta_legacy();
        test_meta_mismatch();

        std::cout << "\n✓ All tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed with exception: " << e.what() << "\n";
        return 1;
    }
}
