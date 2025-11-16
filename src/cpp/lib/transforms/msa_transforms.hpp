#ifndef BIOFMI_TRANSFORMS_MSA_TRANSFORMS_HPP
#define BIOFMI_TRANSFORMS_MSA_TRANSFORMS_HPP

#include "../common.hpp"
#include <iostream>
#include <string>
#include <utility>

namespace biofmi {

/**
 * MSA Transformation Functions
 *
 * This module provides transformations from Multiple Sequence Alignments (MSA)
 * to Elastic-Degenerate Strings (EDS) and length-constrained EDS (l-EDS).
 *
 * Uses streaming approach - only reference sequence kept in memory.
 */

/**
 * Parse MSA (Multiple Sequence Alignment) to EDS with source tracking.
 * Uses streaming approach - only reference sequence kept in memory.
 *
 * @param msa_stream Input stream containing MSA in FASTA format (with gaps as '-')
 * @return Pair of (EDS string, sEDS source string)
 */
std::pair<std::string, std::string> parse_msa_to_eds_streaming(std::istream& msa_stream);

/**
 * Parse MSA directly to l-EDS with source tracking.
 * Uses streaming approach with merging based on context length.
 *
 * @param msa_stream Input stream containing MSA in FASTA format (with gaps as '-')
 * @param context_length Minimum context length for l-EDS
 * @return Pair of (l-EDS string, sEDS source string)
 */
std::pair<std::string, std::string> parse_msa_to_leds_streaming(
    std::istream& msa_stream,
    size_t context_length);

} // namespace biofmi

#endif // BIOFMI_TRANSFORMS_MSA_TRANSFORMS_HPP
