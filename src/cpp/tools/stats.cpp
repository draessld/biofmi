#include "formats/eds.hpp"
#include "common.hpp"
#include <boost/program_options.hpp>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace po = boost::program_options;
using namespace biofmi;

// Format number with thousands separators
std::string format_number(size_t num) {
    std::stringstream ss;
    ss.imbue(std::locale(""));
    ss << std::fixed << num;
    return ss.str();
}

// Format file size in human-readable format
std::string format_size(uintmax_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_idx = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unit_idx < 4) {
        size /= 1024.0;
        unit_idx++;
    }

    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << size << " " << units[unit_idx];
    return ss.str();
}

// Estimate memory usage for FULL mode
size_t estimate_full_mode_memory(size_t N, size_t m, size_t n) {
    // Rough estimation:
    // - String data: N bytes (actual characters)
    // - String overhead: m * sizeof(std::string) ≈ m * 32 bytes
    // - StringSet overhead: n * sizeof(std::vector) ≈ n * 24 bytes
    // - Pointers and bookkeeping: ~20% overhead

    size_t string_data = N;
    size_t string_overhead = m * 32;
    size_t vector_overhead = n * 24;
    size_t bookkeeping = (string_data + string_overhead + vector_overhead) / 5;

    return string_data + string_overhead + vector_overhead + bookkeeping;
}

// Estimate memory usage for METADATA_ONLY mode
size_t estimate_metadata_memory(size_t m, size_t n) {
    // Metadata structure:
    // - base_positions: n * sizeof(streampos) ≈ n * 8 bytes
    // - symbol_sizes: n * sizeof(Length) ≈ n * 4 bytes
    // - string_lengths: m * sizeof(Length) ≈ m * 4 bytes
    // - cum_set_sizes: n * sizeof(Length) ≈ n * 4 bytes
    // - is_degenerate: n / 8 bytes (bit-packed, but using bool: n * 1)
    // - Statistics: ~64 bytes
    // - Overhead: ~10%

    size_t base_positions = n * 8;
    size_t symbol_sizes = n * 4;
    size_t string_lengths = m * 4;
    size_t cum_set_sizes = n * 4;
    size_t is_degenerate = n * 1;
    size_t statistics = 64;
    size_t total = base_positions + symbol_sizes + string_lengths + cum_set_sizes + is_degenerate + statistics;
    size_t overhead = total / 10;

    return total + overhead;
}

