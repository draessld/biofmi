#ifndef BIOFMI_EDS_HPP
#define BIOFMI_EDS_HPP

#include "common.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <set>
#include <fstream>
#include <filesystem>

namespace biofmi {

/**
 * Elastic-Degenerate String (EDS) representation
 *
 * An EDS is a sequence where each position can contain multiple alternative strings.
 * Format: {str1,str2,...}{str3}{str4,str5}...
 * Compact format (optional): str1{str2,str3}str4 (brackets only on degenerate symbols)
 * Empty strings are represented as empty entries between commas.
 *
 * Storage modes:
 * - FULL: All strings loaded into RAM (default, backward compatible)
 * - METADATA_ONLY: Only metadata/index loaded, strings streamed on-demand (memory-efficient)
 */
class EDS {
public:
    // Storage mode options
    enum class StoringMode {
        FULL,           // All strings in RAM (default)
        METADATA_ONLY   // Only metadata, stream strings on-demand
    };

    // Output format options
    enum class OutputFormat {
        FULL,     // Always use brackets: {ACGT}{A,ACA}{CGT}
        COMPACT   // Omit brackets on non-degenerate: ACGT{A,ACA}CGT
    };

    // Default constructor
    EDS() : is_empty_(true), mode_(StoringMode::FULL), has_sources_(false) {}

    // Stream-based constructors
    explicit EDS(std::istream& is);
    EDS(std::istream& eds_stream, std::istream& seds_stream);

    // String-based constructors
    explicit EDS(const std::string& eds_string);
    EDS(const std::string& eds_string, const std::string& seds_string);

    // File-based constructors (with optional StoringMode)
    static EDS load(const std::filesystem::path& path, StoringMode mode = StoringMode::FULL);
    static EDS load(const std::filesystem::path& eds_path, const std::filesystem::path& seds_path, StoringMode mode = StoringMode::FULL);

    // Mixed input constructors (for flexibility)
    EDS(const std::string& eds_string, std::istream& seds_stream);
    EDS(const std::string& eds_string, const std::filesystem::path& seds_path);
    EDS(const std::filesystem::path& eds_path, const std::string& seds_string);
    EDS(std::istream& eds_stream, const std::string& seds_string);

    // Destructor
    ~EDS() = default;

    // Copy and move constructors/assignments
    // Note: Copy is deleted because of ifstream member (non-copyable in METADATA_ONLY mode)
    EDS(const EDS&) = delete;
    EDS& operator=(const EDS&) = delete;
    EDS(EDS&&) = default;
    EDS& operator=(EDS&&) = default;

    // Query methods
    bool empty() const { return is_empty_; }
    size_t length() const { return n_; }           // Number of sets
    size_t size() const { return N_; }             // Total characters
    size_t cardinality() const { return m_; }      // Total number of strings
    bool has_sources() const { return has_sources_; }  // Whether sources are loaded
    StoringMode get_storing_mode() const { return mode_; }  // Get storage mode

    // Metadata structure (combines index data and statistics)
    // This is the core of memory-efficient streaming EDS
    struct Metadata {
        // Index data (position/size information)
        std::vector<std::streampos> base_positions;   // Starting position of each symbol in file
        std::vector<Length> symbol_sizes;             // Number of strings per symbol (n entries)
        std::vector<Length> string_lengths;           // Length of each string (m entries total)
        std::vector<Length> cum_set_sizes;            // Cumulative string IDs (for mapping)
        std::vector<bool> is_degenerate;              // Degenerate flag per symbol

        // Statistics (computed from index data)
        Length min_context_length;        // Minimum non-degenerate symbol length
        Length max_context_length;        // Maximum non-degenerate symbol length
        double avg_context_length;        // Average non-degenerate symbol length
        size_t num_degenerate_symbols;    // Count of degenerate symbols
        size_t num_common_chars;          // Total chars in non-degenerate symbols
        size_t total_change_size;         // Total chars in degenerate symbols
        size_t num_empty_strings;         // Count of empty string alternatives

        // Source statistics (only meaningful if sources are loaded)
        size_t num_paths;                 // Total number of distinct path IDs
        size_t max_paths_per_string;      // Maximum paths in any single string
        double avg_paths_per_string;      // Average paths per string

        // Position checking support (computed from index data)
        std::vector<Position> cum_common_positions;   // Cumulative common chars before each symbol (n+1 entries)
        std::vector<int> cum_degenerate_counts;       // Cumulative degenerate strings before each symbol (n+1 entries)
    };

    // Statistics (for backward compatibility, returns statistics portion of metadata)
    struct Statistics {
        Length min_context_length;
        Length max_context_length;
        double avg_context_length;
        size_t num_degenerate_symbols;
        size_t num_common_chars;
        size_t total_change_size;
        size_t num_empty_strings;

