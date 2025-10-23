#ifndef BIOFMI_UTILS_HPP
#define BIOFMI_UTILS_HPP

#include "common.hpp"
#include "eds.hpp"

#include <iostream>
#include <filesystem>

namespace biofmi {

/**
 * Utility functions for format conversion and data processing
 */

// File type detection
enum class FileFormat {
    MSA,
    VCF,
    EDS,
    EDZ,
    LEDS,
    UNKNOWN
};

FileFormat detect_format(const std::filesystem::path& filepath);

// MSA to l-EDS conversion
/**
 * Convert MSA to l-EDS using linear merging strategy
 *
 * Linear strategy: Only merge adjacent degenerate symbols if necessary
 * to maintain minimum context length l.
 */
void msa_to_leds_linear(
    std::istream& input,
    std::ostream& output,
    Length context_length
);

/**
 * Convert MSA to l-EDS using cartesian merging strategy
 *
 * Cartesian strategy: Create cartesian product of adjacent degenerate
 * symbols to ensure context length >= l. This can lead to exponential
 * growth in the number of alternatives.
 */
void msa_to_leds_cartesian(
    std::istream& input,
    std::ostream& output,
    Length context_length
);

// VCF to l-EDS conversion
/**
 * Convert VCF to phased l-EDS using linear merging
 *
 * Preserves phasing information from VCF.
 */
void vcf_to_leds_linear(
    std::istream& input,
    std::ostream& output,
    Length context_length
);

/**
 * Convert VCF to l-EDS using cartesian merging
 */
void vcf_to_leds_cartesian(
    std::istream& input,
    std::ostream& output,
    Length context_length
);

// EDS to l-EDS conversion
/**
 * Convert EDS to l-EDS using linear merging with phasing preservation
 *
 * @param input EDS input stream
 * @param output l-EDS output stream
 * @param context_length Minimum context length
 * @param phasing_input Optional phasing information (.edp file)
 * @param phasing_output Optional output for updated phasing
 */
void eds_to_leds_linear(
    std::istream& input,
    std::ostream& output,
    Length context_length,
    std::istream* phasing_input = nullptr,
    std::ostream* phasing_output = nullptr
);

/**
 * Convert EDS to l-EDS using cartesian merging
 */
void eds_to_leds_cartesian(
    std::istream& input,
    std::ostream& output,
    Length context_length
);

// Helper functions
/**
 * Check if EDS satisfies l-EDS property
 * (all internal common blocks have length >= l)
 */
bool is_leds(const EDS& eds, Length context_length);

/**
 * Parse MSA format into vector of sequences
 */
std::vector<String> parse_msa(std::istream& input);

/**
 * Build EDS from aligned sequences (MSA)
 * Uses min-max strategy: maximal common blocks, minimal degenerate symbols
 */
EDS build_eds_from_msa(const std::vector<String>& sequences);

/**
 * Memory usage reporting
 */
size_t get_current_memory_usage_kb();
size_t get_peak_memory_usage_kb();

/**
 * Timing utilities
 */
class Timer {
public:
    Timer();
    void start();
    void stop();
    double elapsed_seconds() const;
    double elapsed_milliseconds() const;
    double elapsed_microseconds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace biofmi

#endif // BIOFMI_UTILS_HPP
