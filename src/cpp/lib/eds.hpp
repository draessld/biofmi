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
 */
class EDS {
public:
    // Output format options
    enum class OutputFormat {
        FULL,     // Always use brackets: {ACGT}{A,ACA}{CGT}
        COMPACT   // Omit brackets on non-degenerate: ACGT{A,ACA}CGT
    };

    // Default constructor
    EDS() : is_empty_(true), has_sources_(false) {}

    // Stream-based constructors
    explicit EDS(std::istream& is);
    EDS(std::istream& eds_stream, std::istream& seds_stream);

    // String-based constructors
    explicit EDS(const std::string& eds_string);
    EDS(const std::string& eds_string, const std::string& seds_string);

    // File-based constructors
    static EDS load(const std::filesystem::path& path);
    static EDS load(const std::filesystem::path& eds_path, const std::filesystem::path& seds_path);

    // Mixed input constructors (for flexibility)
    EDS(const std::string& eds_string, std::istream& seds_stream);
    EDS(const std::string& eds_string, const std::filesystem::path& seds_path);
    EDS(const std::filesystem::path& eds_path, const std::string& seds_string);
    EDS(std::istream& eds_stream, const std::string& seds_string);

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
    std::string normalize_eds_format(const std::string& input) const;
};

} // namespace biofmi

#endif // BIOFMI_EDS_HPP
