#ifndef BIOFMI_UTILS_HPP
#define BIOFMI_UTILS_HPP

#include "common.hpp"
#include "eds.hpp"

#include <iostream>
#include <filesystem>

namespace biofmi {

/**
 * Utility functions for format conversion and data processing
 *
 * NOTE: Most functions in this header are NOT YET IMPLEMENTED.
 * They are placeholders for future MSA/VCF parsing functionality.
 * Current EDS→l-EDS implementation is in transform_utils.hpp/cpp
 */

// TODO: MSA/VCF parsing - Not implemented yet
// Will be added when MSA/VCF support is implemented

// File type detection - NOT IMPLEMENTED
// (transform.cpp has its own FileType enum and detection)
// enum class FileFormat { MSA, VCF, EDS, EDZ, LEDS, UNKNOWN };
// FileFormat detect_format(const std::filesystem::path& filepath);

// MSA to l-EDS conversion - NOT IMPLEMENTED
// void msa_to_leds_linear(std::istream& input, std::ostream& output, Length context_length);
// void msa_to_leds_cartesian(std::istream& input, std::ostream& output, Length context_length);

// VCF to l-EDS conversion - NOT IMPLEMENTED
// void vcf_to_leds_linear(std::istream& input, std::ostream& output, Length context_length);
// void vcf_to_leds_cartesian(std::istream& input, std::ostream& output, Length context_length);

// EDS to l-EDS conversion
/**
 * Convert EDS to l-EDS using linear merging with phasing preservation
 *
 * @param input EDS input stream
 * @param output l-EDS output stream
 * @param context_length Minimum context length
 * @param phasing_input Optional phasing information (.edp file)
 * @param phasing_output Optional output for updated phasing
 * @param num_threads Number of threads for parallel processing (default: 1)
 */
void eds_to_leds_linear(
    std::istream& input,
    std::ostream& output,
    Length context_length,
    std::istream* phasing_input = nullptr,
    std::ostream* phasing_output = nullptr,
    size_t num_threads = 1
);

/**
 * Convert EDS to l-EDS using cartesian merging
 *
 * @param num_threads Number of threads for parallel processing (default: 1)
 */
void eds_to_leds_cartesian(
    std::istream& input,
    std::ostream& output,
    Length context_length,
    size_t num_threads = 1
);

// Helper functions
/**
 * Check if EDS satisfies l-EDS property
 * (all internal common blocks have length >= l)
 * IMPLEMENTED in transform_utils.cpp
 */
bool is_leds(const EDS& eds, Length context_length);

// NOT IMPLEMENTED - future MSA support
// std::vector<String> parse_msa(std::istream& input);
// EDS build_eds_from_msa(const std::vector<String>& sequences);

// Memory usage reporting - NOT IMPLEMENTED
// size_t get_current_memory_usage_kb();
// size_t get_peak_memory_usage_kb();

// NOTE: Timer class has been moved to common.hpp

} // namespace biofmi

#endif // BIOFMI_UTILS_HPP
