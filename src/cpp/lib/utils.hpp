#ifndef BIOFMI_UTILS_HPP
#define BIOFMI_UTILS_HPP

/**
 * Legacy compatibility header for BIOFMI utilities
 *
 * This header provides backward compatibility by forwarding to the new
 * modular structure. New code should include the specific headers directly:
 * - formats/eds.hpp - EDS data structure
 * - transforms/eds_transforms.hpp - EDS transformations
 * - transforms/msa_transforms.hpp - MSA transformations
 * - index/index.hpp - BioFMI index
 */

// Forward to new modular headers
#include "common.hpp"
#include "formats/eds.hpp"
#include "transforms/eds_transforms.hpp"
#include "transforms/msa_transforms.hpp"
#include "index/index.hpp"

#endif // BIOFMI_UTILS_HPP
