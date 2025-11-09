#include "utils.hpp"
#include "common.hpp"
#include <fstream>
#include <sstream>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <sdsl/bit_vectors.hpp>

namespace biofmi {

// ============================================================================
// HELPER STRUCTURES
// ============================================================================

struct MSAMetadata {
    std::string ref_seq;                      // Reference (first) sequence
    std::vector<std::streampos> start_positions;  // File positions for each sequence
    size_t n_sequences;                       // Number of sequences
    size_t seq_length;                        // Length of alignment
    int line_width;                           // Characters per line in FASTA (for seeking)
};

// ============================================================================
// PASS 1: PARSE MSA METADATA AND BUILD VARIANT BIT VECTOR
// ============================================================================

/**
 * Parse MSA file to extract reference sequence and file positions.
 * Also builds variant bit vector B where:
 *   B[i] = 1 if all sequences match at position i (and no gaps)
 *   B[i] = 0 if variant or any gap present
 */
std::pair<MSAMetadata, sdsl::bit_vector> parse_msa_and_build_variant_bv(std::istream& in) {
    MSAMetadata meta;
    std::string line;
    uint64_t counter = 0;
    size_t i = 0;
    sdsl::bit_vector B;

    meta.line_width = -1;  // Will be set from first line

    // Read MSA and build variant bit vector
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;  // Skip empty lines
        }

        if (line[0] == '>') {
            // Header line - new sequence
            if (counter == 1) {
                // After reading first sequence, initialize bit vector
                // All positions start as 1 (common), will be set to 0 for variants
                B = sdsl::bit_vector(meta.ref_seq.size() + 1, 1);
            }
            i = 0;  // Reset position for new sequence
            counter++;
            meta.start_positions.push_back(in.tellg());
        }
        else if (counter == 1) {
            // First sequence - use as reference
            meta.ref_seq += line;
            if (meta.line_width == -1) {
                meta.line_width = line.size();
            }
        }
        else {
            // Subsequent sequences - compare with reference
            for (size_t j = 0; j < line.size(); j++) {
                // Mark as variant if:
                // 1. Character doesn't match reference
                // 2. Character is a gap (-)
                if (line[j] != meta.ref_seq[i] || line[j] == '-') {
                    B[i] = 0;
                }
                i++;
            }
        }
    }

    // Set sentinel at end (flip last bit)
    B[meta.ref_seq.size()] = B[meta.ref_seq.size() - 1] ^ 1;

    meta.n_sequences = counter;  // counter is incremented for each '>' header line
    meta.seq_length = meta.ref_seq.size();

    return {meta, B};
}

// ============================================================================
// PASS 2a: BUILD SYMBOL BOUNDARIES FOR EDS (NO MERGING)
// ============================================================================

/**
 * Build symbol boundary bit vector H for EDS mode (no merging).
 * H[i] = 1 means position i starts a new symbol.
 * Every transition in B creates a new symbol.
 */
sdsl::bit_vector build_eds_boundaries(const sdsl::bit_vector& B) {
    sdsl::bit_vector H(B.size(), 0);

    // First position always starts a symbol
    H[0] = 1;

    // Mark boundaries at every transition in B
    for (size_t i = 1; i < B.size(); i++) {
        if (B[i] != B[i-1]) {
            H[i] = 1;
        }
    }

    return H;
}

// ============================================================================
// PASS 2b: BUILD MERGE BOUNDARIES FOR l-EDS (WITH MERGING)
// ============================================================================

/**
 * Build merge boundary bit vector H for l-EDS mode.
 * H[i] = 1 means position i starts a new symbol.
 * Only common runs with length >= context_length are kept as standalone symbols.
 * Short common runs are merged with adjacent variant regions.
 *
 * Algorithm (similar to old.cpp lines 72-109):
 * - Scan B to find runs of 1s (common) and 0s (variant)
 * - Long common runs (>= context_length) become standalone symbols
 * - Short common runs merge with adjacent variants
 * - Special cases: first and last positions
 */
sdsl::bit_vector build_leds_boundaries(const sdsl::bit_vector& B,
                                       size_t context_length,
                                       const std::string& ref_seq) {
    sdsl::bit_vector H(B.size(), 0);

    // Build select support structures
    sdsl::bit_vector::select_1_type select_one(&B);
    sdsl::bit_vector::select_0_type select_zero(&B);

    size_t zeros = 0, ones = 0;
    size_t i = 0;
    bool prev_was_standalone = false;  // Track if previous common run was standalone

    while (i < ref_seq.size()) {
        if (B[i]) {
            // Common region (run of 1s)
            size_t next_zero = select_zero(zeros + 1);
            size_t run_length = next_zero - i;

            // Check if this should be a standalone symbol
            bool is_standalone = (run_length >= context_length || i == 0 || next_zero == ref_seq.size());

            if (is_standalone) {
                H[i] = 1;  // Mark start of standalone common run
                prev_was_standalone = true;
            } else {
                // Short common run - will be merged
                // But if previous was standalone, we need to mark the transition
                if (prev_was_standalone) {
                    H[i] = 1;  // Start new merged region after standalone
                }
                prev_was_standalone = false;
            }

            ones += run_length;
            i = next_zero;
        }
        else {
            // Variant region (run of 0s)
            size_t next_one = select_one(ones + 1);
            size_t run_length = next_one - i;

            // If previous common run was standalone, mark start of this variant region
            if (prev_was_standalone) {
                H[i] = 1;
                prev_was_standalone = false;
            }

            zeros += run_length;
            i = next_one;
        }
    }

    // Ensure first position is always marked
    H[0] = 1;

    return H;
}

