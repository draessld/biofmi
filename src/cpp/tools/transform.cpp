#include "utils.hpp"
#include <boost/program_options.hpp>
#include <iostream>
#include <fstream>

namespace po = boost::program_options;
using namespace biofmi;

int main(int argc, char** argv) {
    try {
        std::filesystem::path input_file;
        std::filesystem::path output_file;
        std::filesystem::path sources_file;
        Length context_length;
        std::string method;

        po::options_description desc("Transform MSA/VCF/EDS to l-EDS format");
        desc.add_options()
            ("help,h", "Show help message")
            ("input,i", po::value<std::filesystem::path>(&input_file)->required(), "Input file")
            ("output,o", po::value<std::filesystem::path>(&output_file), "Output file")
            ("context-length,l", po::value<Length>(&context_length)->default_value(5), "Context length")
            ("method", po::value<std::string>(&method)->default_value("linear"), "Method: linear or cartesian")
            ("sources,s", po::value<std::filesystem::path>(&sources_file), "Source file for phasing (.edp)");

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);

        if (vm.count("help")) {
            std::cout << desc << "\n";
            return 0;
        }

        po::notify(vm);

        // TODO: Implement transformation
        std::cerr << "Transform tool not yet implemented\n";
        return 1;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
