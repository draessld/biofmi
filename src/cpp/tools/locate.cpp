#include "index/index.hpp"
#include <edsparser/common.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <vector>

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
        std::filesystem::path sources_file;
        std::filesystem::path sources_edz_file;
        std::string pattern;
        Length context_length;
        bool benchmark = false;
        bool list_samples = false;
        std::filesystem::path chunk_stats_file;

        po::options_description desc("Locate patterns in BIO-FMI index");
        desc.add_options()
            ("help,h", "Show help message")
            ("index,i", po::value<std::filesystem::path>(&index_file)->required(), "Index file or directory")
            ("context-length,l", po::value<Length>(&context_length)->required(), "Context length")
            ("pattern,p", po::value<std::string>(&pattern), "Single pattern to search")
            ("pattern-file,P", po::value<std::filesystem::path>(&pattern_file), "File with patterns")
            ("output,o", po::value<std::filesystem::path>(&output_file), "Output file")
            ("seds,s", po::value<std::filesystem::path>(&sources_file),
                "Source file of the indexed l-EDS (.seds/.edz, format auto-detected). "
                "Enables LINEAR (source-aware) search: a match must lie on a single "
                "path through the panel. Without it the search is CARTESIAN and pairs "
                "every alternative of one degenerate symbol with every alternative of "
                "the next, over-reporting on a LINEAR l-EDS.")
            ("edz,z", po::value<std::filesystem::path>(&sources_edz_file),
                "Source file treated as binary EDZ regardless of extension "
                "(mutually exclusive with -s)")
            ("samples", po::bool_switch(&list_samples),
                "List the genome ids carrying each occurrence, not just how many. "
                "Requires -s/-z; without sources the index has no basis to name them.")
            ("benchmark", po::bool_switch(&benchmark), "Benchmark mode")
            ("chunk-stats", po::value<std::filesystem::path>(&chunk_stats_file),
                "Write a per-chunk cost trace to this CSV and report per-chunk "
                "aggregates on stderr. locate() splits a pattern into (l+1)-char "
                "chunks and stops as soon as the candidate set empties, so a query "
                "time on its own cannot say whether a chunk is expensive or the "
                "pattern merely survived many of them. Implies --benchmark.");

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

        if (vm.count("seds") && vm.count("edz")) {
            std::cerr << "Error: --seds/-s and --edz/-z are mutually exclusive\n";
            print_performance();
            return 1;
        }

        // Load index
        std::cerr << "Loading index from " << index_file << "...\n";
        BioFMI index(index_file);

        // Attach sources, if given, for LINEAR (source-aware) search.
        if (vm.count("seds") || vm.count("edz")) {
            const auto& src = vm.count("edz") ? sources_edz_file : sources_file;
            if (!std::filesystem::exists(src)) {
                std::cerr << "Error: Source file does not exist: " << src << "\n";
                print_performance();
                return 1;
            }
            if (vm.count("edz")) {
                index.attach_sources(src, Sources::Format::EDZ);
            } else {
                index.attach_sources(src);
            }
            std::cerr << "Mode: LINEAR (source-aware) — " << index.num_paths()
                      << " paths from " << src << "\n";
        } else {
            if (list_samples) {
                std::cerr << "Error: --samples requires -s/--seds or -z/--edz; without "
                             "sources the index\n  cannot name the genomes carrying a "
                             "match.\n";
                print_performance();
                return 1;
            }
            std::cerr << "Mode: CARTESIAN — no sources given, so alternatives of "
                         "adjacent\n  degenerate symbols are paired freely. On a "
                         "LINEAR l-EDS this over-reports;\n  pass -s/--seds to "
                         "constrain matches to a single path.\n";
        }

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

        // Per-chunk cost trace. Opt-in: the trace times every chunk separately,
        // and on a short pattern those clock reads are a measurable share of the
        // query, so the plain --benchmark path must stay untraced.
        const bool trace = !chunk_stats_file.empty();
        std::ofstream chunk_csv;
        if (trace) {
            chunk_csv.open(chunk_stats_file);
            if (!chunk_csv.is_open()) {
                std::cerr << "Error: Cannot open chunk-stats file: " << chunk_stats_file << "\n";
                print_performance();
                return 1;
            }
            benchmark = true;
            index.set_trace(true);
            chunk_csv << "pattern_id,pattern_len,chunks_planned,chunk_idx,chunk_len,"
                         "verify,time_us,ref_hits,chg_hits,cand_in,cand_out\n";
        }
        const size_t chunk_size = (size_t)context_length + 1;

        // Aggregates over every chunk actually executed, kept separately from the
        // per-pattern totals so "what a chunk costs" and "how many chunks a
        // pattern survived" never end up averaged into one number.
        size_t chunks_planned_total = 0, chunks_done_total = 0;
        size_t hits_total = 0, cand_out_total = 0;
        double chunk_us_total = 0.0, pattern_us_total = 0.0;
        std::vector<double> pattern_us;

        // Search each pattern
        size_t total_occurrences = 0;
        size_t patterns_matched = 0;
        size_t pattern_id = 0;
        for (const auto& p : patterns) {
            try {
                const auto t0 = std::chrono::steady_clock::now();
                auto result = index.locate(p);
                const double p_us = std::chrono::duration<double, std::micro>(
                                        std::chrono::steady_clock::now() - t0).count();
                pattern_us.push_back(p_us);
                pattern_us_total += p_us;

                if (trace) {
                    // Chunks the plan called for, against the chunks the search
                    // reached before the candidate set emptied. The difference is
                    // the early exit, and it is the thing that makes a bare query
                    // time uninterpretable.
                    const size_t planned = (p.size() / chunk_size) + (p.size() % chunk_size ? 1 : 0);
                    const auto& tr = index.last_trace();
                    chunks_planned_total += planned;
                    chunks_done_total += tr.size();
                    for (const auto& c : tr) {
                        chunk_us_total += c.time_us;
                        hits_total += c.ref_hits + c.chg_hits;
                        cand_out_total += c.cand_out;
                        chunk_csv << pattern_id << ',' << p.size() << ',' << planned << ','
                                  << c.chunk_idx << ',' << c.chunk_len << ','
                                  << (c.verify ? 1 : 0) << ','
                                  << std::fixed << std::setprecision(3) << c.time_us << ','
                                  << c.ref_hits << ',' << c.chg_hits << ','
                                  << c.cand_in << ',' << c.cand_out << '\n';
                    }
                }

                if (!benchmark) {
                    *out << "Pattern: " << p << "\n";
                    if (result.empty()) {
                        *out << "No occurrences found\n";
                    } else {
                        index.print_result(result, *out, list_samples);
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
                    if (count) patterns_matched++;
                }
            } catch (const std::exception& e) {
                if (!benchmark) {
                    *out << "Pattern: " << p << "\n";
                    *out << "Error: " << e.what() << "\n\n";
                }
                std::cerr << "Error searching pattern '" << p << "': " << e.what() << "\n";
            }
            pattern_id++;
        }

        if (benchmark) {
            std::cerr << "Total patterns: " << patterns.size() << "\n";
            std::cerr << "Patterns matched: " << patterns_matched << "\n";
            std::cerr << "Total occurrences: " << total_occurrences << "\n";
        }

        if (trace) {
            chunk_csv.close();

            // Per-chunk cost, reported against the chunks that actually ran. The
            // denominator is chunks_done, never chunks_planned: dividing by a
            // plan the search abandoned would charge an early exit for work it
            // never did, and would make an unmatched pattern look cheap per chunk
            // for the same reason it looks cheap per pattern.
            const size_t np = patterns.size() ? patterns.size() : 1;
            const size_t nc = chunks_done_total ? chunks_done_total : 1;
            auto median = [](std::vector<double> v) -> double {
                if (v.empty()) return 0.0;
                std::sort(v.begin(), v.end());
                const size_t h = v.size() / 2;
                return v.size() % 2 ? v[h] : 0.5 * (v[h - 1] + v[h]);
            };

            std::cerr << std::fixed << std::setprecision(3);
            std::cerr << "Chunks planned: "        << chunks_planned_total << "\n";
            std::cerr << "Chunks searched: "       << chunks_done_total << "\n";
            std::cerr << "Chunks per pattern: "    << (double)chunks_done_total / np << "\n";
            std::cerr << "Chunk completion: "
                      << (chunks_planned_total
                              ? (double)chunks_done_total / (double)chunks_planned_total
                              : 0.0) << "\n";
            std::cerr << "Us per chunk: "          << chunk_us_total / nc << "\n";
            std::cerr << "Us per pattern: "        << pattern_us_total / np << "\n";
            std::cerr << "Us per pattern median: " << median(pattern_us) << "\n";
            std::cerr << "Hits per chunk: "        << (double)hits_total / nc << "\n";
            std::cerr << "Candidates per chunk: "  << (double)cand_out_total / nc << "\n";
            std::cerr << "Chunk time total us: "   << chunk_us_total << "\n";
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
