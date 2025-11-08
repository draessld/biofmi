#include "eds.hpp"
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <random>

namespace biofmi {

// Stream-based constructor (always FULL mode for streams)
EDS::EDS(std::istream& is) : is_empty_(false), mode_(StoringMode::FULL), has_sources_(false) {
    parse(is);
}

// String-based constructor (always FULL mode for strings)
EDS::EDS(const std::string& eds_string) : is_empty_(false), mode_(StoringMode::FULL), has_sources_(false) {
    std::stringstream ss(eds_string);
    parse(ss);
}

// String-based constructor with sources (always FULL mode)
EDS::EDS(const std::string& eds_string, const std::string& seds_string) : is_empty_(false), mode_(StoringMode::FULL), has_sources_(false) {
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

    // Clear all data structures
    sets_.clear();
    set_sizes_.clear();
    metadata_.base_positions.clear();
    metadata_.symbol_sizes.clear();
    metadata_.string_lengths.clear();
    metadata_.cum_set_sizes.clear();
    metadata_.is_degenerate.clear();

    while (pos < input.length()) {
        // Record starting position of this symbol
        std::streampos symbol_start = static_cast<std::streampos>(pos);
        metadata_.base_positions.push_back(symbol_start);

        // Expect '{'
        if (input[pos] != SET_OPEN) {
            throw std::runtime_error("Expected '{' at position " + std::to_string(pos));
        }
        pos++; // Skip '{'

        // Parse strings within this set
        StringSet current_set;
        std::string current_string;
        size_t symbol_size = 0;

        while (pos < input.length() && input[pos] != SET_CLOSE) {
            if (input[pos] == SET_SEPARATOR) {
                // End of current string
                Length str_len = current_string.length();
                metadata_.string_lengths.push_back(str_len);
                N_ += str_len;
                symbol_size++;

                // Only store string if FULL mode
                if (mode_ == StoringMode::FULL) {
                    current_set.push_back(current_string);
                }

                current_string.clear();
                pos++;
            } else {
                // Regular character, add to current string
                current_string += input[pos];
                pos++;
            }
        }

        // Add last string in set (could be empty)
        Length str_len = current_string.length();
        metadata_.string_lengths.push_back(str_len);
        N_ += str_len;
        symbol_size++;

        if (mode_ == StoringMode::FULL) {
            current_set.push_back(current_string);
        }

        // Expect '}'
        if (pos >= input.length() || input[pos] != SET_CLOSE) {
            throw std::runtime_error("Expected '}' at position " + std::to_string(pos));
        }
        pos++; // Skip '}'

        // Validate set is not empty
        if (symbol_size == 0) {
            throw std::runtime_error("Empty set at position " + std::to_string(pos));
        }

        // Store metadata
        metadata_.symbol_sizes.push_back(symbol_size);
        metadata_.cum_set_sizes.push_back(m_);  // Cumulative count before adding this set
        metadata_.is_degenerate.push_back(symbol_size > 1);

        // Store full data if FULL mode
        if (mode_ == StoringMode::FULL) {
            sets_.push_back(current_set);
            set_sizes_.push_back(current_set.size());
        }

        m_ += symbol_size;
        n_++;
    }

    // Validate we parsed something
    if (n_ == 0) {
        is_empty_ = true;
    } else {
        is_empty_ = false;
        // Calculate statistics from metadata
        calculate_statistics();
    }
}

// Constructor with EDS + sEDS streams (always FULL mode)
EDS::EDS(std::istream& eds_stream, std::istream& seds_stream) : is_empty_(false), mode_(StoringMode::FULL), has_sources_(false) {
    parse(eds_stream);
    parse_sources(seds_stream);
}

// Mixed input constructor: string EDS + stream sEDS (always FULL mode)
EDS::EDS(const std::string& eds_string, std::istream& seds_stream) : is_empty_(false), mode_(StoringMode::FULL), has_sources_(false) {
    std::stringstream eds_ss(eds_string);
    parse(eds_ss);
    parse_sources(seds_stream);
}

// Mixed input constructor: string EDS + file sEDS (always FULL mode)
EDS::EDS(const std::string& eds_string, const std::filesystem::path& seds_path) : is_empty_(false), mode_(StoringMode::FULL), has_sources_(false) {
    std::stringstream eds_ss(eds_string);
    parse(eds_ss);

    std::ifstream seds_ifs(seds_path);
    if (!seds_ifs) {
        throw std::runtime_error("Failed to open sEDS file: " + seds_path.string());
    }
    parse_sources(seds_ifs);
}

// Mixed input constructor: file EDS + string sEDS (uses FULL mode, file not kept open)
EDS::EDS(const std::filesystem::path& eds_path, const std::string& seds_string) : is_empty_(false), mode_(StoringMode::FULL), has_sources_(false) {
    std::ifstream eds_ifs(eds_path);
    if (!eds_ifs) {
        throw std::runtime_error("Failed to open EDS file: " + eds_path.string());
    }
    parse(eds_ifs);

    std::stringstream seds_ss(seds_string);
    parse_sources(seds_ss);
}

// Mixed input constructor: stream EDS + string sEDS (always FULL mode)
EDS::EDS(std::istream& eds_stream, const std::string& seds_string) : is_empty_(false), mode_(StoringMode::FULL), has_sources_(false) {
    parse(eds_stream);
    std::stringstream seds_ss(seds_string);
    parse_sources(seds_ss);
}

// Load EDS from file (with optional StoringMode)
EDS EDS::load(const std::filesystem::path& path, StoringMode mode) {
    EDS eds;
    eds.mode_ = mode;
    eds.is_empty_ = false;
    eds.has_sources_ = false;

    // For METADATA_ONLY, save path for later streaming
    if (mode == StoringMode::METADATA_ONLY) {
        eds.file_path_ = path;
    }

    std::ifstream ifs(path);
    if (!ifs) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    eds.parse(ifs);

    // For METADATA_ONLY, reopen file and keep stream open
    if (mode == StoringMode::METADATA_ONLY) {
        eds.stream_.open(path);
        if (!eds.stream_) {
            throw std::runtime_error("Failed to reopen file for streaming: " + path.string());
        }
    }

    return eds;
}

// Load EDS from file with sources from file (with optional StoringMode)
EDS EDS::load(const std::filesystem::path& eds_path, const std::filesystem::path& seds_path, StoringMode mode) {
    EDS eds;
    eds.mode_ = mode;
    eds.is_empty_ = false;
    eds.has_sources_ = false;

    // For METADATA_ONLY, save path for later streaming
    if (mode == StoringMode::METADATA_ONLY) {
        eds.file_path_ = eds_path;
    }

    std::ifstream eds_ifs(eds_path);
    if (!eds_ifs) {
        throw std::runtime_error("Failed to open EDS file: " + eds_path.string());
    }
    eds.parse(eds_ifs);

    std::ifstream seds_ifs(seds_path);
    if (!seds_ifs) {
        throw std::runtime_error("Failed to open sEDS file: " + seds_path.string());
    }
    eds.parse_sources(seds_ifs);

    // For METADATA_ONLY, reopen file and keep stream open
    if (mode == StoringMode::METADATA_ONLY) {
        eds.stream_.open(eds_path);
        if (!eds.stream_) {
            throw std::runtime_error("Failed to reopen file for streaming: " + eds_path.string());
        }
    }

    return eds;
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

// Parse sEDS format (flattened): {path_ids}{path_ids}...
// Format: one set of path IDs per string (indexed by string ID 0..m-1)
// Example: For EDS {ACGT}{A,ACA}{CGT}{T,TG} with 6 strings total:
//          sEDS is {0}{1,3}{2}{0}{1}{2,3}
//          where str0→{0}, str1→{1,3}, str2→{2}, str3→{0}, str4→{1}, str5→{2,3}
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

    // Parse flattened sEDS format: {path_ids}{path_ids}...
    // One source set per string, ordered by string ID (total = cardinality m)
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

    // Calculate source statistics
    calculate_source_statistics();
}

void EDS::calculate_statistics() {
    if (is_empty_) {
        metadata_.min_context_length = 0;
        metadata_.max_context_length = 0;
        metadata_.avg_context_length = 0.0;
        metadata_.num_degenerate_symbols = 0;
        metadata_.num_common_chars = 0;
        metadata_.total_change_size = 0;
        metadata_.num_empty_strings = 0;
        metadata_.num_paths = 0;
        metadata_.max_paths_per_string = 0;
        metadata_.avg_paths_per_string = 0.0;
        metadata_.cum_common_positions.clear();
        metadata_.cum_degenerate_counts.clear();
        return;
    }

    // Initialize statistics in metadata
    metadata_.min_context_length = UINT32_MAX;
    metadata_.max_context_length = 0;
    metadata_.num_degenerate_symbols = 0;
    metadata_.num_common_chars = 0;
    metadata_.total_change_size = 0;
    metadata_.num_empty_strings = 0;

    size_t total_context_length = 0;
    size_t num_context_blocks = 0;
    size_t string_idx = 0;

    // Iterate through each symbol using metadata only
    for (size_t i = 0; i < n_; i++) {
        size_t symbol_size = metadata_.symbol_sizes[i];
        bool is_degenerate = metadata_.is_degenerate[i];

        // Count degenerate symbols
        if (is_degenerate) {
            metadata_.num_degenerate_symbols++;
            metadata_.total_change_size += (symbol_size - 1);
        } else {
            // Non-degenerate symbols are "context blocks"
            // These are the common parts between degenerate positions
            Length context_len = metadata_.string_lengths[string_idx];

            if (context_len < metadata_.min_context_length) {
                metadata_.min_context_length = context_len;
            }
            if (context_len > metadata_.max_context_length) {
                metadata_.max_context_length = context_len;
            }
            total_context_length += context_len;
            num_context_blocks++;

            metadata_.num_common_chars += context_len;
        }

        // Count empty strings and process all strings in this symbol
        for (size_t j = 0; j < symbol_size; j++) {
            Length str_len = metadata_.string_lengths[string_idx];
            if (str_len == 0) {
                metadata_.num_empty_strings++;
            }
            string_idx++;
        }
    }

    // Calculate average context length
    if (num_context_blocks > 0) {
        metadata_.avg_context_length = static_cast<double>(total_context_length) / num_context_blocks;
    } else {
        metadata_.avg_context_length = 0.0;
    }

    // Handle edge case where all symbols are degenerate (no context blocks)
    if (metadata_.min_context_length == UINT32_MAX) {
        metadata_.min_context_length = 0;
    }

    // Calculate cumulative common positions (for position checking)
    metadata_.cum_common_positions.clear();
    metadata_.cum_common_positions.reserve(n_ + 1);

    Position cumulative_common = 0;
    metadata_.cum_common_positions.push_back(0);

    string_idx = 0;
    for (size_t i = 0; i < n_; i++) {
        if (!metadata_.is_degenerate[i]) {
            // Non-degenerate: add its length to cumulative
            cumulative_common += metadata_.string_lengths[string_idx];
        }
        metadata_.cum_common_positions.push_back(cumulative_common);

        // Move string_idx forward by number of strings in this symbol
        string_idx += metadata_.symbol_sizes[i];
    }

    // Calculate cumulative degenerate counts (for position checking)
    metadata_.cum_degenerate_counts.clear();
    metadata_.cum_degenerate_counts.reserve(n_ + 1);

    int cumulative_degenerate = 0;
    metadata_.cum_degenerate_counts.push_back(0);

    for (size_t i = 0; i < n_; i++) {
        if (metadata_.is_degenerate[i]) {
            cumulative_degenerate += metadata_.symbol_sizes[i];
        }
        metadata_.cum_degenerate_counts.push_back(cumulative_degenerate);
    }
}

void EDS::calculate_source_statistics() {
    // Initialize source statistics
    metadata_.num_paths = 0;
    metadata_.max_paths_per_string = 0;
    metadata_.avg_paths_per_string = 0.0;

    if (!has_sources_ || sources_.empty()) {
        return;
    }

    // Track all unique path IDs
    std::set<int> all_paths;
    size_t total_paths = 0;

    for (const auto& source_set : sources_) {
        // Track max paths in any single string
        if (source_set.size() > metadata_.max_paths_per_string) {
            metadata_.max_paths_per_string = source_set.size();
        }

        // Accumulate all unique paths
        for (int path_id : source_set) {
            all_paths.insert(path_id);
        }

        // Count total for average
        total_paths += source_set.size();
    }

    // Calculate statistics
    metadata_.num_paths = all_paths.size();
    metadata_.avg_paths_per_string = sources_.size() > 0
        ? static_cast<double>(total_paths) / sources_.size()
        : 0.0;
}

EDS::Statistics EDS::get_statistics() const {
    // Return Statistics struct from Metadata (for backward compatibility)
    Statistics stats;
    stats.min_context_length = metadata_.min_context_length;
    stats.max_context_length = metadata_.max_context_length;
    stats.avg_context_length = metadata_.avg_context_length;
    stats.num_degenerate_symbols = metadata_.num_degenerate_symbols;
    stats.num_common_chars = metadata_.num_common_chars;
    stats.total_change_size = metadata_.total_change_size;
    stats.num_empty_strings = metadata_.num_empty_strings;
    stats.num_paths = metadata_.num_paths;
    stats.max_paths_per_string = metadata_.max_paths_per_string;
    stats.avg_paths_per_string = metadata_.avg_paths_per_string;
    return stats;
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
    if (mode_ == StoringMode::METADATA_ONLY) {
        throw std::runtime_error(
            "Cannot print EDS in METADATA_ONLY mode. "
            "Load with StoringMode::FULL to access string data for printing."
        );
    }

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

        if (metadata_.is_degenerate[i]) {
            os << " [degenerate]";
        }

        os << "\n";
    }
}

