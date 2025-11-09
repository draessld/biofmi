#include "utils.hpp"
#include <algorithm>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <stdexcept>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace biofmi {

namespace {
    /**
     * Represents a pair of adjacent positions to merge
     */
    struct MergePair {
        size_t pos1;
        size_t pos2;

        MergePair(size_t p1, size_t p2) : pos1(p1), pos2(p2) {}
    };

    /**
     * Result of merging a pair of positions
     */
    struct MergeResult {
        size_t original_pos1;
        size_t original_pos2;
        StringSet merged_set;
        std::vector<std::set<int>> merged_sources;  // Empty if no sources
    };

    /**
     * Select independent pairs of adjacent positions to merge in parallel.
     *
     * Strategy: Greedy left-to-right selection, choosing pairs where merging
     * would help satisfy the l-EDS property. Ensures no overlapping positions
     * so parallel merging is safe.
     *
     * @param eds The EDS to analyze
     * @param context_length Minimum context length l
     * @return Vector of non-overlapping merge pairs
     */
    std::vector<MergePair> select_independent_merge_pairs(
        const EDS& eds,
        Length context_length
    ) {
        std::vector<MergePair> pairs;

        if (eds.length() < 2) {
            return pairs;  // Need at least 2 positions to merge
        }

        // Get degenerate flags from metadata
        const auto& is_degenerate = eds.get_is_degenerate();

        // Track which positions are already included in pairs
        std::vector<bool> used(eds.length(), false);

        // Greedy left-to-right selection
        for (size_t i = 0; i + 1 < eds.length(); ++i) {
            if (used[i] || used[i + 1]) {
                continue;  // Position already in a pair
            }

            // Only merge if it would fix an l-EDS violation
            // Cases to merge:
            // 1. Internal common block with length < l
            // 2. Two adjacent degenerate symbols (implicit empty common block)
            bool should_merge = false;

            // Check if position i is an internal common block that's too short
            if (!is_degenerate[i] && i > 0 && i < eds.length() - 1) {
                size_t global_idx1 = eds.get_metadata().cum_set_sizes[i];
                Length len1 = eds.get_string_length(global_idx1);
                if (len1 < context_length) {
                    should_merge = true;
                }
            }

            // Check if position i+1 is an internal common block that's too short
            if (!is_degenerate[i + 1] && (i + 1) > 0 && (i + 1) < eds.length() - 1) {
                size_t global_idx2 = eds.get_metadata().cum_set_sizes[i + 1];
                Length len2 = eds.get_string_length(global_idx2);
                if (len2 < context_length) {
                    should_merge = true;
                }
            }

            // Check if both positions are degenerate (implicit empty common)
            // This represents an implicit {} between them, which has length 0 < context_length
            // Note: Edge case exemption applies only to common blocks, not degenerate symbols
            if (is_degenerate[i] && is_degenerate[i + 1]) {
                should_merge = true;
            }

            if (should_merge) {
                pairs.emplace_back(i, i + 1);
                used[i] = true;
                used[i + 1] = true;
            }
        }

        return pairs;
    }

    /**
     * Merge multiple pairs of positions in parallel.
     *
     * Each pair is processed independently, then results are combined
     * to construct a new EDS.
     *
     * @param eds The original EDS
     * @param pairs Vector of non-overlapping merge pairs
     * @param num_threads Number of threads to use (1 = sequential)
     * @return Vector of merge results
     */
    std::vector<MergeResult> merge_multiple_pairs(
        const EDS& eds,
        const std::vector<MergePair>& pairs,
        size_t num_threads
    ) {
        std::vector<MergeResult> results(pairs.size());

        if (num_threads <= 1 || pairs.empty()) {
            // Sequential execution
            for (size_t i = 0; i < pairs.size(); ++i) {
                const auto& pair = pairs[i];
                EDS merged = eds.merge_adjacent(pair.pos1, pair.pos2);

                results[i].original_pos1 = pair.pos1;
                results[i].original_pos2 = pair.pos2;
                // merge_adjacent returns full EDS with positions merged at pos1
                results[i].merged_set = merged.read_symbol(pair.pos1);

                // Extract sources if present
                if (eds.has_sources()) {
                    size_t merged_size = merged.get_symbol_size(pair.pos1);
                    const auto& all_sources = merged.get_sources();
                    size_t global_idx = merged.get_metadata().cum_set_sizes[pair.pos1];
                    results[i].merged_sources.resize(merged_size);
                    for (size_t j = 0; j < merged_size; ++j) {
                        results[i].merged_sources[j] = all_sources[global_idx + j];
                    }
                }
            }
        } else {
            // Parallel execution with OpenMP
#ifdef _OPENMP
            #pragma omp parallel for num_threads(num_threads)
            for (size_t i = 0; i < pairs.size(); ++i) {
                const auto& pair = pairs[i];
                EDS merged = eds.merge_adjacent(pair.pos1, pair.pos2);

                results[i].original_pos1 = pair.pos1;
                results[i].original_pos2 = pair.pos2;
                results[i].merged_set = merged.read_symbol(pair.pos1);

                // Extract sources if present
                if (eds.has_sources()) {
                    size_t merged_size = merged.get_symbol_size(pair.pos1);
                    const auto& all_sources = merged.get_sources();
                    size_t global_idx = merged.get_metadata().cum_set_sizes[pair.pos1];
                    results[i].merged_sources.resize(merged_size);
                    for (size_t j = 0; j < merged_size; ++j) {
                        results[i].merged_sources[j] = all_sources[global_idx + j];
                    }
                }
            }
#else
            // OpenMP not available, fall back to sequential
            for (size_t i = 0; i < pairs.size(); ++i) {
                const auto& pair = pairs[i];
                EDS merged = eds.merge_adjacent(pair.pos1, pair.pos2);

                results[i].original_pos1 = pair.pos1;
                results[i].original_pos2 = pair.pos2;
                results[i].merged_set = merged.read_symbol(pair.pos1);

                if (eds.has_sources()) {
                    size_t merged_size = merged.get_symbol_size(pair.pos1);
                    const auto& all_sources = merged.get_sources();
                    size_t global_idx = merged.get_metadata().cum_set_sizes[pair.pos1];
                    results[i].merged_sources.resize(merged_size);
                    for (size_t j = 0; j < merged_size; ++j) {
                        results[i].merged_sources[j] = all_sources[global_idx + j];
                    }
                }
            }
#endif
        }

        return results;
    }

