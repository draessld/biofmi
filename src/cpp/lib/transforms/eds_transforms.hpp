#ifndef BIOFMI_TRANSFORMS_EDS_TRANSFORMS_HPP
#define BIOFMI_TRANSFORMS_EDS_TRANSFORMS_HPP

#include "../common.hpp"
#include "../formats/eds.hpp"
#include <iostream>

namespace biofmi {

/**
 * EDS Transformation Functions
 *
 * This module provides transformations for Elastic-Degenerate Strings:
 * - EDS → l-EDS (length-constrained merging)
 * - Both LINEAR (phasing-aware) and CARTESIAN (all combinations) strategies
 */

/**
 * Convert EDS to l-EDS using linear merging with phasing preservation
 *
 * @param input EDS input stream
 * @param output l-EDS output stream
 * @param context_length Minimum context length
 * @param phasing_input Optional phasing information (.seds file)
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

/**
 * Check if EDS satisfies l-EDS property
 * (all internal common blocks have length >= l)
 */
bool is_leds(const EDS& eds, Length context_length);

} // namespace biofmi

#endif // BIOFMI_TRANSFORMS_EDS_TRANSFORMS_HPP
