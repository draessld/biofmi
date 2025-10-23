#include "index.hpp"
#include <boost/program_options.hpp>
#include <iostream>
#include <fstream>

namespace po = boost::program_options;
using namespace biofmi;

int main(int argc, char** argv) {
    try {
        std::filesystem::path input_file;
        std::filesystem::path output_file;
        Length context_length;

        po::options_description desc("Build BIO-FMI index");
        desc.add_options()
            ("help,h", "Show help message")
            ("input,i", po::value<std::filesystem::path>(&input_file)->required(), "Input l-EDS file")
            ("output,o", po::value<std::filesystem::path>(&output_file), "Output index directory")
            ("context-length,l", po::value<Length>(&context_length)->required(), "Context length");

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);

        if (vm.count("help")) {
            std::cout << desc << "\n";
            return 0;
        }

        po::notify(vm);

        // TODO: Implement index building
        std::cerr << "Build tool not yet implemented\n";
        return 1;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