    /**
     * Reconstruct EDS from original and merge results.
     *
     * Builds a new EDS by combining unmodified positions with merged results.
     *
     * @param original The original EDS
     * @param merge_results Results from parallel merging
     * @return New EDS with merges applied
     */
    EDS reconstruct_eds(
        const EDS& original,
        const std::vector<MergeResult>& merge_results
    ) {
        // Build mapping: position -> merge result index (or -1 if not merged)
        std::vector<int> merge_map(original.length(), -1);
        std::vector<bool> skip(original.length(), false);

        for (size_t i = 0; i < merge_results.size(); ++i) {
            merge_map[merge_results[i].original_pos1] = static_cast<int>(i);
            skip[merge_results[i].original_pos2] = true;  // Second position consumed by merge
        }

        // Build new EDS string and sources
        std::ostringstream eds_stream;
        std::ostringstream sources_stream;
        bool has_sources = original.has_sources();
        const auto& all_sources = has_sources ? original.get_sources() : std::vector<std::set<int>>();

        for (size_t pos = 0; pos < original.length(); ++pos) {
            if (skip[pos]) {
                continue;  // Position was merged into previous
            }

            if (merge_map[pos] >= 0) {
                // Use merged result
                const auto& result = merge_results[merge_map[pos]];
                const auto& merged_set = result.merged_set;

                // Write merged symbol
                eds_stream << '{';
                for (size_t i = 0; i < merged_set.size(); ++i) {
                    if (i > 0) eds_stream << ',';
                    eds_stream << merged_set[i];
                }
                eds_stream << '}';

                // Write merged sources if present
                if (has_sources) {
                    for (size_t i = 0; i < result.merged_sources.size(); ++i) {
                        sources_stream << '{';
                        bool first = true;
                        for (int path_id : result.merged_sources[i]) {
                            if (!first) sources_stream << ',';
                            sources_stream << path_id;
                            first = false;
                        }
                        sources_stream << '}';
                    }
                }
            } else {
                // Copy original symbol
                const auto& symbol = original.read_symbol(pos);
                eds_stream << '{';
                for (size_t i = 0; i < symbol.size(); ++i) {
                    if (i > 0) eds_stream << ',';
                    eds_stream << symbol[i];
                }
                eds_stream << '}';

                // Copy original sources if present
                if (has_sources) {
                    size_t symbol_size = original.get_symbol_size(pos);
                    size_t global_idx = original.get_metadata().cum_set_sizes[pos];
                    for (size_t i = 0; i < symbol_size; ++i) {
                        const auto& src = all_sources[global_idx + i];
                        sources_stream << '{';
                        bool first = true;
                        for (int path_id : src) {
                            if (!first) sources_stream << ',';
                            sources_stream << path_id;
                            first = false;
                        }
                        sources_stream << '}';
                    }
                }
            }
        }

        // Construct new EDS
        std::string eds_str = eds_stream.str();


        if (has_sources) {
            std::string sources_str = sources_stream.str();
            return EDS(eds_str, sources_str);
        } else {
            return EDS(eds_str);
        }
    }

} // anonymous namespace

