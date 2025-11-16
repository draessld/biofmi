#include "transforms/eds_transforms.hpp"
#include "transforms/msa_transforms.hpp"
#include "transforms/vcf_transforms.hpp"
#include "common.hpp"
#include <boost/program_options.hpp>
#include <iostream>
#include <fstream>
#include <iomanip>

namespace po = boost::program_options;
using namespace biofmi;

// ===== FILE TYPE DETECTION =====

enum class FileType {
    MSA,
    VCF,
    EDS,
    SEDS,
    PEDS,     // Planned: Phased EDS (combined .eds + .seds) - not yet implemented
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
    } else if (extension == ".peds") {
        return FileType::PEDS;  // TODO: Implement PEDS parser
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
        case FileType::PEDS: return "PEDS";
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
    const std::string& method,
    int num_threads
) {
    // Open input MSA file
    std::ifstream msa_in(input_file);
    if (!msa_in) {
        throw std::runtime_error("Failed to open input file: " + input_file.string());
    }

    // Transform MSA to EDS with sources
    auto [eds_str, seds_str] = biofmi::parse_msa_to_eds_streaming(msa_in);
    msa_in.close();

    // Determine output paths
    std::filesystem::path eds_path = output_file.empty()
        ? input_file.parent_path() / (input_file.stem().string() + ".eds")
        : output_file;

    std::filesystem::path seds_path = sources_file.empty()
        ? eds_path.parent_path() / (eds_path.stem().string() + ".seds")
        : sources_file;

    // Write EDS output
    std::ofstream eds_out(eds_path);
    if (!eds_out) {
        throw std::runtime_error("Failed to open output file: " + eds_path.string());
    }
    eds_out << eds_str;
    eds_out.close();

    // Write sources output
    std::ofstream seds_out(seds_path);
    if (!seds_out) {
        throw std::runtime_error("Failed to open sources file: " + seds_path.string());
    }
    seds_out << seds_str;
    seds_out.close();

    std::cout << "MSA → EDS transformation complete\n";
    std::cout << "  EDS output: " << eds_path << "\n";
    std::cout << "  Sources output: " << seds_path << "\n";
}

// MSA → l-EDS (direct transformation with merging)
void transform_msa_to_leds(
    const std::filesystem::path& input_file,
    const std::filesystem::path& output_file,
    const std::filesystem::path& sources_file,
    Length context_length,
    const std::string& method,
    int num_threads
) {
    // Open input MSA file
    std::ifstream msa_in(input_file);
    if (!msa_in) {
        throw std::runtime_error("Failed to open input file: " + input_file.string());
    }

    // Transform MSA to l-EDS with sources
    auto [leds_str, seds_str] = biofmi::parse_msa_to_leds_streaming(msa_in, context_length);
    msa_in.close();

    // Determine output paths with _l<l> suffix
    std::string base_name = input_file.stem().string();
    std::string suffix = "_l" + std::to_string(context_length);

    std::filesystem::path leds_path = output_file.empty()
        ? input_file.parent_path() / (base_name + suffix + ".leds")
        : output_file;

    std::filesystem::path seds_path = sources_file.empty()
        ? leds_path.parent_path() / (base_name + suffix + ".seds")
        : sources_file;

    // Write l-EDS output
    std::ofstream leds_out(leds_path);
    if (!leds_out) {
        throw std::runtime_error("Failed to open output file: " + leds_path.string());
    }
    leds_out << leds_str;
    leds_out.close();

    // Write sources output
    std::ofstream seds_out(seds_path);
    if (!seds_out) {
        throw std::runtime_error("Failed to open sources file: " + seds_path.string());
    }
    seds_out << seds_str;
    seds_out.close();

    std::cout << "MSA → l-EDS transformation complete (l=" << context_length << ")\n";
    std::cout << "  l-EDS output: " << leds_path << "\n";
    std::cout << "  Sources output: " << seds_path << "\n";
}

