#include "eds.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>

using namespace biofmi;

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            std::cerr << "Usage: " << argv[0] << " <eds-file>\n";
            return 1;
        }

        std::filesystem::path input_file(argv[1]);

        // TODO: Implement statistics display
        std::cerr << "Stats tool not yet implemented\n";
        return 1;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
