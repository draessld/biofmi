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
        std::filesystem::path input_file;
        std::filesystem::path output_file;
        Length context_length;
        bool dump_readable;

        po::options_description desc("Build BIO-FMI index");
        desc.add_options()
            ("help,h", "Show help message")
            ("input,i", po::value<std::filesystem::path>(&input_file)->required(), "Input l-EDS file")
            ("output,o", po::value<std::filesystem::path>(&output_file), "Output index directory")
            ("context-length,l", po::value<Length>(&context_length)->required(), "Context length")
            ("dump", po::bool_switch(&dump_readable)->default_value(false),
             "Write human-readable dump of index internals to <output>/index.dump.txt");

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);

        if (vm.count("help")) {
            std::cout << desc << "\n";
            print_performance();
            return 0;
        }

        po::notify(vm);

        // Set output directory (default: input_file.index)
        if (!vm.count("output")) {
            std::string filename = input_file.filename().replace_extension("");
            output_file = input_file.parent_path() / (filename + ".index");
        }

        std::cout << "Building BIO-FMI index...\n";
        std::cout << "  Input file: " << input_file << "\n";
        std::cout << "  Output directory: " << output_file << "\n";
        std::cout << "  Context length: " << context_length << "\n\n";

        // Load l-EDS from file
        std::cout << "Loading l-EDS..." << std::flush;
        EDS eds = EDS::load(input_file);
        std::cout << " done\n";

        // Validate l-EDS property: every INTERNAL context segment (one with a
        // degenerate symbol on both its left and right sides) must have length
        // >= l. Boundary segments at the very start or end of the EDS may be
        // shorter and are handled correctly by the index builder.
        //
        // A segment is a maximal run of regular symbols, not one symbol:
        // {CGCG}{A}{TGCC} is one context of 9 (EDS::Metadata, edsparser). Two
        // degenerate symbols with nothing between them are an internal
        // segment of length 0, which no l satisfies.
        std::cout << "Validating l-EDS property..." << std::flush;
        const auto& metadata = eds.get_metadata();
        const size_t n_sym = metadata.is_degenerate.size();
        bool seen_degenerate = false;
        for (size_t i = 0; i < n_sym; ) {
            if (metadata.is_degenerate[i]) {
                if (i > 0 && metadata.is_degenerate[i - 1]) {
                    std::cerr << "\nError: Input EDS does not satisfy l-EDS property\n";
                    std::cerr << "  Degenerate symbols " << i - 1 << " and " << i
                              << " are adjacent: an internal context of length 0"
                              << " (< required " << context_length << ")\n";
                    std::cerr << "  Please transform the EDS first using 'eds2leds -l "
                              << context_length << "'\n";
                    print_performance();
                    return 1;
                }
                seen_degenerate = true;
                ++i;
                continue;
            }
            size_t j = i, len = 0;
            while (j < n_sym && !metadata.is_degenerate[j]) {
                len += metadata.string_lengths[metadata.cum_set_sizes[j]];
                ++j;
            }
            if (seen_degenerate && j < n_sym && len < (size_t)context_length) {
                std::cerr << "\nError: Input EDS does not satisfy l-EDS property\n";
                std::cerr << "  Internal context at symbol" << (j - i > 1 ? "s " : " ") << i;
                if (j - i > 1) std::cerr << "-" << j - 1;
                std::cerr << " has length " << len
                          << " (< required " << context_length << ")\n";
                std::cerr << "  Please transform the EDS first using 'eds2leds -l "
                          << context_length << "'\n";
                print_performance();
                return 1;
            }
            i = j;
        }
        std::cout << " done\n\n";

        // Build index
        BioFMI index(std::move(eds), context_length);
        index.build();

        // Save index
        std::cout << "\nSaving index to disk..." << std::flush;
        index.save(output_file);
        std::cout << " done\n\n";

        // Print statistics
        index.print_statistics();

        if (dump_readable) {
            std::filesystem::path dump_path = output_file / "index.dump.txt";
            std::cout << "Dumping human-readable index to " << dump_path << "..." << std::flush;
            index.dump_readable(dump_path);
            std::cout << " done\n";
        }

        std::cout << "\nIndex successfully built and saved to " << output_file << "\n";
        print_performance();
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        print_performance();
        return 1;
    }
}