/**
 * Convert EDS to l-EDS using linear merging with phasing preservation.
 *
 * Iteratively merges adjacent positions until all internal common blocks
 * have length >= context_length. Uses parallel processing when num_threads > 1.
 *
 * @param input EDS input stream
 * @param output l-EDS output stream
 * @param context_length Minimum context length l
 * @param phasing_input Optional phasing information (.seds file)
 * @param phasing_output Optional output for updated phasing
 * @param num_threads Number of threads for parallel processing (default: 1)
 */
void eds_to_leds_linear(
    std::istream& input,
    std::ostream& output,
    Length context_length,
    std::istream* phasing_input,
    std::ostream* phasing_output,
    size_t num_threads
) {
    if (context_length == 0) {
        throw std::invalid_argument("context_length must be > 0 for l-EDS transformation");
    }

    // Load EDS (with sources if provided)
    EDS eds = phasing_input ? EDS(input, *phasing_input) : EDS(input);

    // Enforce METADATA_ONLY mode for large datasets
    // Note: Current implementation works with FULL mode, but for production
    // should add mode enforcement and streaming support

    // Iterative merging until convergence
    size_t iteration = 0;
    const size_t MAX_ITERATIONS = 10000;  // Safety limit

    while (iteration < MAX_ITERATIONS) {
        // Check convergence
        if (is_leds(eds, context_length)) {
            break;  // All internal common blocks satisfy l-EDS property
        }

        // Select independent pairs to merge
        auto pairs = select_independent_merge_pairs(eds, context_length);

        if (pairs.empty()) {
            // No more pairs to merge, but still not l-EDS
            // This can happen if degenerate symbols prevent further merging
            break;
        }

        // Merge pairs in parallel
        auto merge_results = merge_multiple_pairs(eds, pairs, num_threads);

        // Reconstruct EDS with merged results
        eds = reconstruct_eds(eds, merge_results);

        iteration++;
    }

    if (iteration >= MAX_ITERATIONS) {
        throw std::runtime_error("Maximum iterations reached without convergence");
    }

    // Write output
    eds.save(output, EDS::OutputFormat::COMPACT);

    // Write updated sources if requested
    if (phasing_output && eds.has_sources()) {
        eds.save_sources(*phasing_output);
    }
}

/**
 * Convert EDS to l-EDS using cartesian merging.
 *
 * Similar to linear merging but uses cartesian product (ignores phasing).
 * Cannot be used with source files.
 */
void eds_to_leds_cartesian(
    std::istream& input,
    std::ostream& output,
    Length context_length,
    size_t num_threads
) {
    if (context_length == 0) {
        throw std::invalid_argument("context_length must be > 0 for l-EDS transformation");
    }

    // Load EDS (without sources)
    EDS eds(input);

    if (eds.has_sources()) {
        throw std::invalid_argument("Cartesian mode cannot be used with source files");
    }

    // Iterative merging (same logic as linear, but no source handling)
    size_t iteration = 0;
    const size_t MAX_ITERATIONS = 10000;

    while (iteration < MAX_ITERATIONS) {
        if (is_leds(eds, context_length)) {
            break;
        }

        auto pairs = select_independent_merge_pairs(eds, context_length);

        if (pairs.empty()) {
            break;
        }

        auto merge_results = merge_multiple_pairs(eds, pairs, num_threads);
        eds = reconstruct_eds(eds, merge_results);

        iteration++;
    }

    if (iteration >= MAX_ITERATIONS) {
        throw std::runtime_error("Maximum iterations reached without convergence");
    }

    eds.save(output, EDS::OutputFormat::COMPACT);
}

/**
 * Check if EDS satisfies l-EDS property.
 *
 * An EDS is an l-EDS if:
 * 1. All internal common blocks have length >= l
 * 2. No two adjacent degenerate symbols (implicit empty common block)
 *
 * @param eds The EDS to check
 * @param context_length Minimum context length l
 * @return true if eds is an l-EDS
 */
bool is_leds(const EDS& eds, Length context_length) {
    if (context_length == 0) {
        return true;  // Every EDS is a 0-EDS
    }

    const auto& is_degenerate = eds.get_is_degenerate();

    // Check all positions
    for (size_t i = 0; i < eds.length(); ++i) {
        if (!is_degenerate[i]) {
            // This is a common block - get its length
            size_t global_idx = eds.get_metadata().cum_set_sizes[i];
            Length len = eds.get_string_length(global_idx);

            // Internal common blocks must have length >= context_length
            // Exception: First and last positions can be shorter
            if (i > 0 && i < eds.length() - 1 && len < context_length) {
                return false;
            }
        }

        // Check for adjacent degenerate symbols (implicit empty common block)
        // Note: Edge case exemption applies only to common blocks, not degenerate symbols
        if (i + 1 < eds.length() && is_degenerate[i] && is_degenerate[i + 1]) {
            return false;  // Two adjacent degenerate = implicit {} with length 0 < context_length
        }
    }

    return true;
}

} // namespace biofmi