// Print statistics in standard format
void print_standard(const EDS& eds, const std::filesystem::path& input_file, bool verbose, bool has_sources_file) {
    auto stats = eds.get_statistics();
    auto metadata = eds.get_metadata();

    // Get file size
    uintmax_t file_size = std::filesystem::file_size(input_file);

    // Calculate memory estimates
    size_t metadata_mem = estimate_metadata_memory(eds.cardinality(), eds.length());
    size_t full_mem = estimate_full_mode_memory(eds.size(), eds.cardinality(), eds.length());
    double reduction_factor = static_cast<double>(full_mem) / static_cast<double>(metadata_mem);

    std::cout << "========================================\n";
    std::cout << "EDS Statistics\n";
    std::cout << "========================================\n";
    std::cout << "File: " << input_file.filename().string() << "\n";
    std::cout << "Size: " << format_size(file_size) << "\n";
    std::cout << "Storage Mode: " << (eds.get_storing_mode() == EDS::StoringMode::METADATA_ONLY ? "METADATA_ONLY (memory-efficient)" : "FULL (all data in RAM)") << "\n";
    std::cout << "\n";

    std::cout << "Structure:\n";
    std::cout << "  Number of symbols (n):        " << std::setw(12) << format_number(eds.length()) << "\n";
    std::cout << "  Total characters (N):         " << std::setw(12) << format_number(eds.size()) << "\n";
    std::cout << "  Total strings (m):            " << std::setw(12) << format_number(eds.cardinality()) << "\n";
    std::cout << "  Degenerate symbols:           " << std::setw(12) << format_number(stats.num_degenerate_symbols) << "\n";
    std::cout << "  Regular symbols:              " << std::setw(12) << format_number(eds.length() - stats.num_degenerate_symbols) << "\n";
    std::cout << "\n";

    std::cout << "Context Lengths (non-degenerate symbols):\n";
    std::cout << "  Minimum:                      " << std::setw(12) << stats.min_context_length << "\n";
    std::cout << "  Maximum:                      " << std::setw(12) << stats.max_context_length << "\n";
    std::cout << "  Average:                      " << std::setw(12) << std::fixed << std::setprecision(2) << stats.avg_context_length << "\n";
    std::cout << "\n";

    std::cout << "Variations:\n";
    std::cout << "  Total change size:            " << std::setw(12) << format_number(stats.total_change_size) << "\n";
    std::cout << "  Common characters:            " << std::setw(12) << format_number(stats.num_common_chars) << "\n";
    std::cout << "  Empty strings:                " << std::setw(12) << format_number(stats.num_empty_strings) << "\n";
    std::cout << "\n";

    if (verbose) {
        std::cout << "Detailed Metrics:\n";
        std::cout << "  Avg strings per symbol:       " << std::setw(12) << std::fixed << std::setprecision(2)
                  << (static_cast<double>(eds.cardinality()) / eds.length()) << "\n";
        std::cout << "  Avg chars per string:         " << std::setw(12) << std::fixed << std::setprecision(2)
                  << (static_cast<double>(eds.size()) / eds.cardinality()) << "\n";
        std::cout << "  Degenerate ratio:             " << std::setw(12) << std::fixed << std::setprecision(2)
                  << (100.0 * stats.num_degenerate_symbols / eds.length()) << " %\n";
        std::cout << "\n";
    }

    if (eds.has_sources()) {
        std::cout << "Sources (pangenome paths):\n";
        std::cout << "  Strings with source info:     " << std::setw(12) << format_number(eds.get_sources().size()) << "\n";
        std::cout << "  Total paths (genomes):        " << std::setw(12) << format_number(stats.num_paths) << "\n";
        std::cout << "  Max paths per string:         " << std::setw(12) << format_number(stats.max_paths_per_string) << "\n";
        std::cout << "  Avg paths per string:         " << std::setw(12) << std::fixed << std::setprecision(2) << stats.avg_paths_per_string << "\n";
        std::cout << "\n";
    } else if (has_sources_file) {
        std::cout << "Sources: File provided but parsing failed\n";
        std::cout << "\n";
    }

    std::cout << "Memory Usage:\n";
    std::cout << "  Current (" << (eds.get_storing_mode() == EDS::StoringMode::METADATA_ONLY ? "METADATA_ONLY" : "FULL") << "): "
              << std::setw(12) << format_size(eds.get_storing_mode() == EDS::StoringMode::METADATA_ONLY ? metadata_mem : full_mem) << "\n";
    if (eds.get_storing_mode() == EDS::StoringMode::METADATA_ONLY) {
        std::cout << "  Estimated FULL mode:          " << std::setw(12) << format_size(full_mem) << "\n";
        std::cout << "  Reduction factor:             " << std::setw(12) << std::fixed << std::setprecision(1) << reduction_factor << "x\n";
    }
    std::cout << "\n";

    // Recommendations
    std::cout << "Recommendations:\n";
    if (stats.min_context_length < 5) {
        std::cout << "  ⚠️  Minimum context length (" << stats.min_context_length << ") < typical l-EDS threshold (5)\n";
        std::cout << "  → Transformation to l-EDS may require merging adjacent symbols\n";
        std::cout << "  → Suggested command:\n";
        std::cout << "      biofmi transform -i " << input_file.filename().string() << " -l 5 --method linear\n";
    } else {
        std::cout << "  ✓ Minimum context length (" << stats.min_context_length << ") ≥ 5\n";
        std::cout << "  → Ready for BIO-FMI indexing with l ≤ " << stats.min_context_length << "\n";
        std::cout << "  → Suggested command:\n";
        std::cout << "      biofmi build -i " << input_file.filename().string() << " -l 5\n";
    }

    std::cout << "========================================\n";
}

