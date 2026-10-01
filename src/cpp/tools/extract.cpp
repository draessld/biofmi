// biofmi-extract: read a stretch of one genome back out of the index (TODO §9).
//
// Under the partition requirement every genome is a path through the l-EDS, so
// its sequence is T0 with its own alternative spliced in at each degenerate
// symbol. That needs the sources the index was queried with in LINEAR mode, and
// nothing else: the reference comes out of I_0 and the alternatives out of I_D.

#include "index/index.hpp"
#include <edsparser/common.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace po = boost::program_options;
using namespace biofmi;
using edsparser::Timer;
using edsparser::get_peak_memory_mb;

namespace {

struct Region { int genome; size_t begin, end; };

// "g:i-j" (0-based, half-open) or "g i j"; the whole genome when only "g" is given.
bool parse_region(const std::string& text, Region& r, bool& whole) {
    std::string t = text;
    for (char& c : t) if (c == ':' || c == '-' || c == '\t') c = ' ';
    std::istringstream is(t);
    whole = false;
    if (!(is >> r.genome)) return false;
    if (!(is >> r.begin)) { whole = true; return true; }
    if (!(is >> r.end)) return false;
    std::string rest;
    return !(is >> rest);
}

}  // namespace

int main(int argc, char** argv) {
    Timer timer;
    timer.start();
    auto print_performance = [&timer]() {
        timer.stop();
        std::cerr << "[Performance] Runtime: " << std::fixed << std::setprecision(2)
                  << timer.elapsed_seconds() << "s";
        const double memory_mb = get_peak_memory_mb();
        if (memory_mb > 0.0)
            std::cerr << " | Peak Memory: " << std::fixed << std::setprecision(1) << memory_mb << " MB";
        std::cerr << "\n";
    };

    try {
        std::filesystem::path index_file, sources_file, sources_edz_file, region_file, output_file;
        std::vector<std::string> regions_arg;
        Length context_length = 0;
        size_t sample_rate = BioFMI::kDefaultGenomeSampleRate;
        bool lengths = false;
        size_t bench_n = 0, bench_len = 100;
        unsigned seed = 1;

        po::options_description desc(
            "Extract stretches of genomes from a BIO-FMI index plus its sources.\n"
            "Coordinates are 0-based and half-open; genomes are the 1-based path ids\n"
            "--samples reports. Requires sources that partition the genomes (TODO §4)");
        desc.add_options()
            ("help,h", "Show help message")
            ("index,i", po::value<std::filesystem::path>(&index_file)->required(), "Index directory")
            ("context-length,l", po::value<Length>(&context_length),
                "Context length; checked against the index when given")
            ("seds,s", po::value<std::filesystem::path>(&sources_file),
                "Source file of the indexed l-EDS (.seds/.edz, format auto-detected)")
            ("edz,z", po::value<std::filesystem::path>(&sources_edz_file),
                "Source file treated as binary EDZ regardless of extension")
            ("region,r", po::value<std::vector<std::string>>(&regions_arg)->composing(),
                "Region g:i-j (repeatable); g alone extracts the whole genome")
            ("region-file,R", po::value<std::filesystem::path>(&region_file),
                "File of regions, one per line, as g:i-j or g i j")
            ("lengths", po::bool_switch(&lengths), "Print every genome's length")
            ("sample-rate,b", po::value<size_t>(&sample_rate),
                "Sample the per-genome prefix sums every b degenerate symbols. 1 "
                "stores them all; larger costs k*n/b words and a walk of up to b symbols")
            ("output,o", po::value<std::filesystem::path>(&output_file), "Output file (FASTA)")
            ("benchmark", po::value<size_t>(&bench_n),
                "Time N random extracts and N random coordinate lookups; one TSV row "
                "on stdout: sample_rate, map_bytes, build_s, extract_us, coord_us, coords_all_us")
            ("extract-length", po::value<size_t>(&bench_len), "Extract length for --benchmark (default 100)")
            ("seed", po::value<unsigned>(&seed), "RNG seed for --benchmark (default 1)");

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        if (vm.count("help")) {
            std::cout << desc << "\n";
            print_performance();
            return 0;
        }
        po::notify(vm);

        if (vm.count("seds") == vm.count("edz")) {
            std::cerr << "Error: exactly one of --seds/-s or --edz/-z is required: a genome "
                         "is a path, and only the sources say which\n";
            print_performance();
            return 1;
        }
        if (!vm.count("region") && !vm.count("region-file") && !lengths && !bench_n) {
            std::cerr << "Error: nothing to do — give --region, --region-file, --lengths or --benchmark\n";
            print_performance();
            return 1;
        }

        std::cerr << "Loading index from " << index_file << "...\n";
        BioFMI index(index_file);
        if (vm.count("context-length") && index.get_statistics().context_length != context_length) {
            std::cerr << "Error: -l " << context_length << " but the index was built with l="
                      << index.get_statistics().context_length << "\n";
            print_performance();
            return 1;
        }

        const auto& src = vm.count("edz") ? sources_edz_file : sources_file;
        if (!std::filesystem::exists(src)) {
            std::cerr << "Error: Source file does not exist: " << src << "\n";
            print_performance();
            return 1;
        }
        if (vm.count("edz")) index.attach_sources(src, Sources::Format::EDZ);
        else                 index.attach_sources(src);

        const auto b0 = std::chrono::steady_clock::now();
        index.build_genome_map(sample_rate);
        const double build_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - b0).count();
        std::cerr << "Genome map: " << index.num_paths() << " genomes, "
                  << index.get_statistics().num_changes << " degenerate symbols sampled every "
                  << sample_rate << ", " << index.genome_map_bytes() << " bytes, built in "
                  << std::fixed << std::setprecision(3) << build_s << " s\n";

        std::ostream* out = &std::cout;
        std::ofstream outfile;
        if (vm.count("output")) {
            outfile.open(output_file);
            if (!outfile) {
                std::cerr << "Error: Cannot open output file: " << output_file << "\n";
                print_performance();
                return 1;
            }
            out = &outfile;
        }

        if (lengths)
            for (int g = 1; g <= (int)index.num_paths(); g++)
                *out << g << "\t" << index.genome_length(g) << "\n";

        std::vector<std::string> region_text = regions_arg;
        if (vm.count("region-file")) {
            std::ifstream rf(region_file);
            if (!rf) {
                std::cerr << "Error: Cannot open region file: " << region_file << "\n";
                print_performance();
                return 1;
            }
            for (std::string line; std::getline(rf, line);)
                if (!line.empty() && line[0] != '#') region_text.push_back(line);
        }

        int failed = 0;
        for (const auto& text : region_text) {
            Region r{};
            bool whole = false;
            if (!parse_region(text, r, whole)) {
                std::cerr << "Error: cannot parse region '" << text << "' (want g:i-j)\n";
                failed++;
                continue;
            }
            try {
                if (whole) { r.begin = 0; r.end = index.genome_length(r.genome); }
                const String seq = index.extract(r.genome, r.begin, r.end);
                *out << ">" << r.genome << ":" << r.begin << "-" << r.end << "\n";
                for (size_t k = 0; k < seq.size(); k += 80) *out << seq.substr(k, 80) << "\n";
            } catch (const std::exception& e) {
                std::cerr << "Error extracting '" << text << "': " << e.what() << "\n";
                failed++;
            }
        }

        if (bench_n) {
            // Two costs, kept apart: extract() is locate-in-genome plus spelling,
            // genome_position() is the walk alone. Coordinates are drawn first so
            // the RNG is not inside the timed loop.
            std::mt19937_64 rng(seed);
            const int k = (int)index.num_paths();
            const auto st = index.get_statistics();
            std::vector<std::pair<int, size_t>> ex, co;
            for (size_t n = 0; n < bench_n; n++) {
                const int g = 1 + (int)(rng() % (uint64_t)k);
                const size_t len = index.genome_length(g);
                const size_t span = std::min(bench_len, len);
                ex.emplace_back(g, (size_t)(rng() % (uint64_t)(len - span + 1)));
                co.emplace_back(1 + (int)(rng() % (uint64_t)k),
                                (size_t)(rng() % (uint64_t)std::max<size_t>(1, st.reference_length)));
            }

            size_t chars = 0;
            auto t = std::chrono::steady_clock::now();
            for (const auto& [g, i] : ex)
                chars += index.extract(g, i, std::min(i + bench_len, index.genome_length(g))).size();
            const double ex_us = std::chrono::duration<double, std::micro>(
                                     std::chrono::steady_clock::now() - t).count() / (double)bench_n;

            // A T0 position with no changes is a reference character, which every
            // genome carries; the lookup is then the walk and nothing else.
            int64_t sink = 0;
            t = std::chrono::steady_clock::now();
            for (const auto& [g, p] : co) sink += index.genome_position(g, (Position)p, {});
            const double co_us = std::chrono::duration<double, std::micro>(
                                     std::chrono::steady_clock::now() - t).count() / (double)bench_n;

            // Every genome at once, as --genome-coords reports an occurrence.
            BioFMI::Occurrence occ;
            occ.paths = PathSet{0};
            t = std::chrono::steady_clock::now();
            for (const auto& [g, p] : co) {
                occ.position = (Position)p;
                sink += (int64_t)index.genome_positions(occ).size();
            }
            const double all_us = std::chrono::duration<double, std::micro>(
                                      std::chrono::steady_clock::now() - t).count() / (double)bench_n;

            std::cerr << "Benchmark: " << bench_n << " extracts of " << bench_len << " (" << chars
                      << " chars), " << bench_n << " lookups (checksum " << sink << ")\n";
            *out << std::fixed << std::setprecision(3) << sample_rate << "\t"
                 << index.genome_map_bytes() << "\t" << build_s << "\t" << ex_us << "\t"
                 << co_us << "\t" << all_us << "\n";
        }

        print_performance();
        return failed ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        print_performance();
        return 1;
    }
}
