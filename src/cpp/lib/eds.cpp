#include "eds.hpp"
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace biofmi {

// Stream-based constructor
EDS::EDS(std::istream& is) : is_empty_(false), has_sources_(false) {
    parse(is);
}

// String-based constructor
EDS::EDS(const std::string& eds_string) : is_empty_(false), has_sources_(false) {
    std::stringstream ss(eds_string);
    parse(ss);
}

// String-based constructor with sources
EDS::EDS(const std::string& eds_string, const std::string& seds_string) : is_empty_(false), has_sources_(false) {
    std::stringstream eds_ss(eds_string);
    std::stringstream seds_ss(seds_string);
    parse(eds_ss);
    parse_sources(seds_ss);
}

void EDS::parse(std::istream& is) {
    // Read entire input into string for easier parsing
    std::stringstream buffer;
    buffer << is.rdbuf();
    std::string input = buffer.str();

    // Remove whitespace
    input.erase(std::remove_if(input.begin(), input.end(), ::isspace), input.end());

    if (input.empty()) {
        is_empty_ = true;
        n_ = 0;
        N_ = 0;
        m_ = 0;
        return;
    }

    // Normalize compact format to full bracketed format
    // This allows both "ACGT{A,ACA}CGT" and "{ACGT}{A,ACA}{CGT}" to work
    input = normalize_eds_format(input);

    // Parse EDS format: {str1,str2,...}{str3}{str4,str5}...
    size_t pos = 0;
    n_ = 0;      // Number of sets
    N_ = 0;      // Total characters
    m_ = 0;      // Cardinality (total strings)

    sets_.clear();
    is_degenerate_.clear();
    set_sizes_.clear();
    cum_set_sizes_.clear();

    while (pos < input.length()) {
        // Expect '{'
        if (input[pos] != SET_OPEN) {
            throw std::runtime_error("Expected '{' at position " + std::to_string(pos));
        }
        pos++; // Skip '{'

        // Parse strings within this set
        StringSet current_set;
        std::string current_string;

        while (pos < input.length() && input[pos] != SET_CLOSE) {
            if (input[pos] == SET_SEPARATOR) {
                // End of current string, add to set
                current_set.push_back(current_string);
                N_ += current_string.length();
                current_string.clear();
                pos++;
            } else {
                // Regular character, add to current string
                current_string += input[pos];
                pos++;
            }
        }

        // Add last string in set (could be empty)
        current_set.push_back(current_string);
        N_ += current_string.length();

        // Expect '}'
        if (pos >= input.length() || input[pos] != SET_CLOSE) {
            throw std::runtime_error("Expected '}' at position " + std::to_string(pos));
        }
        pos++; // Skip '}'

        // Validate set is not empty
        if (current_set.empty()) {
            throw std::runtime_error("Empty set at position " + std::to_string(pos));
        }

        // Store set information
        sets_.push_back(current_set);
        set_sizes_.push_back(current_set.size());
        cum_set_sizes_.push_back(m_);  // Cumulative count before adding this set
        is_degenerate_.push_back(current_set.size() > 1);

        m_ += current_set.size();
        n_++;
    }

    // Validate we parsed something
    if (n_ == 0) {
        is_empty_ = true;
    } else {
        is_empty_ = false;
    }
}

// Constructor with EDS + sEDS streams
EDS::EDS(std::istream& eds_stream, std::istream& seds_stream) : is_empty_(false), has_sources_(false) {
    parse(eds_stream);
    parse_sources(seds_stream);
}

// Mixed input constructor: string EDS + stream sEDS
EDS::EDS(const std::string& eds_string, std::istream& seds_stream) : is_empty_(false), has_sources_(false) {
    std::stringstream eds_ss(eds_string);
    parse(eds_ss);
    parse_sources(seds_stream);
}

// Mixed input constructor: string EDS + file sEDS
EDS::EDS(const std::string& eds_string, const std::filesystem::path& seds_path) : is_empty_(false), has_sources_(false) {
    std::stringstream eds_ss(eds_string);
    parse(eds_ss);

    std::ifstream seds_ifs(seds_path);
    if (!seds_ifs) {
        throw std::runtime_error("Failed to open sEDS file: " + seds_path.string());
    }
    parse_sources(seds_ifs);
}

// Mixed input constructor: file EDS + string sEDS
EDS::EDS(const std::filesystem::path& eds_path, const std::string& seds_string) : is_empty_(false), has_sources_(false) {
    std::ifstream eds_ifs(eds_path);
    if (!eds_ifs) {
        throw std::runtime_error("Failed to open EDS file: " + eds_path.string());
    }
    parse(eds_ifs);

    std::stringstream seds_ss(seds_string);
    parse_sources(seds_ss);
}

// Mixed input constructor: stream EDS + string sEDS
EDS::EDS(std::istream& eds_stream, const std::string& seds_string) : is_empty_(false), has_sources_(false) {
    parse(eds_stream);
    std::stringstream seds_ss(seds_string);
    parse_sources(seds_ss);
}

// Load EDS from file
EDS EDS::load(const std::filesystem::path& path) {
    std::ifstream ifs(path);
    if (!ifs) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    return EDS(ifs);
}

// Load EDS from file with sources from file
EDS EDS::load(const std::filesystem::path& eds_path, const std::filesystem::path& seds_path) {
    std::ifstream eds_ifs(eds_path);
    if (!eds_ifs) {
        throw std::runtime_error("Failed to open EDS file: " + eds_path.string());
    }

    std::ifstream seds_ifs(seds_path);
    if (!seds_ifs) {
        throw std::runtime_error("Failed to open sEDS file: " + seds_path.string());
    }

    return EDS(eds_ifs, seds_ifs);
}

// Load sources from sEDS stream
void EDS::load_sources(std::istream& is) {
    parse_sources(is);
}

// Load sources from sEDS file
void EDS::load_sources(const std::filesystem::path& path) {
    std::ifstream ifs(path);
    if (!ifs) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    parse_sources(ifs);
}

// Load sources from sEDS string
void EDS::load_sources(const std::string& seds_string) {
    std::stringstream ss(seds_string);
    parse_sources(ss);
}

// Parse sEDS format: {{path_ids},{path_ids},...}
void EDS::parse_sources(std::istream& is) {
    // Read entire input into string
    std::stringstream buffer;
    buffer << is.rdbuf();
    std::string input = buffer.str();

    // Remove whitespace
    input.erase(std::remove_if(input.begin(), input.end(), ::isspace), input.end());

    if (input.empty()) {
        throw std::runtime_error("sEDS input is empty");
    }

    // Parse sEDS format: {{0},{1,2},{3}}{{0},{1}}...
    // One source set per string (by cardinality)
    size_t pos = 0;
    sources_.clear();
    size_t string_count = 0;

    while (pos < input.length()) {
        // Expect '{'
        if (input[pos] != SET_OPEN) {
            throw std::runtime_error("sEDS: Expected '{' at position " + std::to_string(pos));
        }
        pos++; // Skip '{'

        // Parse path IDs for this string
        std::set<int> path_set;
        std::string current_number;

        while (pos < input.length() && input[pos] != SET_CLOSE) {
            if (input[pos] == SET_SEPARATOR) {
                // End of current path ID
                if (!current_number.empty()) {
                    int path_id = std::stoi(current_number);
                    if (path_id < 0) {
                        throw std::runtime_error("sEDS: Invalid path ID (must be >= 0): " + current_number);
                    }
                    path_set.insert(path_id);
                    current_number.clear();
                }
                pos++;
            } else if (std::isdigit(input[pos])) {
                // Digit, add to current number
                current_number += input[pos];
                pos++;
            } else {
                throw std::runtime_error("sEDS: Invalid character '" + std::string(1, input[pos]) +
                                       "' at position " + std::to_string(pos));
            }
        }

        // Add last path ID if present
        if (!current_number.empty()) {
            int path_id = std::stoi(current_number);
            if (path_id < 0) {
                throw std::runtime_error("sEDS: Invalid path ID (must be >= 0): " + current_number);
            }
            path_set.insert(path_id);
        }

        // Expect '}'
        if (pos >= input.length() || input[pos] != SET_CLOSE) {
            throw std::runtime_error("sEDS: Expected '}' at position " + std::to_string(pos));
        }
        pos++; // Skip '}'

        // Validate path set is not empty (unless it's an error case we want to catch)
        if (path_set.empty()) {
            throw std::runtime_error("sEDS: Empty path set at string " + std::to_string(string_count));
        }

        // Store source set
        sources_.push_back(path_set);
        string_count++;
    }

    // Validate source count matches cardinality
    if (sources_.size() != m_) {
        throw std::runtime_error("sEDS: Source count (" + std::to_string(sources_.size()) +
                               ") does not match EDS cardinality (" + std::to_string(m_) + ")");
    }

    has_sources_ = true;
}

void EDS::calculate_statistics() {
    if (is_empty_) {
        stats_.min_context_length = 0;
        stats_.max_context_length = 0;
        stats_.avg_context_length = 0.0;
        stats_.num_degenerate_symbols = 0;
        stats_.num_common_chars = 0;
        stats_.total_change_size = 0;
        stats_.num_empty_strings = 0;
        return;
    }

    // Initialize statistics
    stats_.min_context_length = UINT32_MAX;
    stats_.max_context_length = 0;
    stats_.num_degenerate_symbols = 0;
    stats_.num_common_chars = 0;
    stats_.total_change_size = 0;
    stats_.num_empty_strings = 0;

    size_t total_length = 0;
    size_t num_strings_counted = 0;

    // Iterate through each set
    for (size_t i = 0; i < sets_.size(); i++) {
        const auto& set = sets_[i];

        // Count degenerate symbols (sets with more than one string)
        if (is_degenerate_[i]) {
            stats_.num_degenerate_symbols++;
            // Total change size is the number of alternatives beyond the first
            stats_.total_change_size += (set.size() - 1);
        }

        // Process each string in the set
        for (const auto& str : set) {
            Length len = str.length();

            // Update min/max/avg context length
            if (len < stats_.min_context_length) {
                stats_.min_context_length = len;
            }
            if (len > stats_.max_context_length) {
                stats_.max_context_length = len;
            }
            total_length += len;
            num_strings_counted++;

            // Count empty strings
            if (len == 0) {
                stats_.num_empty_strings++;
            }
        }

        // Calculate common characters in degenerate sets
        if (is_degenerate_[i] && set.size() > 1) {
            // Find the length of the shortest string in this set
            size_t min_len = SIZE_MAX;
            for (const auto& str : set) {
                if (str.length() < min_len) {
                    min_len = str.length();
                }
            }

            // Count common prefix characters
            for (size_t pos = 0; pos < min_len; pos++) {
                char common_char = set[0][pos];
                bool is_common = true;

                for (size_t j = 1; j < set.size(); j++) {
                    if (set[j][pos] != common_char) {
                        is_common = false;
                        break;
                    }
                }

                if (is_common) {
                    stats_.num_common_chars++;
                } else {
                    // Once we find a non-common character, stop checking
                    // (only counting common prefix)
                    break;
                }
            }
        }
    }

    // Calculate average context length
    if (num_strings_counted > 0) {
        stats_.avg_context_length = static_cast<double>(total_length) / num_strings_counted;
    } else {
        stats_.avg_context_length = 0.0;
    }

    // Handle edge case where all strings are empty
    if (stats_.min_context_length == UINT32_MAX) {
        stats_.min_context_length = 0;
    }
}

EDS::Statistics EDS::get_statistics() const {
    // Note: calculate_statistics() modifies stats_, but get_statistics() is const
    // We need to make this work by calling calculate_statistics() when needed
    // For now, we'll calculate statistics on demand using const_cast
    // A better approach would be to use mutable or calculate in constructor
    const_cast<EDS*>(this)->calculate_statistics();
    return stats_;
}

void EDS::print_statistics(std::ostream& os) const {
    Statistics stats = get_statistics();

    os << "========================================\n";
    os << "EDS Statistics\n";
    os << "========================================\n";
    os << "Structure:\n";
    os << "  Number of sets (n):           " << n_ << "\n";
    os << "  Total characters (N):         " << N_ << "\n";
    os << "  Total strings (m):            " << m_ << "\n";
    os << "  Degenerate symbols:           " << stats.num_degenerate_symbols << "\n";
    os << "  Regular symbols:              " << (n_ - stats.num_degenerate_symbols) << "\n";
    os << "\n";
    os << "Context Lengths:\n";
    os << "  Minimum:                      " << stats.min_context_length << "\n";
    os << "  Maximum:                      " << stats.max_context_length << "\n";
    os << "  Average:                      " << stats.avg_context_length << "\n";
    os << "\n";
    os << "Variations:\n";
    os << "  Total change size:            " << stats.total_change_size << "\n";
    os << "  Common characters:            " << stats.num_common_chars << "\n";
    os << "  Empty strings:                " << stats.num_empty_strings << "\n";
    os << "\n";
    if (has_sources_) {
        os << "Sources: Loaded (" << sources_.size() << " strings with source info)\n";
    } else {
        os << "Sources: Not loaded\n";
    }
    os << "========================================\n";
}

void EDS::print(std::ostream& os) const {
    if (is_empty_) {
        os << "(empty EDS)\n";
        return;
    }

    os << "EDS with " << n_ << " sets, " << m_ << " total strings:\n";

    for (size_t i = 0; i < sets_.size(); i++) {
        const auto& set = sets_[i];

        os << "Set " << i << ": {";

        for (size_t j = 0; j < set.size(); j++) {
            if (j > 0) os << ", ";

            const auto& str = set[j];
            if (str.empty()) {
                os << "ε";  // Epsilon for empty string
            } else {
                os << "\"" << str << "\"";
            }
        }

        os << "}";

        if (is_degenerate_[i]) {
            os << " [degenerate]";
        }

        os << "\n";
    }
}

void EDS::save(std::ostream& os, OutputFormat format) const {
    // Output EDS format
    for (size_t i = 0; i < sets_.size(); i++) {
        const auto& set = sets_[i];

        // Determine if we should use brackets for this set
        bool use_brackets = (format == OutputFormat::FULL) || is_degenerate_[i];

        if (use_brackets) {
            os << "{";
        }

        bool first = true;
        for (const auto& str : set) {
            if (!first) os << ",";
            os << str;
            first = false;
        }

        if (use_brackets) {
            os << "}";
        }
    }
    os << "\n";
}

void EDS::save(const std::filesystem::path& path, OutputFormat format) const {
    std::ofstream ofs(path);
    if (!ofs) {
        throw std::runtime_error("Failed to open file for writing: " + path.string());
    }
    save(ofs, format);
}

void EDS::save_sources(std::ostream& os) const {
    if (!has_sources_) {
        throw std::runtime_error("Cannot save sources: no sources loaded");
    }

    // Output sEDS format: {{path_ids},{path_ids},...}
    for (size_t i = 0; i < sources_.size(); i++) {
        os << "{";
        bool first = true;
        for (int path_id : sources_[i]) {
            if (!first) os << ",";
            os << path_id;
            first = false;
        }
        os << "}";
    }
    os << "\n";
}

void EDS::save_sources(const std::filesystem::path& path) const {
    std::ofstream ofs(path);
    if (!ofs) {
        throw std::runtime_error("Failed to open file for writing: " + path.string());
    }
    save_sources(ofs);
}

void EDS::generate_patterns(std::ostream& os, size_t count, Length pattern_length) const {
    // TODO: Implement
}

String EDS::extract(Position pos, Length len, const std::vector<int>& changes) const {
    // TODO: Implement
    return "";
}

double EDS::calculate_size_in_bytes() const {
    // TODO: Implement
    return 0.0;
}

std::string EDS::normalize_eds_format(const std::string& input) const {
    /*
     * Normalize compact EDS format to full bracketed format
     * Examples:
     *   "ACGT{A,ACA}CGT" -> "{ACGT}{A,ACA}{CGT}"
     *   "{ACGT}{A,ACA}{CGT}" -> "{ACGT}{A,ACA}{CGT}" (no change)
     *   "A{C,G}T" -> "{A}{C,G}{T}"
     */

    std::string result;
    std::string current_string;
    size_t i = 0;
    int brace_depth = 0;

    while (i < input.length()) {
        char ch = input[i];

        if (ch == SET_OPEN) {
            // If we have accumulated non-bracketed characters, wrap them
            if (!current_string.empty() && brace_depth == 0) {
                result += "{" + current_string + "}";
                current_string.clear();
            }
            result += ch;
            brace_depth++;
            i++;
        }
        else if (ch == SET_CLOSE) {
            result += ch;
            brace_depth--;
            i++;
        }
        else if (brace_depth > 0) {
            // Inside brackets, pass through as-is
            result += ch;
            i++;
        }
        else {
            // Outside brackets, accumulate characters
            current_string += ch;
            i++;
        }
    }

    // If there are remaining non-bracketed characters at the end, wrap them
    if (!current_string.empty() && brace_depth == 0) {
        result += "{" + current_string + "}";
    }

    return result;
}

} // namespace biofmi
