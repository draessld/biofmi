#include "index.hpp"
#include <boost/program_options.hpp>
#include <iostream>
#include <fstream>

namespace po = boost::program_options;
using namespace biofmi;

int main(int argc, char** argv) {
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
            return 0;
        }

        po::notify(vm);

        // TODO: Implement pattern searching
        std::cerr << "Locate tool not yet implemented\n";
        return 1;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
