#include "utils.hpp"
#include <boost/program_options.hpp>
#include <iostream>
#include <fstream>

namespace po = boost::program_options;
using namespace biofmi;

// ===== FILE TYPE DETECTION =====

enum class FileType {
    MSA,
    VCF,
    EDS,
    SEDS,
    UNKNOWN
};

FileType detect_file_type(const std::filesystem::path& file_path) {
    std::string extension = file_path.extension().string();

    if (extension == ".msa") {
        return FileType::MSA;
    } else if (extension == ".vcf") {
        return FileType::VCF;
    } else if (extension == ".eds") {
        return FileType::EDS;
    } else if (extension == ".seds") {
        return FileType::SEDS;
    } else {
        return FileType::UNKNOWN;
    }
}

std::string file_type_to_string(FileType type) {
    switch (type) {
        case FileType::MSA: return "MSA";
        case FileType::VCF: return "VCF";
        case FileType::EDS: return "EDS";
        case FileType::SEDS: return "SEDS";
        case FileType::UNKNOWN: return "UNKNOWN";
        default: return "UNKNOWN";
    }
}

// ===== TRANSFORMATION FUNCTIONS (STUBS) =====

// MSA → EDS (with/without sources based on sources_file)
void transform_msa_to_eds(
    const std::filesystem::path& input_file,
    const std::filesystem::path& output_file,
    const std::filesystem::path& sources_file,
    const std::string& method
) {
    std::cerr << "MSA → EDS transformation not yet implemented\n";
    std::cerr << "  Input: " << input_file << "\n";
    std::cerr << "  Output: " << output_file << "\n";
    if (!sources_file.empty()) {
        std::cerr << "  Sources: " << sources_file << " (method: " << method << ")\n";
    } else {
        std::cerr << "  Sources: none (simple EDS)\n";
    }
    throw std::runtime_error("MSA → EDS transformation not implemented");
}

// MSA → l-EDS (direct transformation with merging)
void transform_msa_to_leds(
    const std::filesystem::path& input_file,
    const std::filesystem::path& output_file,
    const std::filesystem::path& sources_file,
    Length context_length,
    const std::string& method
) {
    std::cerr << "MSA → l-EDS transformation not yet implemented\n";
    std::cerr << "  Input: " << input_file << "\n";
    std::cerr << "  Output: " << output_file << "\n";
    std::cerr << "  Context length: " << context_length << "\n";
    if (!sources_file.empty()) {
        std::cerr << "  Sources: " << sources_file << " (method: " << method << ")\n";
    } else {
        std::cerr << "  Sources: none (CARTESIAN merge)\n";
    }
    throw std::runtime_error("MSA → l-EDS transformation not implemented");
}

// VCF → EDS (with/without sources based on sources_file)
void transform_vcf_to_eds(
    const std::filesystem::path& input_file,
    const std::filesystem::path& output_file,
    const std::filesystem::path& sources_file,
    const std::string& method
) {
    std::cerr << "VCF → EDS transformation not yet implemented\n";
    std::cerr << "  Input: " << input_file << "\n";
    std::cerr << "  Output: " << output_file << "\n";
    if (!sources_file.empty()) {
        std::cerr << "  Sources: " << sources_file << " (method: " << method << ")\n";
    } else {
        std::cerr << "  Sources: none (simple EDS)\n";
    }
    throw std::runtime_error("VCF → EDS transformation not implemented");
}

// VCF → l-EDS (direct transformation with merging)
void transform_vcf_to_leds(
    const std::filesystem::path& input_file,
    const std::filesystem::path& output_file,
    const std::filesystem::path& sources_file,
    Length context_length,
    const std::string& method
) {
    std::cerr << "VCF → l-EDS transformation not yet implemented\n";
    std::cerr << "  Input: " << input_file << "\n";
    std::cerr << "  Output: " << output_file << "\n";
    std::cerr << "  Context length: " << context_length << "\n";
    if (!sources_file.empty()) {
        std::cerr << "  Sources: " << sources_file << " (method: " << method << ")\n";
    } else {
        std::cerr << "  Sources: none (CARTESIAN merge)\n";
    }
    throw std::runtime_error("VCF → l-EDS transformation not implemented");
}

