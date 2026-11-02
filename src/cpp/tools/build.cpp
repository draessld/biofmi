#include "index/index.hpp"
#include "version.hpp"
#include <edsparser/common.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <limits>

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
        long long context_length_arg;
        Length context_length;
        bool dump_readable;

        po::options_description desc("Build EDS-BIO-FMI index");
        desc.add_options()
            ("help,h", "Show help message")
            ("version,V", "Print version and build provenance (COMMIT, COMMIT_DATE, DIRTY) and exit")
            ("input,i", po::value<std::filesystem::path>(&input_file)->required(), "Input l-EDS file")
            ("output,o", po::value<std::filesystem::path>(&output_file), "Output index directory")
            ("context-length,l", po::value<long long>(&context_length_arg)->required(),
             "Context length l >= 1")
            ("dump", po::bool_switch(&dump_readable)->default_value(false),
             "Write human-readable dump of index internals to <output>/index.dump.txt");

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);

        if (vm.count("help")) {
            std::cout << desc << "\n";
            print_performance();
            return 0;
        }
        if (vm.count("version")) {
            biofmi::print_version("eds-biofmi-build");
            return 0;
        }

        po::notify(vm);

        // Parsed signed: boost reads "-1" into an unsigned as 4294967295,
        // which then failed as bad_alloc on padding contexts that long.
        if (context_length_arg < 1 || context_length_arg > std::numeric_limits<Length>::max()) {
            std::cerr << "Error: context length -l must be in 1.."
                      << std::numeric_limits<Length>::max() << ", got " << context_length_arg << "\n";
            print_performance();
            return 1;
        }
        context_length = static_cast<Length>(context_length_arg);

        // Set output directory (default: input_file.index)
        if (!vm.count("output")) {
            std::string filename = input_file.filename().replace_extension("");
            output_file = input_file.parent_path() / (filename + ".index");
        }

        std::cout << "Building EDS-BIO-FMI index...\n";
        std::cout << "  Input file: " << input_file << "\n";
        std::cout << "  Output directory: " << output_file << "\n";
        std::cout << "  Context length: " << context_length << "\n\n";

        // Load l-EDS from file
        std::cout << "Loading l-EDS..." << std::flush;
        EDS eds = EDS::load(input_file);
        std::cout << " done\n";

        // Validate the l-EDS property (BioFMI::check_leds): every internal
        // segment — a maximal run of regular symbols with a degenerate symbol
        // on both sides — at least l long, no two degenerate symbols adjacent.
        // build() checks it again; asking first is only for the hint below.
        std::cout << "Validating l-EDS property..." << std::flush;
        try {
            BioFMI::check_leds(eds, context_length);
        } catch (const std::invalid_argument& e) {
            std::cerr << "\nError: " << e.what() << "\n";
            std::cerr << "  Please transform the EDS first using 'eds2leds -l "
                      << context_length << "'\n";
            print_performance();
            return 1;
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