void EDS::save(std::ostream& os, OutputFormat format) const {
    if (mode_ == StoringMode::METADATA_ONLY) {
        throw std::runtime_error(
            "Cannot save EDS in METADATA_ONLY mode. "
            "Load with StoringMode::FULL to access string data for saving."
        );
    }

    // Output EDS format
    for (size_t i = 0; i < sets_.size(); i++) {
        const auto& set = sets_[i];

        // Determine if we should use brackets for this set
        bool use_brackets = (format == OutputFormat::FULL) || metadata_.is_degenerate[i];

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

    // Output sEDS format (flattened): {path_ids}{path_ids}...
    // One set per string, ordered by string ID
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
    if (is_empty_ || n_ == 0) {
        throw std::runtime_error("Cannot generate patterns from empty EDS");
    }

    if (pattern_length == 0) {
        throw std::invalid_argument("Pattern length must be greater than 0");
    }

    // Use random number generator for reproducible results
    std::random_device rd;
    std::mt19937 gen(rd());

    // Random position distribution (0 to num_common_chars - 1)
    std::uniform_int_distribution<Position> pos_dist(
        0,
        metadata_.num_common_chars > 0 ? metadata_.num_common_chars - 1 : 0
    );

    for (size_t i = 0; i < count; ++i) {
        String pattern;
        Length remaining_length = pattern_length;

        // Pick random starting position in the EDS
        Position random_common_pos = metadata_.num_common_chars > 0 ? pos_dist(gen) : 0;
        Position offset_in_symbol = 0;
        size_t start_symbol = 0;

        if (metadata_.num_common_chars > 0) {
            start_symbol = find_symbol_at_common_position(random_common_pos, offset_in_symbol);
        }

        Position current_pos = start_symbol;
        bool first_symbol = true;

        // Generate pattern by randomly selecting from sets
        // Works in both FULL and METADATA_ONLY modes via read_symbol()
        while (remaining_length > 0 && current_pos < n_) {
            StringSet set = read_symbol(current_pos);

            if (set.empty()) {
                // Skip empty sets (epsilon)
                current_pos++;
                first_symbol = false;
                continue;
            }

            // Randomly select one string from the set
            std::uniform_int_distribution<size_t> set_dist(0, set.size() - 1);
            size_t string_idx = set_dist(gen);
            const String& selected = set[string_idx];

            // For first symbol, start from offset; for others, start from 0
            Length start_offset = first_symbol ? offset_in_symbol : 0;

            // Take what we need from this string (starting from offset)
            if (start_offset < selected.length()) {
                Length available = selected.length() - start_offset;
                Length to_take = std::min(remaining_length, available);
                pattern.append(selected.substr(start_offset, to_take));
                remaining_length -= to_take;
            }

            first_symbol = false;

            if (remaining_length > 0) {
                current_pos++;
            } else {
                break;
            }
        }

        // If we couldn't generate full pattern length, pad or regenerate
        if (pattern.length() < pattern_length) {
            // Try wrapping around for short EDS
            while (pattern.length() < pattern_length && n_ > 0) {
                Position wrap_pos = pattern.length() % n_;
                StringSet set = read_symbol(wrap_pos);

                if (!set.empty()) {
                    std::uniform_int_distribution<size_t> set_dist(0, set.size() - 1);
                    size_t string_idx = set_dist(gen);
                    const String& selected = set[string_idx];

                    Length to_take = std::min(
                        static_cast<Length>(pattern_length - pattern.length()),
                        static_cast<Length>(selected.length())
                    );
                    pattern.append(selected.substr(0, to_take));
                }
            }
        }

        // Output the pattern
        os << pattern << '\n';
    }
}

String EDS::extract(Position pos, Length len, const std::vector<int>& changes) const {
    // Only available in FULL mode
    if (mode_ == StoringMode::METADATA_ONLY) {
        throw std::runtime_error(
            "extract() is only available in FULL mode. "
            "Load EDS with StoringMode::FULL to use this function."
        );
    }

    if (is_empty_ || n_ == 0) {
        throw std::runtime_error("Cannot extract from empty EDS");
    }

    if (pos >= n_) {
        throw std::out_of_range("Start position exceeds EDS length");
    }

    if (len == 0) {
        return "";
    }

    // Validate changes vector size
    Position end_pos = std::min(pos + len, n_);
    size_t expected_changes = end_pos - pos;

    if (changes.size() != expected_changes) {
        throw std::invalid_argument(
            "changes vector size (" + std::to_string(changes.size()) +
            ") must match range length (" + std::to_string(expected_changes) + ")"
        );
    }

    // Extract substring by selecting alternatives according to changes vector
    String result;
    for (size_t i = 0; i < expected_changes; ++i) {
        Position current_pos = pos + i;
        int change_idx = changes[i];

        const StringSet& set = sets_[current_pos];

        // Validate change index
        if (change_idx < 0 || static_cast<size_t>(change_idx) >= set.size()) {
            throw std::out_of_range(
                "Change index " + std::to_string(change_idx) +
                " at position " + std::to_string(current_pos) +
                " is out of range (set size: " + std::to_string(set.size()) + ")"
            );
        }

        // Append the selected string
        result.append(set[change_idx]);
    }

    return result;
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

// Read symbol from stream (for METADATA_ONLY mode)
StringSet EDS::read_symbol_from_stream(Position pos) const {
    if (mode_ == StoringMode::FULL) {
        // In FULL mode, return directly from sets_
        return sets_[pos];
    }

    // METADATA_ONLY mode: stream from file
    if (!stream_.is_open()) {
        throw std::runtime_error("File stream not available for reading symbol");
    }

    // Seek to symbol position
    stream_.clear();  // Clear any error flags
    stream_.seekg(metadata_.base_positions[pos]);

    if (!stream_) {
        throw std::runtime_error("Failed to seek to position " + std::to_string(pos));
    }

    // Parse one symbol
    StringSet result;
    char ch;
    std::string current_str;

    // Expect '{'
    if (!stream_.get(ch) || ch != SET_OPEN) {
        throw std::runtime_error("Expected '{' at position " + std::to_string(pos));
    }

    // Parse strings in symbol
    while (stream_.get(ch) && ch != SET_CLOSE) {
        if (ch == SET_SEPARATOR) {
            result.push_back(current_str);
            current_str.clear();
        } else if (!std::isspace(ch)) {  // Skip whitespace
            current_str += ch;
        }
    }

    // Add last string
    result.push_back(current_str);

    return result;
}

// Public accessor for read_symbol (works in both modes)
StringSet EDS::read_symbol(Position pos) const {
    if (pos >= n_) {
        throw std::out_of_range("Position " + std::to_string(pos) + " out of range");
    }
    return read_symbol_from_stream(pos);
}

// get_sets() with error checking
const std::vector<StringSet>& EDS::get_sets() const {
    if (mode_ == StoringMode::METADATA_ONLY) {
        throw std::runtime_error(
            "Cannot access sets in METADATA_ONLY mode. "
            "Use read_symbol(pos) for on-demand access, or load with StoringMode::FULL"
        );
    }
    return sets_;
}

// Check if pattern occurs at position with given degenerate string choices
bool EDS::check_position(Position common_pos,
                        const std::vector<int>& degenerate_strings,
                        const String& pattern) const {
    // Handle empty EDS
    if (is_empty_ || n_ == 0) {
        return false;
    }

    // Handle empty pattern
    if (pattern.empty()) {
        return true;  // Empty pattern always matches
    }

    // Find starting symbol using binary search
    Position offset_in_symbol = 0;
    size_t start_symbol = 0;

    try {
        start_symbol = find_symbol_at_common_position(common_pos, offset_in_symbol);
    } catch (const std::out_of_range&) {
        // Position is beyond EDS range
        return false;
    }

    // Warn if too many degenerate strings provided
    // Count expected number of degenerate symbols we'll traverse
    size_t expected_deg_count = 0;
    Length chars_counted = 0;
    for (size_t i = start_symbol; i < n_ && chars_counted < pattern.length(); i++) {
        if (metadata_.is_degenerate[i]) {
            expected_deg_count++;
        }
        // Estimate how many chars this symbol contributes
        size_t global_string_idx = metadata_.cum_set_sizes[i];
        Length sym_len = metadata_.string_lengths[global_string_idx];
        if (i == start_symbol) {
            sym_len = (sym_len > offset_in_symbol) ? (sym_len - offset_in_symbol) : 0;
        }
        chars_counted += sym_len;
    }

    if (degenerate_strings.size() > expected_deg_count) {
        std::cerr << "Warning: More degenerate strings provided ("
                  << degenerate_strings.size()
                  << ") than needed (" << expected_deg_count
                  << "). Extra strings will be ignored.\n";
    }

    // Source validation: check if path intersection is non-empty
    if (has_sources_) {
        std::set<int> path_intersection;
        try {
            path_intersection = calculate_path_intersection(
                start_symbol, offset_in_symbol,
                degenerate_strings, pattern.length()
            );
        } catch (const std::exception&) {
            // If path intersection calculation fails, propagate error
            throw;
        }

        // Empty intersection means no valid biological path exists
        if (path_intersection.empty()) {
            return false;
        }
    }

    // Reconstruct string based on storage mode
    String reconstructed;

    try {
        if (mode_ == StoringMode::FULL) {
            reconstructed = reconstruct_from_memory(
                start_symbol, offset_in_symbol,
                degenerate_strings, pattern.length()
            );
        } else {
            reconstructed = reconstruct_from_file(
                start_symbol, offset_in_symbol,
                degenerate_strings, pattern.length()
            );
        }
    } catch (const std::exception&) {
        // If reconstruction fails (e.g., validation errors),
        // let the exception propagate
        throw;
    }

    // If we couldn't reconstruct enough characters, pattern doesn't match
    if (reconstructed.length() < pattern.length()) {
        return false;
    }

    // Compare reconstructed string with pattern
    return reconstructed == pattern;
}

// Position checking helper: decode absolute degenerate string number
std::pair<size_t, size_t> EDS::decode_degenerate_string_number(int abs_string_num) const {
    if (abs_string_num < 0) {
        throw std::invalid_argument(
            "Degenerate string number must be non-negative, got: " +
            std::to_string(abs_string_num)
        );
    }

    // Binary search to find which symbol this string belongs to
    auto it = std::upper_bound(
        metadata_.cum_degenerate_counts.begin(),
        metadata_.cum_degenerate_counts.end(),
        abs_string_num
    );

    if (it == metadata_.cum_degenerate_counts.begin()) {
        throw std::out_of_range(
            "Invalid degenerate string number: " + std::to_string(abs_string_num)
        );
    }

    size_t symbol_idx = std::distance(metadata_.cum_degenerate_counts.begin(), it) - 1;

    // Check if this symbol is actually degenerate
    if (!metadata_.is_degenerate[symbol_idx]) {
        throw std::runtime_error(
            "Internal error: degenerate string number " +
            std::to_string(abs_string_num) +
            " maps to non-degenerate symbol " + std::to_string(symbol_idx)
        );
    }

    size_t local_idx = abs_string_num - metadata_.cum_degenerate_counts[symbol_idx];

    // Validate local index is within range
    if (local_idx >= metadata_.symbol_sizes[symbol_idx]) {
        throw std::out_of_range(
            "Local index " + std::to_string(local_idx) +
            " out of range for symbol " + std::to_string(symbol_idx) +
            " (size: " + std::to_string(metadata_.symbol_sizes[symbol_idx]) + ")"
        );
    }

    return {symbol_idx, local_idx};
}

// Position checking helper: find symbol containing common position
size_t EDS::find_symbol_at_common_position(Position common_pos, Position& offset_out) const {
    // Binary search in cum_common_positions
    auto it = std::upper_bound(
        metadata_.cum_common_positions.begin(),
        metadata_.cum_common_positions.end(),
        common_pos
    );

    if (it == metadata_.cum_common_positions.begin()) {
        throw std::out_of_range(
            "Common position " + std::to_string(common_pos) + " is before EDS start"
        );
    }

    size_t symbol_idx = std::distance(metadata_.cum_common_positions.begin(), it) - 1;

    // This symbol must be non-degenerate (common)
    if (metadata_.is_degenerate[symbol_idx]) {
        throw std::out_of_range(
            "Common position " + std::to_string(common_pos) +
            " points to degenerate symbol " + std::to_string(symbol_idx)
        );
    }

    // Calculate offset within the symbol
    offset_out = common_pos - metadata_.cum_common_positions[symbol_idx];

    // Validate offset is within the symbol's length
    size_t global_string_idx = metadata_.cum_set_sizes[symbol_idx];
    Length symbol_length = metadata_.string_lengths[global_string_idx];

    if (offset_out >= symbol_length) {
        throw std::out_of_range(
            "Offset " + std::to_string(offset_out) +
            " exceeds symbol " + std::to_string(symbol_idx) +
            " length " + std::to_string(symbol_length)
        );
    }

    return symbol_idx;
}

// Position checking helper: reconstruct string from memory (FULL mode)
String EDS::reconstruct_from_memory(size_t start_symbol,
                                   Position offset_in_symbol,
                                   const std::vector<int>& degenerate_strings,
                                   Length pattern_length) const {
    String result;
    result.reserve(pattern_length);

    size_t deg_idx = 0;
    bool first_symbol = true;

    for (size_t symbol_idx = start_symbol;
         symbol_idx < n_ && result.length() < pattern_length;
         symbol_idx++) {

        String str;

        if (metadata_.is_degenerate[symbol_idx]) {
            // Degenerate symbol: use specified string
            if (deg_idx >= degenerate_strings.size()) {
                throw std::invalid_argument(
                    "Not enough degenerate strings provided (need at least " +
                    std::to_string(deg_idx + 1) + ", got " +
                    std::to_string(degenerate_strings.size()) + ")"
                );
            }

            int abs_string_num = degenerate_strings[deg_idx];
            auto [expected_symbol, local_idx] = decode_degenerate_string_number(abs_string_num);

            // Verify this degenerate string belongs to current symbol
            if (expected_symbol != symbol_idx) {
                throw std::invalid_argument(
                    "Degenerate string " + std::to_string(abs_string_num) +
                    " belongs to symbol " + std::to_string(expected_symbol) +
                    ", but expected for symbol " + std::to_string(symbol_idx)
                );
            }

            str = sets_[symbol_idx][local_idx];
            deg_idx++;

        } else {
            // Common symbol: use the only string
            str = sets_[symbol_idx][0];

            // Apply offset if this is the first symbol
            if (first_symbol && offset_in_symbol > 0) {
                if (offset_in_symbol >= str.length()) {
                    throw std::out_of_range(
                        "Offset " + std::to_string(offset_in_symbol) +
                        " exceeds symbol length " + std::to_string(str.length())
                    );
                }
                str = str.substr(offset_in_symbol);
                first_symbol = false;
            }
        }

        // Take only what we need
        Length chars_to_take = std::min(
            static_cast<Length>(str.length()),
            static_cast<Length>(pattern_length - result.length())
        );

        result += str.substr(0, chars_to_take);
    }

    return result;
}

// Position checking helper: reconstruct string from file (METADATA_ONLY mode)
String EDS::reconstruct_from_file(size_t start_symbol,
                                 Position offset_in_symbol,
                                 const std::vector<int>& degenerate_strings,
                                 Length pattern_length) const {
    String result;
    result.reserve(pattern_length);

    size_t deg_idx = 0;
    bool first_symbol = true;

    for (size_t symbol_idx = start_symbol;
         symbol_idx < n_ && result.length() < pattern_length;
         symbol_idx++) {

        // Read symbol from file using existing method
        StringSet symbol_strings = read_symbol(symbol_idx);

        String str;

        if (metadata_.is_degenerate[symbol_idx]) {
            // Degenerate symbol: use specified string
            if (deg_idx >= degenerate_strings.size()) {
                throw std::invalid_argument(
                    "Not enough degenerate strings provided (need at least " +
                    std::to_string(deg_idx + 1) + ", got " +
                    std::to_string(degenerate_strings.size()) + ")"
                );
            }

            int abs_string_num = degenerate_strings[deg_idx];
            auto [expected_symbol, local_idx] = decode_degenerate_string_number(abs_string_num);

            // Verify this degenerate string belongs to current symbol
            if (expected_symbol != symbol_idx) {
                throw std::invalid_argument(
                    "Degenerate string " + std::to_string(abs_string_num) +
                    " belongs to symbol " + std::to_string(expected_symbol) +
                    ", but expected for symbol " + std::to_string(symbol_idx)
                );
            }

            if (local_idx >= symbol_strings.size()) {
                throw std::runtime_error(
                    "Local index " + std::to_string(local_idx) +
                    " out of range for symbol (size: " +
                    std::to_string(symbol_strings.size()) + ")"
                );
            }

            str = symbol_strings[local_idx];
            deg_idx++;

        } else {
            // Common symbol: use the only string
            if (symbol_strings.empty()) {
                throw std::runtime_error(
                    "Common symbol " + std::to_string(symbol_idx) + " is empty"
                );
            }

            str = symbol_strings[0];

            // Apply offset if this is the first symbol
            if (first_symbol && offset_in_symbol > 0) {
                if (offset_in_symbol >= str.length()) {
                    throw std::out_of_range(
                        "Offset " + std::to_string(offset_in_symbol) +
                        " exceeds symbol length " + std::to_string(str.length())
                    );
                }
                str = str.substr(offset_in_symbol);
                first_symbol = false;
            }
        }

        // Take only what we need
        Length chars_to_take = std::min(
            static_cast<Length>(str.length()),
            static_cast<Length>(pattern_length - result.length())
        );

        result += str.substr(0, chars_to_take);
    }

    return result;
}

// Position checking helper: calculate path intersection for source validation
std::set<int> EDS::calculate_path_intersection(size_t start_symbol,
                                               Position offset_in_symbol,
                                               const std::vector<int>& degenerate_strings,
                                               Length pattern_length) const {
    // If no sources loaded, return universal set {0}
    if (!has_sources_) {
        return {0};
    }

    // Start with universal set (all paths)
    std::set<int> intersection;
    bool first = true;

    size_t deg_idx = 0;
    Length chars_counted = 0;

    for (size_t symbol_idx = start_symbol;
         symbol_idx < n_ && chars_counted < pattern_length;
         symbol_idx++) {

        // Determine which string is used from this symbol
        size_t global_string_idx;

        if (metadata_.is_degenerate[symbol_idx]) {
            // Degenerate symbol: use specified string
            if (deg_idx >= degenerate_strings.size()) {
                throw std::invalid_argument(
                    "Not enough degenerate strings for path intersection calculation"
                );
            }

            int abs_string_num = degenerate_strings[deg_idx];
            auto [expected_symbol, local_idx] = decode_degenerate_string_number(abs_string_num);

            if (expected_symbol != symbol_idx) {
                throw std::invalid_argument(
                    "Degenerate string mismatch in path intersection calculation"
                );
            }

            // Convert to global string ID
            global_string_idx = metadata_.cum_set_sizes[symbol_idx] + local_idx;
            deg_idx++;

        } else {
            // Common symbol: use the only string
            global_string_idx = metadata_.cum_set_sizes[symbol_idx];

            // Apply offset for first symbol
            if (symbol_idx == start_symbol && offset_in_symbol > 0) {
                Length sym_len = metadata_.string_lengths[global_string_idx];
                if (offset_in_symbol >= sym_len) {
                    // Offset exceeds symbol length - invalid
                    return {};
                }
                sym_len -= offset_in_symbol;
                chars_counted += std::min(sym_len, static_cast<Length>(pattern_length - chars_counted));
            } else {
                Length sym_len = metadata_.string_lengths[global_string_idx];
                chars_counted += std::min(sym_len, static_cast<Length>(pattern_length - chars_counted));
            }
        }

        // Get source set for this string
        if (global_string_idx >= sources_.size()) {
            throw std::runtime_error(
                "String ID " + std::to_string(global_string_idx) +
                " out of range for sources (size: " + std::to_string(sources_.size()) + ")"
            );
        }

        const std::set<int>& current_sources = sources_[global_string_idx];

        // Compute intersection
        if (first) {
            intersection = current_sources;
            first = false;
        } else {
            // Intersection with special handling for universal marker {0}
            std::set<int> new_intersection;

            bool current_has_universal = current_sources.count(0) > 0;
            bool accum_has_universal = intersection.count(0) > 0;

            if (current_has_universal && accum_has_universal) {
                // {0} ∩ {0} = {0}
                new_intersection.insert(0);
            } else if (current_has_universal) {
                // {0} ∩ {x,y,...} = {x,y,...}
                new_intersection = intersection;
            } else if (accum_has_universal) {
                // {x,y,...} ∩ {0} = {x,y,...}
                new_intersection = current_sources;
            } else {
                // Regular set intersection
                std::set_intersection(
                    intersection.begin(), intersection.end(),
                    current_sources.begin(), current_sources.end(),
                    std::inserter(new_intersection, new_intersection.begin())
                );
            }

            intersection = new_intersection;
        }

        // Early termination if intersection becomes empty
        if (intersection.empty()) {
            return {};
        }

        // Update chars_counted for degenerate symbols
        if (metadata_.is_degenerate[symbol_idx]) {
            Length sym_len = metadata_.string_lengths[global_string_idx];
            chars_counted += std::min(sym_len, static_cast<Length>(pattern_length - chars_counted));
        }
    }

    return intersection;
}

// Merge two adjacent symbols (degenerate or non-degenerate)
EDS EDS::merge_adjacent(size_t pos1, size_t pos2) const {
    // ===== VALIDATION =====

    // Validation: positions must be adjacent
    if (pos2 != pos1 + 1) {
        throw std::invalid_argument(
            "Positions must be adjacent: pos2 (" + std::to_string(pos2) +
            ") must equal pos1 + 1 (" + std::to_string(pos1 + 1) + ")"
        );
    }

    // Validation: both positions must be within bounds
    if (pos1 >= n_ || pos2 >= n_) {
        throw std::out_of_range(
            "Position out of range: pos1=" + std::to_string(pos1) +
            ", pos2=" + std::to_string(pos2) + ", n=" + std::to_string(n_)
        );
    }

    // ===== CALCULATE MERGED METADATA =====

    // Get source indices for the two symbols
    size_t global_string_idx1 = metadata_.cum_set_sizes[pos1];
    size_t global_string_idx2 = metadata_.cum_set_sizes[pos2];
    size_t set1_size = metadata_.symbol_sizes[pos1];
    size_t set2_size = metadata_.symbol_sizes[pos2];

    // Calculate merged symbol size and prepare merged data
    size_t merged_size;
    std::vector<std::set<int>> merged_sources;
    std::vector<Length> merged_string_lengths;

    if (!has_sources_) {
        // CARTESIAN merge: size is product
        merged_size = set1_size * set2_size;

        // Calculate string lengths for all combinations
        for (size_t i = 0; i < set1_size; ++i) {
            Length len1 = metadata_.string_lengths[global_string_idx1 + i];
            for (size_t j = 0; j < set2_size; ++j) {
                Length len2 = metadata_.string_lengths[global_string_idx2 + j];
                merged_string_lengths.push_back(len1 + len2);
            }
        }
    } else {
        // LINEAR merge: only count valid combinations (non-empty intersection)
        for (size_t i = 0; i < set1_size; ++i) {
            const std::set<int>& sources1 = sources_[global_string_idx1 + i];
            Length len1 = metadata_.string_lengths[global_string_idx1 + i];

            for (size_t j = 0; j < set2_size; ++j) {
                const std::set<int>& sources2 = sources_[global_string_idx2 + j];
                Length len2 = metadata_.string_lengths[global_string_idx2 + j];

                // Compute intersection with special handling for {0}
                std::set<int> intersection;
                bool sources1_has_universal = sources1.count(0) > 0;
                bool sources2_has_universal = sources2.count(0) > 0;

                if (sources1_has_universal && sources2_has_universal) {
                    // {0} ∩ {0} = {0}
                    intersection.insert(0);
                } else if (sources1_has_universal) {
                    // {0} ∩ {x,y,...} = {x,y,...}
                    intersection = sources2;
                } else if (sources2_has_universal) {
                    // {x,y,...} ∩ {0} = {x,y,...}
                    intersection = sources1;
                } else {
                    // Regular set intersection
                    std::set_intersection(
                        sources1.begin(), sources1.end(),
                        sources2.begin(), sources2.end(),
                        std::inserter(intersection, intersection.begin())
                    );
                }

                // Only keep if intersection is non-empty
                if (!intersection.empty()) {
                    merged_sources.push_back(intersection);
                    merged_string_lengths.push_back(len1 + len2);
                }
            }
        }

        merged_size = merged_sources.size();

        // Validation: merged set must not be empty
        if (merged_size == 0) {
            throw std::runtime_error(
                "Merging positions " + std::to_string(pos1) + " and " +
                std::to_string(pos2) + " results in empty set "
                "(no valid source intersections)"
            );
        }
    }

    // ===== BUILD NEW EDS =====

    EDS result;
    result.is_empty_ = false;
    result.mode_ = mode_;
    result.has_sources_ = has_sources_;
    result.file_path_ = file_path_;
    result.n_ = n_ - 1;  // One less position after merge

    // ===== BUILD NEW METADATA =====

    result.metadata_.base_positions.clear();
    result.metadata_.symbol_sizes.clear();
    result.metadata_.string_lengths.clear();
    result.metadata_.cum_set_sizes.clear();
    result.metadata_.is_degenerate.clear();

    size_t current_string_idx = 0;

    // Copy metadata for positions before pos1
    for (size_t i = 0; i < pos1; ++i) {
        result.metadata_.base_positions.push_back(metadata_.base_positions[i]);
        result.metadata_.symbol_sizes.push_back(metadata_.symbol_sizes[i]);
        result.metadata_.is_degenerate.push_back(metadata_.is_degenerate[i]);
        result.metadata_.cum_set_sizes.push_back(current_string_idx);

        // Copy string lengths for this symbol
        for (size_t j = 0; j < metadata_.symbol_sizes[i]; ++j) {
            result.metadata_.string_lengths.push_back(
                metadata_.string_lengths[metadata_.cum_set_sizes[i] + j]
            );
        }

        current_string_idx += metadata_.symbol_sizes[i];
    }

    // Add merged position metadata
    result.metadata_.base_positions.push_back(metadata_.base_positions[pos1]);
    result.metadata_.symbol_sizes.push_back(merged_size);
    result.metadata_.is_degenerate.push_back(merged_size > 1);  // Degenerate if > 1 alternative
    result.metadata_.cum_set_sizes.push_back(current_string_idx);

    // Add merged string lengths
    for (Length len : merged_string_lengths) {
        result.metadata_.string_lengths.push_back(len);
    }
    current_string_idx += merged_size;

    // Copy metadata for positions after pos2
    for (size_t i = pos2 + 1; i < n_; ++i) {
        result.metadata_.base_positions.push_back(metadata_.base_positions[i]);
        result.metadata_.symbol_sizes.push_back(metadata_.symbol_sizes[i]);
        result.metadata_.is_degenerate.push_back(metadata_.is_degenerate[i]);
        result.metadata_.cum_set_sizes.push_back(current_string_idx);

        // Copy string lengths for this symbol
        for (size_t j = 0; j < metadata_.symbol_sizes[i]; ++j) {
            result.metadata_.string_lengths.push_back(
                metadata_.string_lengths[metadata_.cum_set_sizes[i] + j]
            );
        }

        current_string_idx += metadata_.symbol_sizes[i];
    }

    // Calculate result cardinality and size
    result.m_ = current_string_idx;
    result.N_ = 0;
    for (Length len : result.metadata_.string_lengths) {
        result.N_ += len;
    }

    // ===== BUILD SOURCES (if needed) =====

    if (has_sources_) {
        result.sources_.clear();

        // Copy sources before pos1
        size_t source_idx = 0;
        for (size_t i = 0; i < pos1; ++i) {
            for (size_t j = 0; j < metadata_.symbol_sizes[i]; ++j) {
                result.sources_.push_back(sources_[source_idx++]);
            }
        }

        // Add merged sources
        for (const auto& src : merged_sources) {
            result.sources_.push_back(src);
        }

        // Skip sources for pos1 and pos2
        source_idx = metadata_.cum_set_sizes[pos2] + metadata_.symbol_sizes[pos2];

        // Copy sources after pos2
        for (size_t i = pos2 + 1; i < n_; ++i) {
            for (size_t j = 0; j < metadata_.symbol_sizes[i]; ++j) {
                result.sources_.push_back(sources_[source_idx++]);
            }
        }
    }

    // ===== BUILD SETS (FULL mode only) =====

    if (mode_ == StoringMode::FULL) {
        result.sets_.clear();
        result.set_sizes_.clear();

        // Copy sets before pos1
        for (size_t i = 0; i < pos1; ++i) {
            result.sets_.push_back(sets_[i]);
            result.set_sizes_.push_back(set_sizes_[i]);
        }

        // Build merged set
        StringSet merged_set;
        const StringSet& set1 = sets_[pos1];
        const StringSet& set2 = sets_[pos2];

        if (!has_sources_) {
            // CARTESIAN: all combinations
            for (const auto& str1 : set1) {
                for (const auto& str2 : set2) {
                    merged_set.push_back(str1 + str2);
                }
            }
        } else {
            // LINEAR: only valid combinations (same logic as metadata calculation)
            for (size_t i = 0; i < set1.size(); ++i) {
                for (size_t j = 0; j < set2.size(); ++j) {
                    const std::set<int>& sources1 = sources_[global_string_idx1 + i];
                    const std::set<int>& sources2 = sources_[global_string_idx2 + j];

                    // Compute intersection
                    std::set<int> intersection;
                    bool sources1_has_universal = sources1.count(0) > 0;
                    bool sources2_has_universal = sources2.count(0) > 0;

                    if (sources1_has_universal && sources2_has_universal) {
                        intersection.insert(0);
                    } else if (sources1_has_universal) {
                        intersection = sources2;
                    } else if (sources2_has_universal) {
                        intersection = sources1;
                    } else {
                        std::set_intersection(
                            sources1.begin(), sources1.end(),
                            sources2.begin(), sources2.end(),
                            std::inserter(intersection, intersection.begin())
                        );
                    }

                    if (!intersection.empty()) {
                        merged_set.push_back(set1[i] + set2[j]);
                    }
                }
            }
        }

        result.sets_.push_back(merged_set);
        result.set_sizes_.push_back(merged_set.size());

        // Copy sets after pos2
        for (size_t i = pos2 + 1; i < n_; ++i) {
            result.sets_.push_back(sets_[i]);
            result.set_sizes_.push_back(set_sizes_[i]);
        }
    }

    // ===== FINALIZE =====

    // Recalculate statistics
    result.calculate_statistics();
    if (has_sources_) {
        result.calculate_source_statistics();
    }

    return result;
}

} // namespace biofmi