// Print statistics in JSON format
void print_json(const EDS& eds, const std::filesystem::path& input_file, bool has_sources_file) {
    auto stats = eds.get_statistics();
    auto metadata = eds.get_metadata();
    uintmax_t file_size = std::filesystem::file_size(input_file);

    size_t metadata_mem = estimate_metadata_memory(eds.cardinality(), eds.length());
    size_t full_mem = estimate_full_mode_memory(eds.size(), eds.cardinality(), eds.length());
    double reduction_factor = static_cast<double>(full_mem) / static_cast<double>(metadata_mem);

    std::cout << "{\n";
    std::cout << "  \"file\": {\n";
    std::cout << "    \"path\": \"" << input_file.string() << "\",\n";
    std::cout << "    \"size_bytes\": " << file_size << ",\n";
    std::cout << "    \"storage_mode\": \"" << (eds.get_storing_mode() == EDS::StoringMode::METADATA_ONLY ? "METADATA_ONLY" : "FULL") << "\"\n";
    std::cout << "  },\n";
    std::cout << "  \"structure\": {\n";
    std::cout << "    \"n_symbols\": " << eds.length() << ",\n";
    std::cout << "    \"N_characters\": " << eds.size() << ",\n";
    std::cout << "    \"m_strings\": " << eds.cardinality() << ",\n";
    std::cout << "    \"degenerate_symbols\": " << stats.num_degenerate_symbols << ",\n";
    std::cout << "    \"regular_symbols\": " << (eds.length() - stats.num_degenerate_symbols) << "\n";
    std::cout << "  },\n";
    std::cout << "  \"context_lengths\": {\n";
    std::cout << "    \"min\": " << stats.min_context_length << ",\n";
    std::cout << "    \"max\": " << stats.max_context_length << ",\n";
    std::cout << "    \"avg\": " << std::fixed << std::setprecision(2) << stats.avg_context_length << "\n";
    std::cout << "  },\n";
    std::cout << "  \"variations\": {\n";
    std::cout << "    \"total_change_size\": " << stats.total_change_size << ",\n";
    std::cout << "    \"common_characters\": " << stats.num_common_chars << ",\n";
    std::cout << "    \"empty_strings\": " << stats.num_empty_strings << "\n";
    std::cout << "  },\n";
    std::cout << "  \"memory\": {\n";
    std::cout << "    \"current_bytes\": " << (eds.get_storing_mode() == EDS::StoringMode::METADATA_ONLY ? metadata_mem : full_mem) << ",\n";
    std::cout << "    \"current_mb\": " << std::fixed << std::setprecision(1) << ((eds.get_storing_mode() == EDS::StoringMode::METADATA_ONLY ? metadata_mem : full_mem) / 1024.0 / 1024.0) << ",\n";
    if (eds.get_storing_mode() == EDS::StoringMode::METADATA_ONLY) {
        std::cout << "    \"estimated_full_bytes\": " << full_mem << ",\n";
        std::cout << "    \"estimated_full_mb\": " << std::fixed << std::setprecision(1) << (full_mem / 1024.0 / 1024.0) << ",\n";
        std::cout << "    \"reduction_factor\": " << std::fixed << std::setprecision(1) << reduction_factor << "\n";
    } else {
        std::cout << "    \"mode\": \"FULL\"\n";
    }
    std::cout << "  },\n";
    std::cout << "  \"sources\": {\n";
    std::cout << "    \"loaded\": " << (eds.has_sources() ? "true" : "false") << ",\n";
    std::cout << "    \"file_provided\": " << (has_sources_file ? "true" : "false") << ",\n";
    if (eds.has_sources()) {
        std::cout << "    \"num_paths\": " << stats.num_paths << ",\n";
        std::cout << "    \"max_paths_per_string\": " << stats.max_paths_per_string << ",\n";
        std::cout << "    \"avg_paths_per_string\": " << std::fixed << std::setprecision(2) << stats.avg_paths_per_string << "\n";
    } else {
        std::cout << "    \"num_paths\": 0,\n";
        std::cout << "    \"max_paths_per_string\": 0,\n";
        std::cout << "    \"avg_paths_per_string\": 0.0\n";
    }
    std::cout << "  },\n";
    std::cout << "  \"recommendations\": {\n";
    std::cout << "    \"needs_transformation\": " << (stats.min_context_length < 5 ? "true" : "false") << ",\n";
    std::cout << "    \"ready_for_indexing\": " << (stats.min_context_length >= 5 ? "true" : "false") << ",\n";
    std::cout << "    \"min_context_length\": " << stats.min_context_length << ",\n";
    std::cout << "    \"suggested_command\": \"" << (stats.min_context_length < 5
                  ? "biofmi transform -i " + input_file.filename().string() + " -l 5"
                  : "biofmi build -i " + input_file.filename().string() + " -l 5") << "\"\n";
    std::cout << "  }\n";
    std::cout << "}\n";
}

