#include "eds.hpp"
#include <boost/program_options.hpp>
#include <iostream>
#include <fstream>
#include <filesystem>

namespace po = boost::program_options;
using namespace biofmi;

int main(int argc, char** argv) {
    try {
        std::filesystem::path input_file;
        std::filesystem::path output_file;
        size_t count;
        Length length;

        po::options_description desc("Generate random patterns from EDS");
        desc.add_options()
            ("help,h", "Show help message")
            ("input,i", po::value<std::filesystem::path>(&input_file)->required(), "Input EDS file")
            ("output,o", po::value<std::filesystem::path>(&output_file)->required(), "Output pattern file")
            ("count,n", po::value<size_t>(&count)->default_value(100), "Number of patterns")
            ("length,l", po::value<Length>(&length)->default_value(10), "Pattern length");

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);

        if (vm.count("help")) {
            std::cout << desc << "\n";
            return 0;
        }

        po::notify(vm);

        // Validate input file exists
        if (!std::filesystem::exists(input_file)) {
            std::cerr << "Error: Input file does not exist: " << input_file << "\n";
            return 1;
        }

        // Validate parameters
        if (count == 0) {
            std::cerr << "Error: Pattern count must be greater than 0\n";
            return 1;
        }

        if (length == 0) {
            std::cerr << "Error: Pattern length must be greater than 0\n";
            return 1;
        }

        // Load EDS in FULL mode (required for pattern generation)
        std::cerr << "Loading EDS file: " << input_file << "\n";
        EDS eds = EDS::load(input_file, EDS::StoringMode::FULL);

        if (eds.empty()) {
            std::cerr << "Error: Cannot generate patterns from empty EDS\n";
            return 1;
        }

        std::cerr << "Loaded EDS with " << eds.length() << " symbols, "
                  << eds.cardinality() << " strings\n";

        // Check if pattern length is reasonable
        if (length > eds.size()) {
            std::cerr << "Warning: Pattern length (" << length
                      << ") is greater than total EDS size (" << eds.size() << ")\n";
            std::cerr << "Patterns may be truncated or generation may fail\n";
        }

        // Open output file
        std::ofstream outfile(output_file);
        if (!outfile) {
            std::cerr << "Error: Cannot open output file: " << output_file << "\n";
            return 1;
        }

        // Generate patterns
        std::cerr << "Generating " << count << " patterns of length " << length << "...\n";
        eds.generate_patterns(outfile, count, length);

        std::cerr << "Successfully generated " << count << " patterns\n";
        std::cerr << "Output written to: " << output_file << "\n";

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
