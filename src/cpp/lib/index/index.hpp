#ifndef BIOFMI_INDEX_HPP
#define BIOFMI_INDEX_HPP

#include <edsparser/common.hpp>
#include <edsparser/formats/eds.hpp>

#include <filesystem>
#include <unordered_map>
#include <memory>
#include <vector>
#include <iostream>

// SDSL headers (needed for type definitions)
#include <sdsl/suffix_arrays.hpp>

namespace biofmi {

// Import types from edsparser
using edsparser::Position;
using edsparser::Length;
using edsparser::String;
using edsparser::StringSet;
using edsparser::EDS;
using edsparser::SET_OPEN;
using edsparser::SET_CLOSE;
using edsparser::SET_SEPARATOR;
using edsparser::CHANGE_SEPARATOR;

/**
 * BIO-FMI Index
 *
 * Hybrid index combining FM-index structures for reference and changes.
 * Uses wavelet tree-based FM-index (SDSL library) for efficient pattern matching.
 */
class BioFMI {
public:
    // FM-index type (defined using SDSL)
    using IndexType = sdsl::csa_wt<>;

    // Result type: map from sequence ID to vector of (position, changes) pairs
    using ResultMap = std::unordered_map<int, std::vector<std::pair<Position, std::vector<int>>>>;

    // Constructor: build index from EDS (takes ownership via move)
    BioFMI(EDS&& eds, Length context_length);

    // Constructor: build index from file
    BioFMI(const std::filesystem::path& eds_file, Length context_length);

    // Constructor: load existing index
    explicit BioFMI(const std::filesystem::path& index_dir);

    // Destructor
    ~BioFMI();

    // Delete copy constructor/assignment (index is heavy)
    BioFMI(const BioFMI&) = delete;
    BioFMI& operator=(const BioFMI&) = delete;

    // Move constructor/assignment
    BioFMI(BioFMI&&) noexcept;
    BioFMI& operator=(BioFMI&&) noexcept;

    // Build the index structures
    void build();

    // Save index to disk
    void save(const std::filesystem::path& output_dir);

    // Load index from disk
    void load(const std::filesystem::path& index_dir);

    // Dump internal structures in human-readable text form (for inspection/testing)
    void dump_readable(const std::filesystem::path& dump_path) const;

    // Query operations
    ResultMap locate(const String& pattern);
    size_t count(const String& pattern);

    // Statistics and information
    struct IndexStats {
        Length context_length;
        double total_size_mb;
        double reference_index_size_mb;
        double changes_index_size_mb;
        size_t num_changes;
        size_t reference_length;
        size_t changes_total_length;
    };

    IndexStats get_statistics() const;
    void print_statistics(std::ostream& os = std::cout) const;

    // Get the last query results
    const ResultMap& get_last_result() const { return last_result_; }

    // Print results in human-readable format
    void print_result(const ResultMap& result, std::ostream& os = std::cout) const;

private:
    // Hash map types for tracking occurrences during locate
    using OccurrenceInfo = std::pair<Position, std::vector<int>>;
    using HashType = std::unordered_map<Position, std::vector<OccurrenceInfo>>;

    // Index data (PIMPL pattern to hide some SDSL details)
    struct IndexData;
    std::unique_ptr<IndexData> data_;

    Length context_length_;
    EDS eds_;
    ResultMap last_result_;

    // Index directory and metadata files
    std::filesystem::path index_dir_;
    std::filesystem::path reference_filepath_;
    std::filesystem::path changes_filepath_;

    // EDS statistics (cached from eds_)
    size_t n_;  // Number of sets
    size_t m_;  // Total number of strings
    size_t N_;  // Total size

    // Hash maps for chunk-based locate
    HashType new_hash_map_;
    HashType old_hash_map_;

    // Internal methods
    void parse_eds();
    void build_reference_index();
    void build_changes_index();
    void build_metadata_structures();

    // Locate helper methods
    void process_reference_matches(const String& chunk, size_t chunk_idx);
    void process_changes_matches(const String& chunk, size_t chunk_idx);
    void validate_change_continuity(int loc, int change_offset, int change_number,
                                   int block_number, bool previous_outside_change);
    ResultMap convert_hash_to_result(const HashType& hash_map);


};

} // namespace biofmi

#endif // BIOFMI_INDEX_HPP
