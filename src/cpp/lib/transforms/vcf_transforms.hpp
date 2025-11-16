#ifndef BIOFMI_TRANSFORMS_VCF_TRANSFORMS_HPP
#define BIOFMI_TRANSFORMS_VCF_TRANSFORMS_HPP

#include "../common.hpp"
#include <iostream>
#include <string>
#include <utility>

namespace biofmi {

/**
 * VCF Transformation Functions
 *
 * This module provides transformations from VCF (Variant Call Format) files
 * to Elastic-Degenerate Strings (EDS) and length-constrained EDS (l-EDS).
 *
 * Uses streaming approach for FASTA reference - only active regions loaded.
 * Sources track samples at the sample level (one path per sample).
 */

/**
 * Parse VCF + FASTA reference to EDS with source tracking.
 *
 * Source tracking: Sample-level (diploid samples contribute to one path).
 * Path IDs are 1-indexed, matching sample order in VCF.
 *
 * Handles:
 * - SNPs and small indels
 * - Simple deletions (<DEL>)
 * - Simple insertions (<INS>)
 * - Multi-allelic sites (multiple ALT alleles)
 *
 * Skips with warnings:
 * - Overlapping variants
 * - Complex structural variants (<INV>, <CN*>, mobile elements)
 * - Malformed VCF lines
 *
 * @param vcf_stream Input stream containing VCF file
 * @param fasta_stream Input stream containing reference FASTA
 * @return Pair of (EDS string, sEDS source string)
 */
std::pair<std::string, std::string> parse_vcf_to_eds_streaming(
    std::istream& vcf_stream,
    std::istream& fasta_stream);

/**
 * Parse VCF + FASTA reference directly to l-EDS with source tracking.
 *
 * Uses existing parse_vcf_to_eds_streaming() + eds_to_leds_linear() pipeline.
 *
 * @param vcf_stream Input stream containing VCF file
 * @param fasta_stream Input stream containing reference FASTA
 * @param context_length Minimum context length for l-EDS
 * @return Pair of (l-EDS string, sEDS source string)
 */
std::pair<std::string, std::string> parse_vcf_to_leds_streaming(
    std::istream& vcf_stream,
    std::istream& fasta_stream,
    size_t context_length);

} // namespace biofmi

#endif // BIOFMI_TRANSFORMS_VCF_TRANSFORMS_HPP