// EDS → l-EDS (merge existing EDS to satisfy length constraint)
void transform_eds_to_leds(
    const std::filesystem::path& input_file,
    const std::filesystem::path& output_file,
    const std::filesystem::path& sources_file,
    Length context_length,
    const std::string& method
) {
    std::cerr << "EDS → l-EDS transformation not yet implemented\n";
    std::cerr << "  Input: " << input_file << "\n";
    std::cerr << "  Output: " << output_file << "\n";
    std::cerr << "  Context length: " << context_length << "\n";
    std::cerr << "  Method: " << method << " (LINEAR=with sources, CARTESIAN=without)\n";
    if (!sources_file.empty()) {
        std::cerr << "  Sources: " << sources_file << "\n";
    }
    throw std::runtime_error("EDS → l-EDS transformation not implemented");
}

// ===== MAIN =====

int main(int argc, char** argv) {
    try {
        std::filesystem::path input_file;
        std::filesystem::path output_file;
        std::filesystem::path sources_file;
        Length context_length;
        std::string method;

        po::options_description desc("Transform MSA/VCF/EDS to EDS/l-EDS format");
        desc.add_options()
            ("help,h", "Show help message")
            ("input,i", po::value<std::filesystem::path>(&input_file)->required(), "Input file (.msa, .vcf, .eds)")
            ("output,o", po::value<std::filesystem::path>(&output_file), "Output file")
            ("context-length,l", po::value<Length>(&context_length)->default_value(0), "Context length (0 = no merging, >0 = create l-EDS)")
            ("method", po::value<std::string>(&method)->default_value("linear"), "Method: linear or cartesian")
            ("sources,s", po::value<std::filesystem::path>(&sources_file), "Source file for phasing (.seds)");

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);

        if (vm.count("help")) {
            std::cout << desc << "\n";
            std::cout << "\nSupported transformations:\n";
            std::cout << "  MSA → EDS        : biofmi-transform -i data.msa -o output.eds\n";
            std::cout << "  MSA → l-EDS      : biofmi-transform -i data.msa -o output.eds -l 5\n";
            std::cout << "  VCF → EDS        : biofmi-transform -i data.vcf -o output.eds\n";
            std::cout << "  VCF → l-EDS      : biofmi-transform -i data.vcf -o output.eds -l 5\n";
            std::cout << "  EDS → l-EDS      : biofmi-transform -i data.eds -o output.eds -l 5\n";
            std::cout << "\nWith sources (LINEAR merge):\n";
            std::cout << "  biofmi-transform -i data.msa -o output.eds -s sources.seds -l 5 --method linear\n";
            std::cout << "\nWithout sources (CARTESIAN merge):\n";
            std::cout << "  biofmi-transform -i data.msa -o output.eds -l 5 --method cartesian\n";
            return 0;
        }

        po::notify(vm);

        // Detect input file type
        FileType input_type = detect_file_type(input_file);

        if (input_type == FileType::UNKNOWN) {
            std::cerr << "Error: Unknown input file type: " << input_file << "\n";
            std::cerr << "Supported types: .msa, .vcf, .eds\n";
            return 1;
        }

        if (input_type == FileType::SEDS) {
            std::cerr << "Error: .seds files are source files, not input data\n";
            std::cerr << "Use --sources/-s flag to specify a .seds file\n";
            return 1;
        }

        // Generate output filename if not provided
        if (output_file.empty()) {
            output_file = input_file;
            output_file.replace_extension(".eds");
            if (context_length > 0) {
                output_file = input_file.stem().string() + "_l" + std::to_string(context_length) + ".eds";
            }
        }

        // Validate method
        if (method != "linear" && method != "cartesian") {
            std::cerr << "Error: Invalid method '" << method << "'\n";
            std::cerr << "Valid methods: linear, cartesian\n";
            return 1;
        }

        std::cout << "Transform: " << file_type_to_string(input_type);
        if (context_length > 0) {
            std::cout << " → l-EDS (l=" << context_length << ")\n";
        } else {
            std::cout << " → EDS\n";
        }

        // Route to appropriate transformation function
        if (input_type == FileType::MSA) {
            if (context_length > 0) {
                transform_msa_to_leds(input_file, output_file, sources_file, context_length, method);
            } else {
                transform_msa_to_eds(input_file, output_file, sources_file, method);
            }
        } else if (input_type == FileType::VCF) {
            if (context_length > 0) {
                transform_vcf_to_leds(input_file, output_file, sources_file, context_length, method);
            } else {
                transform_vcf_to_eds(input_file, output_file, sources_file, method);
            }
        } else if (input_type == FileType::EDS) {
            if (context_length == 0) {
                std::cerr << "Error: EDS input requires context length (-l) for transformation to l-EDS\n";
                std::cerr << "Use -l <length> to specify minimum context length\n";
                return 1;
            }
            transform_eds_to_leds(input_file, output_file, sources_file, context_length, method);
        }

        std::cout << "Output: " << output_file << "\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
