#include "index/index.hpp"
#include <edsparser/common.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <fstream>
#include <iomanip>

namespace po = boost::program_options;
using namespace biofmi;
using edsparser::Timer;
using edsparser::get_peak_memory_mb;

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
        std::filesystem::path index_file;
        std::filesystem::path pattern_file;
        std::filesystem::path output_file;
        std::string pattern;
        Length context_length;
        bool benchmark = false;

        po::options_description desc("Locate patterns in BIO-FMI index");
        desc.add_options()
            ("help,h", "Show help message")
            ("index,i", po::value<std::filesystem::path>(&index_file)->required(), "Index file or directory")
            ("context-length,l", po::value<Length>(&context_length)->required(), "Context length")
            ("pattern,p", po::value<std::string>(&pattern), "Single pattern to search")
            ("pattern-file,P", po::value<std::filesystem::path>(&pattern_file), "File with patterns")
            ("output,o", po::value<std::filesystem::path>(&output_file), "Output file")
            ("benchmark", po::bool_switch(&benchmark), "Benchmark mode");

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);

        if (vm.count("help")) {
            std::cout << desc << "\n";
            print_performance();
            return 0;
        }

        po::notify(vm);

        // Validate inputs
        if (!vm.count("pattern") && !vm.count("pattern-file")) {
            std::cerr << "Error: Must provide either --pattern or --pattern-file\n";
            print_performance();
            return 1;
        }

        // Load index
        std::cerr << "Loading index from " << index_file << "...\n";
        BioFMI index(index_file);

        // Read pattern(s)
        std::vector<std::string> patterns;
        if (vm.count("pattern")) {
            patterns.push_back(pattern);
        } else if (vm.count("pattern-file")) {
            std::ifstream pfile(pattern_file);
            if (!pfile.is_open()) {
                std::cerr << "Error: Cannot open pattern file: " << pattern_file << "\n";
                print_performance();
                return 1;
            }
            std::string line;
            while (std::getline(pfile, line)) {
                if (!line.empty()) {
                    patterns.push_back(line);
                }
            }
            pfile.close();
        }

        // Open output file if specified
        std::ostream* out = &std::cout;
        std::ofstream outfile;
        if (vm.count("output")) {
            outfile.open(output_file);
            if (!outfile.is_open()) {
                std::cerr << "Error: Cannot open output file: " << output_file << "\n";
                print_performance();
                return 1;
            }
            out = &outfile;
        }

        // Search each pattern
        size_t total_occurrences = 0;
        for (const auto& p : patterns) {
            try {
                auto result = index.locate(p);

                if (!benchmark) {
                    *out << "Pattern: " << p << "\n";
                    if (result.empty()) {
                        *out << "No occurrences found\n";
                    } else {
                        index.print_result(result, *out);
                    }
                    *out << "\n";
                } else {
                    // Benchmark mode: just count occurrences
                    size_t count = 0;
                    for (const auto& [seq_id, occs] : result) {
                        count += occs.size();
                    }
                    *out << p << "\t" << count << "\n";
                    total_occurrences += count;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error searching pattern '" << p << "': " << e.what() << "\n";
            }
        }

        if (benchmark) {
            std::cerr << "Total patterns: " << patterns.size() << "\n";
            std::cerr << "Total occurrences: " << total_occurrences << "\n";
        }

        if (outfile.is_open()) {
            outfile.close();
        }

        print_performance();
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        print_performance();
        return 1;
    }
}