// VCF → EDS (with sources)
void transform_vcf_to_eds(
    const std::filesystem::path& input_file,
    const std::filesystem::path& reference_file,
    const std::filesystem::path& output_file,
    const std::filesystem::path& sources_file,
    const std::string& method,
    int num_threads
) {
    // Open VCF file
    std::ifstream vcf_in(input_file);
    if (!vcf_in) {
        throw std::runtime_error("Failed to open VCF file: " + input_file.string());
    }

    // Open FASTA reference file
    std::ifstream fasta_in(reference_file);
    if (!fasta_in) {
        throw std::runtime_error("Failed to open reference FASTA file: " + reference_file.string());
    }

    // Transform VCF to EDS with sources
    auto [eds_str, seds_str] = biofmi::parse_vcf_to_eds_streaming(vcf_in, fasta_in);
    vcf_in.close();
    fasta_in.close();

    // Determine output paths
    std::filesystem::path eds_path = output_file.empty()
        ? input_file.parent_path() / (input_file.stem().string() + ".eds")
        : output_file;

    std::filesystem::path seds_path = sources_file.empty()
        ? eds_path.parent_path() / (eds_path.stem().string() + ".seds")
        : sources_file;

    // Write EDS output
    std::ofstream eds_out(eds_path);
    if (!eds_out) {
        throw std::runtime_error("Failed to open output file: " + eds_path.string());
    }
    eds_out << eds_str;
    eds_out.close();

    // Write sources output
    std::ofstream seds_out(seds_path);
    if (!seds_out) {
        throw std::runtime_error("Failed to open sources file: " + seds_path.string());
    }
    seds_out << seds_str;
    seds_out.close();

    std::cout << "VCF → EDS transformation complete\n";
    std::cout << "  EDS output: " << eds_path << "\n";
    std::cout << "  Sources output: " << seds_path << "\n";
}

// VCF → l-EDS (direct transformation with merging)
void transform_vcf_to_leds(
    const std::filesystem::path& input_file,
    const std::filesystem::path& reference_file,
    const std::filesystem::path& output_file,
    const std::filesystem::path& sources_file,
    Length context_length,
    const std::string& method,
    int num_threads
) {
    // Open VCF file
    std::ifstream vcf_in(input_file);
    if (!vcf_in) {
        throw std::runtime_error("Failed to open VCF file: " + input_file.string());
    }

    // Open FASTA reference file
    std::ifstream fasta_in(reference_file);
    if (!fasta_in) {
        throw std::runtime_error("Failed to open reference FASTA file: " + reference_file.string());
    }

    // Transform VCF to l-EDS with sources
    auto [leds_str, seds_str] = biofmi::parse_vcf_to_leds_streaming(vcf_in, fasta_in, context_length);
    vcf_in.close();
    fasta_in.close();

    // Determine output paths with _l<l> suffix
    std::string base_name = input_file.stem().string();
    std::string suffix = "_l" + std::to_string(context_length);

    std::filesystem::path leds_path = output_file.empty()
        ? input_file.parent_path() / (base_name + suffix + ".leds")
        : output_file;

    std::filesystem::path seds_path = sources_file.empty()
        ? leds_path.parent_path() / (base_name + suffix + ".seds")
        : sources_file;

    // Write l-EDS output
    std::ofstream leds_out(leds_path);
    if (!leds_out) {
        throw std::runtime_error("Failed to open output file: " + leds_path.string());
    }
    leds_out << leds_str;
    leds_out.close();

    // Write sources output
    std::ofstream seds_out(seds_path);
    if (!seds_out) {
        throw std::runtime_error("Failed to open sources file: " + seds_path.string());
    }
    seds_out << seds_str;
    seds_out.close();

    std::cout << "VCF → l-EDS transformation complete (l=" << context_length << ")\n";
    std::cout << "  l-EDS output: " << leds_path << "\n";
    std::cout << "  Sources output: " << seds_path << "\n";
}

// EDS → l-EDS (merge existing EDS to satisfy length constraint)
void transform_eds_to_leds(
    const std::filesystem::path& input_file,
    const std::filesystem::path& output_file,
    const std::filesystem::path& sources_file,
    Length context_length,
    const std::string& method,
    int num_threads
) {
    std::cout << "EDS → l-EDS transformation\n";
    std::cout << "  Input: " << input_file << "\n";
    std::cout << "  Output: " << output_file << "\n";
    std::cout << "  Context length: " << context_length << "\n";
    std::cout << "  Method: " << method << " (LINEAR=with sources, CARTESIAN=without)\n";
    if (!sources_file.empty()) {
        std::cout << "  Sources: " << sources_file << "\n";
    }
    std::cout << "  Threads: " << num_threads << (num_threads == 1 ? " (sequential)" : " (parallel)") << "\n";

    // Open input file
    std::ifstream input(input_file);
    if (!input) {
        throw std::runtime_error("Cannot open input file: " + input_file.string());
    }

    // Open output file
    std::ofstream output(output_file);
    if (!output) {
        throw std::runtime_error("Cannot open output file: " + output_file.string());
    }

    // Handle sources if provided
    std::ifstream* sources_in = nullptr;
    std::ofstream* sources_out = nullptr;

    if (!sources_file.empty()) {
        // Input sources file
        sources_in = new std::ifstream(sources_file);
        if (!*sources_in) {
            delete sources_in;
            throw std::runtime_error("Cannot open sources file: " + sources_file.string());
        }

        // Generate output sources filename
        std::filesystem::path output_sources = output_file;
        output_sources.replace_extension(".seds");

        sources_out = new std::ofstream(output_sources);
        if (!*sources_out) {
            delete sources_in;
            delete sources_out;
            throw std::runtime_error("Cannot create output sources file: " + output_sources.string());
        }

        std::cout << "  Output sources: " << output_sources << "\n";
    }

    try {
        // Call library function based on method
        if (method == "linear") {
            // Linear merge with source intersection
            biofmi::eds_to_leds_linear(
                input,
                output,
                context_length,
                sources_in,
                sources_out,
                static_cast<size_t>(num_threads)
            );
        } else if (method == "cartesian") {
            // Cartesian merge (no sources)
            if (sources_in) {
                delete sources_in;
                delete sources_out;
                throw std::runtime_error("Cartesian method cannot be used with source files");
            }

            biofmi::eds_to_leds_cartesian(
                input,
                output,
                context_length,
                static_cast<size_t>(num_threads)
            );
        } else {
            delete sources_in;
            delete sources_out;
            throw std::runtime_error("Unknown method: " + method);
        }

        // Cleanup
        delete sources_in;
        delete sources_out;

        std::cout << "Transformation complete!\n";

    } catch (...) {
        // Cleanup on exception
        delete sources_in;
        delete sources_out;
        throw;
    }
}