// ============================================================================
// PASS 3: GENERATE EDS/l-EDS OUTPUT WITH SOURCES
// ============================================================================

/**
 * Generate EDS or l-EDS string and sources from bit vectors.
 * Uses file seeking to read sequence data on-demand.
 */
std::pair<std::string, std::string> generate_output(
    std::istream& in,
    const MSAMetadata& meta,
    const sdsl::bit_vector& B,
    const sdsl::bit_vector& H)
{
    std::ostringstream eds_out;
    std::ostringstream seds_out;

    // Build select support for H
    sdsl::bit_vector::select_1_type select_h(&H);

    // Count number of symbols (number of 1s in H, excluding sentinel)
    size_t n_symbols = 0;
    for (size_t i = 0; i < meta.ref_seq.size(); i++) {
        if (H[i]) n_symbols++;
    }

    // Buffer for reading sequence data
    std::vector<char> buffer(meta.seq_length + (meta.seq_length / meta.line_width) + 10);

    // Process each symbol
    for (size_t sym_idx = 0; sym_idx < n_symbols; sym_idx++) {
        size_t start_pos = select_h(sym_idx + 1);  // select is 1-indexed
        size_t end_pos;

        if (sym_idx + 1 < n_symbols) {
            end_pos = select_h(sym_idx + 2);
        } else {
            end_pos = meta.ref_seq.size();
        }

        size_t region_length = end_pos - start_pos;

        // Check if this region is common (all 1s in B) or variant (contains 0s)
        bool is_common = true;
        for (size_t i = start_pos; i < end_pos; i++) {
            if (B[i] == 0) {
                is_common = false;
                break;
            }
        }

        eds_out << '{';

        if (is_common) {
            // Common region - output from reference sequence only
            // Read from reference (first sequence)
            size_t chars_read = 0;
            for (size_t i = start_pos; i < end_pos; i++) {
                if (meta.ref_seq[i] != '-') {
                    eds_out << meta.ref_seq[i];
                    chars_read++;
                }
            }

            // Source: {0} for universal path (one string in this symbol)
            seds_out << "{0}";
        }
        else {
            // Variant region - collect alternatives from all sequences
            // Map: string -> set of path IDs
            std::map<std::string, std::set<int>> variant_to_paths;
            std::vector<std::string> insertion_order;  // Track order of first appearance

            // Process each sequence
            for (size_t seq_idx = 0; seq_idx < meta.n_sequences; seq_idx++) {
                // Calculate file position for this region in this sequence
                std::streampos file_pos = meta.start_positions[seq_idx] +
                                         static_cast<std::streamoff>(start_pos + (start_pos / meta.line_width));

                // Calculate how many bytes to read (including newlines)
                size_t tmp = static_cast<size_t>((start_pos % meta.line_width) + region_length) / meta.line_width;
                size_t bytes_to_read = region_length + tmp;

                // Seek and read
                in.clear();
                in.seekg(file_pos);
                in.read(buffer.data(), bytes_to_read);

                // Extract actual sequence (skip newlines and gaps)
                std::string variant;
                for (size_t k = 0; k < bytes_to_read && buffer[k] != '\0'; k++) {
                    if (buffer[k] != '\n' && buffer[k] != '-') {
                        variant.push_back(buffer[k]);
                    }
                }

                // Add to map (path IDs are 1-indexed)
                int path_id = seq_idx + 1;
                if (variant_to_paths.find(variant) == variant_to_paths.end()) {
                    insertion_order.push_back(variant);
                }
                variant_to_paths[variant].insert(path_id);
            }

            // Output variants in order of first appearance
            for (size_t v = 0; v < insertion_order.size(); v++) {
                const std::string& variant = insertion_order[v];
                const std::set<int>& paths = variant_to_paths[variant];

                // Output variant string
                eds_out << variant;
                if (v < insertion_order.size() - 1) {
                    eds_out << ',';
                }

                // Output source set (one set per string alternative)
                seds_out << '{';
                auto it = paths.begin();
                for (size_t p = 0; p < paths.size(); p++, it++) {
                    seds_out << *it;
                    if (p < paths.size() - 1) {
                        seds_out << ',';
                    }
                }
                seds_out << '}';
            }
        }

        eds_out << '}';
    }

    return {eds_out.str(), seds_out.str()};
}

// ============================================================================
// PUBLIC API FUNCTIONS
// ============================================================================

/**
 * Transform MSA to EDS (no merging) with source tracking.
 * Returns pair: (EDS string, sEDS string)
 */
std::pair<std::string, std::string> parse_msa_to_eds_streaming(std::istream& msa_stream) {
    // Pass 1: Parse MSA and build variant bit vector
    auto [meta, B] = parse_msa_and_build_variant_bv(msa_stream);

    // Pass 2: Build symbol boundaries (every transition)
    sdsl::bit_vector H = build_eds_boundaries(B);

    // Pass 3: Generate output
    msa_stream.clear();
    msa_stream.seekg(0);
    return generate_output(msa_stream, meta, B, H);
}

/**
 * Transform MSA to l-EDS (with merging) with source tracking.
 * Returns pair: (l-EDS string, sEDS string)
 */
std::pair<std::string, std::string> parse_msa_to_leds_streaming(
    std::istream& msa_stream,
    size_t context_length)
{
    // Pass 1: Parse MSA and build variant bit vector
    auto [meta, B] = parse_msa_and_build_variant_bv(msa_stream);

    // Pass 2: Build merge boundaries
    sdsl::bit_vector H = build_leds_boundaries(B, context_length, meta.ref_seq);

    // Pass 3: Generate output
    msa_stream.clear();
    msa_stream.seekg(0);
    return generate_output(msa_stream, meta, B, H);
}

} // namespace biofmi
