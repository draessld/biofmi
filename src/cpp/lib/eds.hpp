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
 * Empty strings are represented as empty entries between commas.
 */
class EDS {
public:
    // Constructor from input stream
    explicit EDS(std::istream& is);

    // Constructor from EDS + sEDS streams
    EDS(std::istream& eds_stream, std::istream& seds_stream);

    // Default constructor
    EDS() : is_empty_(true), has_sources_(false) {}

    // Destructor
    ~EDS() = default;

    // Copy and move constructors/assignments
    EDS(const EDS&) = default;
    EDS& operator=(const EDS&) = default;
    EDS(EDS&&) = default;
    EDS& operator=(EDS&&) = default;

    // Query methods
    bool empty() const { return is_empty_; }
    size_t length() const { return n_; }           // Number of sets
    size_t size() const { return N_; }             // Total characters
    size_t cardinality() const { return m_; }      // Total number of strings
    bool has_sources() const { return has_sources_; }  // Whether sources are loaded

    // Statistics
    struct Statistics {
        Length min_context_length;
        Length max_context_length;
        double avg_context_length;
        size_t num_degenerate_symbols;
        size_t num_common_chars;
        size_t total_change_size;
        size_t num_empty_strings;
    };

    Statistics get_statistics() const;
    void print_statistics(std::ostream& os = std::cout) const;

    // Output methods
    void print(std::ostream& os = std::cout) const;
    void save(std::ostream& os) const;
    void save(const std::filesystem::path& path) const;
    void save_sources(std::ostream& os) const;  // Save sEDS format
    void save_sources(const std::filesystem::path& path) const;  // Save sEDS to file

    // Loading methods
    static EDS load(const std::filesystem::path& path);  // Load EDS from file
    void load_sources(std::istream& is);  // Load sources from sEDS stream
    void load_sources(const std::filesystem::path& path);  // Load sources from sEDS file

    // Pattern generation for benchmarking
    void generate_patterns(std::ostream& os, size_t count, Length pattern_length) const;

    // Extract substring from EDS
    String extract(Position pos, Length len, const std::vector<int>& changes) const;

    // Access to internal data
    const std::vector<StringSet>& get_sets() const { return sets_; }
    const std::vector<bool>& get_is_degenerate() const { return is_degenerate_; }
    const std::vector<std::set<int>>& get_sources() const { return sources_; }

private:
    bool is_empty_;
    size_t n_;                          // Number of sets
    size_t N_;                          // Total size (characters)
    size_t m_;                          // Cardinality (number of strings in all sets)

    std::vector<StringSet> sets_;       // The actual EDS data
    std::vector<bool> is_degenerate_;   // True if set has more than one element
    std::vector<Length> set_sizes_;     // Size of each set
    std::vector<Length> cum_set_sizes_; // Cumulative set sizes

    // Optional source support
    bool has_sources_;                           // Whether sources are loaded
    std::vector<std::set<int>> sources_;         // Path IDs per string (indexed by string ID)

    Statistics stats_;

    // Helper methods
    void parse(std::istream& is);
    void parse_sources(std::istream& is);
    void calculate_statistics();
    double calculate_size_in_bytes() const;
};

} // namespace biofmi

#endif // BIOFMI_EDS_HPP