// ===== MAIN =====

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
        std::filesystem::path reference_file;
        std::filesystem::path output_file;
        std::filesystem::path sources_file;
        Length context_length;
        std::string method;
        int num_threads;

        po::options_description desc("Transform MSA/VCF/EDS to EDS/l-EDS format");
        desc.add_options()
            ("help,h", "Show help message")
            ("input,i", po::value<std::filesystem::path>(&input_file)->required(), "Input file (.msa, .vcf, .eds)")
            ("reference,r", po::value<std::filesystem::path>(&reference_file), "Reference FASTA file (required for VCF input)")
            ("output,o", po::value<std::filesystem::path>(&output_file), "Output file")
            ("context-length,l", po::value<Length>(&context_length)->default_value(0), "Context length (0 = no merging, >0 = create l-EDS)")
            ("method", po::value<std::string>(&method)->default_value("linear"), "Method: linear or cartesian")
            ("sources,s", po::value<std::filesystem::path>(&sources_file), "Source file for phasing (.seds)")
            ("threads", po::value<int>(&num_threads)->default_value(1), "Number of threads for parallel merging (default: 1 = sequential)");

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);

        if (vm.count("help")) {
            std::cout << desc << "\n";
            std::cout << "\n=== SUPPORTED TRANSFORMATIONS ===\n\n";

            std::cout << "EDS → l-EDS (Length-constrained EDS):\n";
            std::cout << "  Linear merging (phasing-aware, requires sources):\n";
            std::cout << "    biofmi-transform -i data.eds -s data.seds -l 5 --method linear\n";
            std::cout << "  Cartesian merging (all combinations, no sources):\n";
            std::cout << "    biofmi-transform -i data.eds -l 5 --method cartesian\n";
            std::cout << "  Parallel processing (4 threads):\n";
            std::cout << "    biofmi-transform -i data.eds -l 5 --method cartesian --threads 4\n\n";

            std::cout << "MSA → EDS (with source tracking):\n";
            std::cout << "  biofmi-transform -i alignment.msa -o output.eds\n";
            std::cout << "  # Creates: output.eds and output.seds\n\n";

            std::cout << "MSA → l-EDS (direct transformation):\n";
            std::cout << "  biofmi-transform -i alignment.msa -l 10\n";
            std::cout << "  # Creates: alignment_l10.eds and alignment_l10.seds\n";
            std::cout << "  # Skips intermediate EDS for efficiency\n\n";

            std::cout << "VCF → EDS (with sample phasing, requires reference):\n";
            std::cout << "  biofmi-transform -i variants.vcf -r reference.fa -o output.eds\n";
            std::cout << "  # Creates: output.eds and output.seds\n\n";

            std::cout << "VCF → l-EDS (two-stage pipeline: VCF→EDS→l-EDS):\n";
            std::cout << "  biofmi-transform -i variants.vcf -r reference.fa -l 5\n";
            std::cout << "  # Creates: variants_l5.eds and variants_l5.seds\n";
            std::cout << "  # Two-stage approach is optimal (see note below)\n\n";

            std::cout << "=== METHOD SELECTION (EDS → l-EDS only) ===\n\n";
            std::cout << "Linear (--method linear):\n";
            std::cout << "  - Phasing-aware merging\n";
            std::cout << "  - Requires source file (.seds) via --sources/-s\n";
            std::cout << "  - Preserves haplotype relationships\n";
            std::cout << "  - Use for: Genomic data with known phasing\n\n";

            std::cout << "Cartesian (--method cartesian):\n";
            std::cout << "  - All-combinations merging\n";
            std::cout << "  - No source information used\n";
            std::cout << "  - Creates cross-product of alternatives\n";
            std::cout << "  - Use for: Unknown phasing or all combos needed\n\n";

            std::cout << "=== NOTES ===\n\n";
            std::cout << "VCF Requirements:\n";
            std::cout << "  VCF input requires reference FASTA via --reference/-r flag\n\n";

            std::cout << "Why VCF → l-EDS Uses Two Stages:\n";
            std::cout << "  VCF represents sparse variants on a reference. Unlike MSA\n";
            std::cout << "  (which has full alignment), VCF doesn't provide a global view\n";
            std::cout << "  of common vs variant regions. The two-stage pipeline is the\n";
            std::cout << "  optimal approach:\n";
            std::cout << "    1. VCF→EDS: Handle VCF-specific complexity (overlaps, etc)\n";
            std::cout << "    2. EDS→l-EDS: Apply context-length constraint\n";
            std::cout << "  This provides better code reuse, testability, and performance\n";
            std::cout << "  than a direct transformation would achieve.\n\n";

            std::cout << "Output Files:\n";
            std::cout << "  EDS output:   <input_base>.eds and <input_base>.seds\n";
            std::cout << "  l-EDS output: <input_base>_l<N>.eds and <input_base>_l<N>.seds\n";
            std::cout << "  where <N> is the context length value\n\n";

            std::cout << "Parallel Processing:\n";
            std::cout << "  --threads parameter only affects EDS→l-EDS transformations\n";
            std::cout << "  Beneficial for large EDS with many degenerate symbols\n";
            std::cout << "  Small files (<1MB) may not benefit from parallelization\n\n";

            print_performance();
            return 0;
        }

        po::notify(vm);

        // Detect input file type
        FileType input_type = detect_file_type(input_file);

        if (input_type == FileType::UNKNOWN) {
            std::cerr << "Error: Unknown input file type: " << input_file << "\n";
            std::cerr << "Supported types: .msa, .vcf, .eds\n";
            print_performance();
            return 1;
        }

        if (input_type == FileType::SEDS) {
            std::cerr << "Error: .seds files are source files, not input data\n";
            std::cerr << "Use --sources/-s flag to specify a .seds file\n";
            print_performance();
            return 1;
        }

        if (input_type == FileType::PEDS) {
            std::cerr << "Error: .peds format not yet implemented\n";
            std::cerr << "PEDS (Phased EDS) will combine .eds + .seds into a single file\n";
            std::cerr << "For now, use separate .eds and .seds files with --sources/-s flag\n";
            print_performance();
            return 1;
        }

        // Validate VCF requires reference file
        if (input_type == FileType::VCF && reference_file.empty()) {
            std::cerr << "Error: VCF input requires reference FASTA file\n";
            std::cerr << "Use --reference/-r flag to specify the reference FASTA file\n";
            print_performance();
            return 1;
        }

        // Generate output filename if not provided
        if (output_file.empty()) {
            if (context_length > 0) {
                // For l-EDS, add _l{context_length} suffix before extension
                output_file = input_file.parent_path() / (input_file.stem().string() + "_l" + std::to_string(context_length) + ".eds");
            } else {
                // For regular EDS, keep same name with .eds extension
                output_file = input_file;
                output_file.replace_extension(".eds");
            }
        }

        // Validate method
        if (method != "linear" && method != "cartesian") {
            std::cerr << "Error: Invalid method '" << method << "'\n";
            std::cerr << "Valid methods: linear, cartesian\n";
            print_performance();
            return 1;
        }

        // Validate threads
        if (num_threads < 1) {
            std::cerr << "Error: Number of threads must be >= 1\n";
            print_performance();
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
                transform_msa_to_leds(input_file, output_file, sources_file, context_length, method, num_threads);
            } else {
                transform_msa_to_eds(input_file, output_file, sources_file, method, num_threads);
            }
        } else if (input_type == FileType::VCF) {
            if (context_length > 0) {
                transform_vcf_to_leds(input_file, reference_file, output_file, sources_file, context_length, method, num_threads);
            } else {
                transform_vcf_to_eds(input_file, reference_file, output_file, sources_file, method, num_threads);
            }
        } else if (input_type == FileType::EDS) {
            if (context_length == 0) {
                std::cerr << "Error: EDS input requires context length (-l) for transformation to l-EDS\n";
                std::cerr << "Use -l <length> to specify minimum context length\n";
                print_performance();
                return 1;
            }
            transform_eds_to_leds(input_file, output_file, sources_file, context_length, method, num_threads);
        }

        std::cout << "Output: " << output_file << "\n";
        print_performance();
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        print_performance();
        return 1;
    }
}