        // Source statistics
        size_t num_paths;
        size_t max_paths_per_string;
        double avg_paths_per_string;
    };

    const Metadata& get_metadata() const { return metadata_; }  // Get full metadata
    Statistics get_statistics() const;                           // Get statistics only
    void print_statistics(std::ostream& os = std::cout) const;

    // Output methods
    void print(std::ostream& os = std::cout) const;
    void save(std::ostream& os, OutputFormat format = OutputFormat::FULL) const;
    void save(const std::filesystem::path& path, OutputFormat format = OutputFormat::FULL) const;
    void save_sources(std::ostream& os) const;  // Save sEDS format
    void save_sources(const std::filesystem::path& path) const;  // Save sEDS to file

    // Loading methods (sources only - EDS loading is via constructors/static load)
    void load_sources(std::istream& is);  // Load sources from sEDS stream
    void load_sources(const std::filesystem::path& path);  // Load sources from sEDS file
    void load_sources(const std::string& seds_string);  // Load sources from sEDS string

    // Pattern generation for benchmarking
    void generate_patterns(std::ostream& os, size_t count, Length pattern_length) const;

    // Extract substring from EDS
    String extract(Position pos, Length len, const std::vector<int>& changes) const;

    // Position checking: verify if pattern occurs at position with given degenerate string choices
    bool check_position(Position common_pos,
                       const std::vector<int>& degenerate_strings,
                       const String& pattern) const;

    // Merging: merge two adjacent symbols (degenerate or non-degenerate)
    // Example: {G,C} + {T} → {GT,CT}
    // Example: {T} + {A,C,G} → {TA,TC,TG}
    // Example: {G,C} + {T} + {A,C} would require two calls: merge(0,1) then merge(0,1) again
    // Behavior depends on whether sources are loaded:
    //   - WITHOUT sources: CARTESIAN merge (all combinations of alternatives)
    //   - WITH sources: LINEAR merge (only combinations with valid source intersection)
    // Returns new EDS with merged positions (original EDS unchanged)
    // Throws: std::invalid_argument if positions not adjacent (pos2 != pos1 + 1)
    EDS merge_adjacent(size_t pos1, size_t pos2) const;

    // Access to internal data
    const std::vector<StringSet>& get_sets() const;  // Throws if METADATA_ONLY mode
    const std::vector<bool>& get_is_degenerate() const { return metadata_.is_degenerate; }
    const std::vector<std::set<int>>& get_sources() const { return sources_; }

    // Streaming access (works in both modes)
    StringSet read_symbol(Position pos) const;  // Read symbol from file or memory
    Length get_symbol_size(Position pos) const { return metadata_.symbol_sizes[pos]; }
    std::streampos get_base_position(Position pos) const { return metadata_.base_positions[pos]; }
    Length get_string_length(size_t string_id) const { return metadata_.string_lengths[string_id]; }

private:
    // Core state
    bool is_empty_;
    size_t n_;                          // Number of sets
    size_t N_;                          // Total size (characters)
    size_t m_;                          // Cardinality (number of strings in all sets)
    StoringMode mode_;                  // Storage mode

    // Metadata (always present, contains index + statistics)
    Metadata metadata_;

    // String data (only if mode_ == FULL)
    std::vector<StringSet> sets_;       // The actual EDS data
    std::vector<Length> set_sizes_;     // Size of each set (redundant with metadata, kept for compatibility)

    // File streaming (only if mode_ == METADATA_ONLY)
    std::filesystem::path file_path_;
    mutable std::ifstream stream_;      // Mutable to allow reading in const methods

    // Optional source support
    bool has_sources_;                           // Whether sources are loaded
    std::vector<std::set<int>> sources_;         // Path IDs per string (indexed by string ID)

    // Helper methods
    void parse(std::istream& is);
    void parse_sources(std::istream& is);
    void calculate_statistics();
    void calculate_source_statistics();
    std::string normalize_eds_format(const std::string& input) const;

    // Streaming helpers
    StringSet read_symbol_from_stream(Position pos) const;

    // Position checking helpers
    std::pair<size_t, size_t> decode_degenerate_string_number(int abs_string_num) const;
    size_t find_symbol_at_common_position(Position common_pos, Position& offset_out) const;
    String reconstruct_from_memory(size_t start_symbol,
                                   Position offset_in_symbol,
                                   const std::vector<int>& degenerate_strings,
                                   Length pattern_length) const;
    String reconstruct_from_file(size_t start_symbol,
                                 Position offset_in_symbol,
                                 const std::vector<int>& degenerate_strings,
                                 Length pattern_length) const;
    std::set<int> calculate_path_intersection(size_t start_symbol,
                                              Position offset_in_symbol,
                                              const std::vector<int>& degenerate_strings,
                                              Length pattern_length) const;
};

} // namespace biofmi

#endif // BIOFMI_EDS_HPP