int main(int argc, char** argv) {
    // Start performance tracking
    Timer timer;
    timer.start();

    // Helper to print performance info to stderr
    auto print_performance = [&timer]() {
        timer.stop();
        double runtime = timer.elapsed_seconds();
        double memory_mb = get_peak_memory_mb();
        std::cerr << "[Performance] Runtime: " << std::fixed << std::setprecision(2) << runtime << "s";
        if (memory_mb > 0.0) {
            std::cerr << " | Peak Memory: " << std::fixed << std::setprecision(1) << memory_mb << " MB";
        }
        std::cerr << "\n";
    };

    try {
        std::filesystem::path input_file;
        std::filesystem::path sources_file;
        bool use_full_mode = false;
        bool json_output = false;
        bool verbose = false;

        po::options_description desc("Display statistics for EDS/l-EDS file");
        desc.add_options()
            ("help,h", "Show help message")
            ("input,i", po::value<std::filesystem::path>(&input_file)->required(), "Input EDS file")
            ("sources,s", po::value<std::filesystem::path>(&sources_file), "Source file (.seds) - optional")
            ("full,f", po::bool_switch(&use_full_mode), "Use FULL mode (load all strings)")
            ("json,j", po::bool_switch(&json_output), "Output in JSON format")
            ("verbose,v", po::bool_switch(&verbose), "Show detailed statistics");

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);

        if (vm.count("help")) {
            std::cout << "biofmi-stats - Display EDS statistics\n\n";
            std::cout << desc << "\n";
            std::cout << "Examples:\n";
            std::cout << "  # Show statistics for EDS file (memory-efficient):\n";
            std::cout << "  biofmi-stats -i data.eds\n\n";
            std::cout << "  # Show statistics with sources:\n";
            std::cout << "  biofmi-stats -i data.eds -s data.seds\n\n";
            std::cout << "  # Show statistics in JSON format:\n";
            std::cout << "  biofmi-stats -i data.eds --json\n\n";
            std::cout << "  # Use FULL mode (loads all strings, more memory):\n";
            std::cout << "  biofmi-stats -i data.eds --full --verbose\n\n";
            std::cout << "Storage Modes:\n";
            std::cout << "  METADATA_ONLY (default): Uses ~10% memory of FULL mode, fast for large files\n";
            std::cout << "                           Sources are loaded as metadata (minimal memory impact)\n";
            std::cout << "  FULL (--full):           Loads all strings into RAM, enables detailed inspection\n";
            print_performance();
            return 0;
        }

        po::notify(vm);

        // Check if input file exists
        if (!std::filesystem::exists(input_file)) {
            std::cerr << "Error: Input file '" << input_file << "' not found\n";
            print_performance();
            return 1;
        }

        // Load EDS with appropriate mode
        EDS::StoringMode mode = use_full_mode ? EDS::StoringMode::FULL : EDS::StoringMode::METADATA_ONLY;

        EDS eds;
        if (vm.count("sources")) {
            if (!std::filesystem::exists(sources_file)) {
                std::cerr << "Error: Source file '" << sources_file << "' not found\n";
                print_performance();
                return 1;
            }
            // Load with sources (works in both FULL and METADATA_ONLY modes)
            eds = EDS::load(input_file, sources_file, mode);
        } else {
            eds = EDS::load(input_file, mode);
        }

        // Output statistics
        if (json_output) {
            print_json(eds, input_file, vm.count("sources") > 0);
        } else {
            print_standard(eds, input_file, verbose, vm.count("sources") > 0);
        }

        print_performance();
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        print_performance();
        return 1;
    }
}
