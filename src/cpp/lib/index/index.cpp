#include "index.hpp"

// SDSL headers included in implementation only
#include <sdsl/suffix_arrays.hpp>

#include <iomanip>

namespace biofmi {

// PIMPL implementation for IndexData
struct BioFMI::IndexData {
    BioFMI::IndexType reference_index;
    BioFMI::IndexType changes_index;

    sdsl::bit_vector loc;
    sdsl::bit_vector iloc;
    sdsl::bit_vector tloc;

    sdsl::rank_support_v<> rloc;
    sdsl::rank_support_v<> riloc;
    sdsl::rank_support_v<> rtloc;
    sdsl::select_support_mcl<> sloc;

    std::vector<int> base_positions;
    std::vector<int> set_sizes;
    std::vector<int> offsets;
};

BioFMI::BioFMI(EDS&& eds, Length context_length)
    : data_(std::make_unique<IndexData>()),
      context_length_(context_length),
      eds_(std::move(eds)),
      n_(0), m_(0), N_(0) {
    // Create temporary directory for index
    index_dir_ = std::filesystem::temp_directory_path() / ("biofmi_index_" + std::to_string(context_length));
    std::filesystem::create_directories(index_dir_);

    // Set up metadata file paths
    reference_filepath_ = index_dir_ / "reference.txt";
    changes_filepath_ = index_dir_ / "changes.txt";
}

BioFMI::BioFMI(const std::filesystem::path& eds_file, Length context_length)
    : data_(std::make_unique<IndexData>()),
      context_length_(context_length),
      n_(0), m_(0), N_(0) {
    // Load EDS from file
    eds_ = EDS::load(eds_file);

    // Create index directory next to EDS file
    std::string filename = eds_file.filename().replace_extension("");
    index_dir_ = eds_file.parent_path() / (filename + ".index");
    std::filesystem::create_directories(index_dir_);

    // Set up metadata file paths
    reference_filepath_ = index_dir_ / "reference.txt";
    changes_filepath_ = index_dir_ / "changes.txt";
}

BioFMI::BioFMI(const std::filesystem::path& index_dir)
    : data_(std::make_unique<IndexData>()),
      index_dir_(index_dir),
      context_length_(0),
      n_(0), m_(0), N_(0) {
    // Load index from directory
    load(index_dir);
}

BioFMI::~BioFMI() = default;
BioFMI::BioFMI(BioFMI&&) noexcept = default;
BioFMI& BioFMI::operator=(BioFMI&&) noexcept = default;

void BioFMI::build() {
    try {
        std::cout << "  (0/4) Parsing EDS..." << std::flush;
        parse_eds();
        std::cout << " done" << std::endl;

        std::cout << "  (1/4) Building FM-index over reference string..." << std::flush;
        build_reference_index();
        std::cout << " done" << std::endl;

        std::cout << "  (2/4) Building FM-index over string of changes..." << std::flush;
        build_changes_index();
        std::cout << " done" << std::endl;

        std::cout << "  (3/4) Building rank and select support structures..." << std::flush;
        build_metadata_structures();
        std::cout << " done" << std::endl;

        std::cout << "  (4/4) Cleaning up temporary files..." << std::flush;
        // Remove temporary metadata files
        std::filesystem::remove(reference_filepath_);
        std::filesystem::remove(changes_filepath_);
        std::cout << " done" << std::endl;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Index building failed: ") + e.what());
    }
}

void BioFMI::save(const std::filesystem::path& output_dir) {
    // Create output directory if it doesn't exist
    std::filesystem::create_directories(output_dir);

    // Save FM-indexes
    std::string base_name = output_dir / "index";
    sdsl::store_to_file(data_->reference_index, base_name + ".ri");
    sdsl::store_to_file(data_->changes_index, base_name + ".ci");

    // Save bit vectors
    sdsl::store_to_file(data_->loc, base_name + ".loc");
    sdsl::store_to_file(data_->iloc, base_name + ".iloc");
    sdsl::store_to_file(data_->tloc, base_name + ".tloc");

    // Save metadata arrays
    sdsl::store_to_file(data_->base_positions, base_name + ".abp");
    sdsl::store_to_file(data_->set_sizes, base_name + ".ss");
    sdsl::store_to_file(data_->offsets, base_name + ".aof");

    // Save index metadata (context_length, n, m, N)
    std::ofstream meta_file(base_name + ".meta");
    if (meta_file.is_open()) {
        meta_file << context_length_ << "\n";
        meta_file << n_ << "\n";
        meta_file << m_ << "\n";
        meta_file << N_ << "\n";
        meta_file.close();
    }
}

void BioFMI::load(const std::filesystem::path& index_dir) {
    std::string base_name = index_dir / "index";

    // Load metadata first
    std::ifstream meta_file(base_name + ".meta");
    if (meta_file.is_open()) {
        meta_file >> context_length_;
        meta_file >> n_;
        meta_file >> m_;
        meta_file >> N_;
        meta_file.close();
    }

    // Load FM-indexes
    sdsl::load_from_file(data_->reference_index, base_name + ".ri");
    sdsl::load_from_file(data_->changes_index, base_name + ".ci");

    // Load bit vectors
    sdsl::load_from_file(data_->loc, base_name + ".loc");
    sdsl::load_from_file(data_->iloc, base_name + ".iloc");
    sdsl::load_from_file(data_->tloc, base_name + ".tloc");

    // Load metadata arrays
    sdsl::load_from_file(data_->base_positions, base_name + ".abp");
    sdsl::load_from_file(data_->set_sizes, base_name + ".ss");
    sdsl::load_from_file(data_->offsets, base_name + ".aof");

    // Build rank and select support structures from loaded bit vectors
    data_->riloc = sdsl::rank_support_v<>(&data_->iloc);
    data_->rloc = sdsl::rank_support_v<>(&data_->loc);
    data_->rtloc = sdsl::rank_support_v<>(&data_->tloc);
    data_->sloc = sdsl::select_support_mcl<>(&data_->loc);
}

BioFMI::ResultMap BioFMI::locate(const String& pattern) {
    // Chunk-based dual-index search.
    //
    // The pattern is split into (l+1)-char chunks.  Candidate occurrences are
    // tracked across chunks in two alternating hash maps.  Each map entry maps a
    // key K to a list of (origin, changes) pairs.  The key for a chunk starting
    // at T0-relative position P is K = P - C, where C is the total change-content
    // length consumed by this chunk (0 for a pure-reference chunk, or the length
    // of the degenerate alternative for a changes chunk).  When processing chunk k,
    // continuity with chunk k-1 is checked by looking up K_k = T0_start_k - (l+1):
    // if chunk k-1 set key K_{k-1} = K_k the chain is valid.
    //
    // The origin (actual T0 start of the first chunk) is stored inside each
    // OccurrenceInfo and recovered only in convert_hash_to_result().
    //
    // Worked example — EDS "AAATTT{G,C}AAATTT", l=3, pattern "TTTGAAAT" (2 chunks):
    //
    //   changes_text = "#TTTGAAA#TTTCAAA#"
    //   base_positions = [6, 12]   offsets = [1, 1]   (G and C each len 1)
    //
    //   chunk 0 "TTTG": found in changes_text at file pos 1.
    //     change_number = rloc(1) = 1   (loc[0] is the only 1-bit before pos 1)
    //     pre_hash_loc  = sloc(1) = 0   (first 1-bit in loc)
    //     offset = 1 - (0 + 3 + 1) = -3;  previous_outside_change = true
    //     loc_new = base_positions[0] + (-3) = 6 - 3 = 3
    //     key = loc_new - change_offset = 3 - 1 = 2;  origin = (3, {1})
    //     → old_hash_map_ = { 2: [(3,{1})] }
    //
    //   chunk 1 "AAAT": found in reference_text at T0 pos 6.
    //     look up find(6 - 4) = find(2) → hit.  Case change→ref: back()=1 ≤ set_sizes[1]=2 → keep.
    //     → new_hash_map_ = { 6: [(3,{1})] }
    //
    //   convert_hash_to_result: origin=3, changes {1} → 0-based {0}.
    //   Result: { position=3, changes=[0] }

    // Clear hash maps
    new_hash_map_.clear();
    old_hash_map_.clear();

    // Chunk size is l+1: each chunk covers exactly l chars of context plus 1 char
    // of content (or reference), maintaining the invariant context_size == chunk_size - 1.
    const size_t chunk_size = context_length_ + 1;

    if ((pattern.size() % chunk_size) != 0) {
        throw std::runtime_error("Pattern length must be a multiple of context_length + 1 (" +
                                std::to_string(chunk_size) + ")");
    }

    size_t num_chunks = pattern.size() / chunk_size;

    for (size_t chunk_idx = 0; chunk_idx < num_chunks; chunk_idx++) {
        size_t chunk_start = chunk_idx * chunk_size;
        String chunk = pattern.substr(chunk_start, chunk_size);

        // Search in both indexes
        process_reference_matches(chunk, chunk_idx);
        process_changes_matches(chunk, chunk_idx);

        // Early termination if no matches found
        if (new_hash_map_.empty()) {
            old_hash_map_.clear();
            return ResultMap{};
        }

        // Swap for next iteration
        std::swap(old_hash_map_, new_hash_map_);
        new_hash_map_.clear();
    }

    // Convert final hash map to ResultMap
    return convert_hash_to_result(old_hash_map_);
}

size_t BioFMI::count(const String& pattern) {
    auto result = locate(pattern);
    size_t total = 0;
    for (const auto& [seq_id, occs] : result)
        total += occs.size();
    return total;
}

BioFMI::IndexStats BioFMI::get_statistics() const {
    IndexStats stats;
    stats.context_length = context_length_;
    stats.num_changes = data_->set_sizes.size();
    stats.reference_length = data_->base_positions.empty() ? 0 : data_->base_positions.back();

    // Calculate sizes in megabytes
    stats.reference_index_size_mb = sdsl::size_in_mega_bytes(data_->reference_index);
    stats.changes_index_size_mb = sdsl::size_in_mega_bytes(data_->changes_index);

    double bit_vectors_size = sdsl::size_in_mega_bytes(data_->loc) +
                               sdsl::size_in_mega_bytes(data_->iloc) +
                               sdsl::size_in_mega_bytes(data_->tloc);

    double metadata_size = sdsl::size_in_mega_bytes(data_->base_positions) +
                           sdsl::size_in_mega_bytes(data_->set_sizes) +
                           sdsl::size_in_mega_bytes(data_->offsets);

    double rank_select_size = sdsl::size_in_mega_bytes(data_->rloc) +
                              sdsl::size_in_mega_bytes(data_->riloc) +
                              sdsl::size_in_mega_bytes(data_->rtloc) +
                              sdsl::size_in_mega_bytes(data_->sloc);

    stats.total_size_mb = stats.reference_index_size_mb +
                          stats.changes_index_size_mb +
                          bit_vectors_size +
                          metadata_size +
                          rank_select_size;

    // Calculate total length of changes
    stats.changes_total_length = 0;
    for (int offset : data_->offsets) {
        stats.changes_total_length += offset;
    }

    return stats;
}

void BioFMI::print_statistics(std::ostream& os) const {
    auto stats = get_statistics();

    os << "BIO-FMI Index Statistics:\n";
    os << "  Context length: " << stats.context_length << "\n";
    os << "  Number of symbols: " << n_ << "\n";
    os << "  Total characters: " << N_ << "\n";
    os << "  Total strings: " << m_ << "\n";
    os << "  Number of degenerate symbols: " << stats.num_changes << "\n";
    os << "  Reference length: " << stats.reference_length << "\n";
    os << "  Changes total length: " << stats.changes_total_length << "\n";
    os << "\nIndex Sizes:\n";
    os << "  Reference index: " << stats.reference_index_size_mb << " MB\n";
    os << "  Changes index: " << stats.changes_index_size_mb << " MB\n";
    os << "  Total index size: " << stats.total_size_mb << " MB\n";
}

void BioFMI::print_result(const ResultMap& result, std::ostream& os) const {
    for (const auto& [seq_id, occurrences] : result) {
        for (const auto& [position, changes] : occurrences) {
            os << position << "[ ";
            for (int change_num : changes) {
                os << change_num << " ";
            }
            os << "]\n";
        }
    }
}

void BioFMI::parse_eds() {
    if (eds_.empty()) {
        throw std::runtime_error("Cannot parse empty EDS");
    }

    // Cache EDS statistics
    n_ = eds_.length();
    m_ = eds_.cardinality();
    N_ = eds_.size();

    const auto& metadata = eds_.get_metadata();

    // Open output files for reference and changes sequences
    std::ofstream ref_file(reference_filepath_, std::ios::out);
    std::ofstream chan_file(changes_filepath_, std::ios::out);
    if (!ref_file.is_open() || !chan_file.is_open()) {
        throw std::runtime_error("Unable to open metadata files for writing");
    }

    // cl = l: each entry stores l chars of context on each side, so chunk queries
    // of size l+1 can straddle any reference-change boundary.
    unsigned int cl = context_length_;
    // Initialise to cl separator chars: used when the first symbol is degenerate
    // (no preceding reference segment to draw context from).  Separator is not a
    // valid DNA character, so a chunk query can never match across it.
    std::string context_left(cl, CHANGE_SEPARATOR);
    std::string context_right("");

    // Start with separator character
    ref_file << CHANGE_SEPARATOR;
    chan_file << CHANGE_SEPARATOR;

    // Initialize bit vectors
    // tloc_: marks positions in reference sequence
    // loc_: marks positions in changes sequence (per string)
    // iloc_: marks positions at end of each degenerate set in changes sequence
    size_t estimated_ref_size = metadata.num_common_chars + n_ + 1;

    // Correct size estimate for the changes file:
    //   degenerate_chars: actual character content of all degenerate strings
    //   m_deg:            number of individual strings inside degenerate symbols
    //   each string contributes at most cl left-ctx + string + cl right-ctx + 1 separator
    // Note: metadata.total_change_size counts commas (separators), NOT characters.
    size_t degenerate_chars = N_ - metadata.num_common_chars;
    size_t n_non_deg        = n_ - metadata.num_degenerate_symbols;
    size_t m_deg            = m_ - n_non_deg;
    size_t estimated_chan_size = 2 + degenerate_chars + m_deg * (2 * cl + 1);

    data_->tloc = sdsl::bit_vector(estimated_ref_size, 0);
    data_->loc = sdsl::bit_vector(estimated_chan_size, 0);
    data_->iloc = sdsl::bit_vector(estimated_chan_size, 0);

    data_->tloc[0] = 1;
    data_->loc[0] = 1;
    data_->iloc[0] = 1;

    size_t chi = 0;  // Current string index across all symbols
    int base_pos = 0;  // Current position in reference sequence
    int set_size_cumulative = 0;  // Cumulative set size

    // First position handling
    if (n_ > 0 && metadata.is_degenerate[0]) {
        data_->base_positions.push_back(0);
    }

    // Iterate through all symbols in the EDS
    for (size_t x = 0; x < n_; x++) {
        StringSet symbol = eds_.read_symbol(x);
        size_t symbol_size = metadata.symbol_sizes[x];

        if (!metadata.is_degenerate[x]) {
            // NON-DEGENERATE (reference) symbol
            base_pos += symbol[0].size();
            data_->base_positions.push_back(base_pos);

            // Write to reference file
            ref_file << symbol[0];
            data_->tloc[ref_file.tellp()] = 1;
            ref_file << CHANGE_SEPARATOR;

            // Update left context for next degenerate symbol.
            // Pad with separator on the left when the segment is shorter than cl
            // so every stored entry has exactly cl chars of left context.
            if (symbol[0].size() < cl) {
                context_left = std::string(cl - symbol[0].size(), CHANGE_SEPARATOR)
                               + symbol[0];
            } else {
                context_left = symbol[0].substr(symbol[0].size() - cl, cl);
            }
        } else {
            // DEGENERATE (changes) symbol
            set_size_cumulative += symbol_size;
            data_->set_sizes.push_back(set_size_cumulative);

            // Determine right context from next non-degenerate symbol.
            // Pad with separator on the right when the segment is shorter than cl
            // (including when there is no following segment at all).
            if (x + 1 >= n_) {
                context_right = std::string(cl, CHANGE_SEPARATOR);
            } else {
                StringSet next_symbol = eds_.read_symbol(x + 1);
                if (next_symbol[0].size() < cl) {
                    context_right = next_symbol[0]
                                    + std::string(cl - next_symbol[0].size(), CHANGE_SEPARATOR);
                } else {
                    context_right = next_symbol[0].substr(0, cl);
                }
            }

            // Write all strings in this degenerate symbol with contexts
            for (size_t i = 0; i < symbol_size; i++) {
                data_->offsets.push_back(symbol[i].size());

                // Write: left_context + string + right_context + separator
                chan_file << context_left;
                chan_file << symbol[i];
                chan_file << context_right;

                std::streampos pos = chan_file.tellp();
                data_->loc[pos] = 1;
                chan_file << CHANGE_SEPARATOR;

                // Mark last string in set with iloc
                if (i == symbol_size - 1) {
                    data_->iloc[pos] = 1;
                }
            }
        }

        chi += symbol_size;
    }

    ref_file.close();
    chan_file.close();
}

void BioFMI::build_reference_index() {
    // Use SDSL's construct function to build FM-index from file
    sdsl::construct(data_->reference_index, reference_filepath_.string(), 1);
}

void BioFMI::build_changes_index() {
    // Use SDSL's construct function to build FM-index from file
    sdsl::construct(data_->changes_index, changes_filepath_.string(), 1);
}

void BioFMI::build_metadata_structures() {
    // Build rank and select support structures for the bit vectors
    data_->riloc = sdsl::rank_support_v<>(&data_->iloc);
    data_->rloc = sdsl::rank_support_v<>(&data_->loc);
    data_->rtloc = sdsl::rank_support_v<>(&data_->tloc);
    data_->sloc = sdsl::select_support_mcl<>(&data_->loc);
}

void BioFMI::process_reference_matches(const String& chunk, size_t chunk_idx) {
    auto ref_locations = sdsl::locate(data_->reference_index, chunk);

    for (auto loc : ref_locations) {
        // Determine block number using tloc rank
        int block_number = data_->rtloc(loc);

        // Adjust position: strip leading $-separators to get 0-based T0 coordinate.
        // rtloc(loc) gives 1-based rank of $ chars up to and including loc, so
        // subtracting it removes exactly the separators that precede this char.
        loc = loc - block_number;

        if (chunk_idx == 0) {
            // First chunk: save initial position
            new_hash_map_[loc] = {{loc, {}}};
        } else {
            // Validate continuity with previous chunk
            auto it = old_hash_map_.find(loc - (int)(context_length_ + 1));
            if (it != old_hash_map_.end()) {
                for (const auto& occ : it->second) {
                    // Case 1: Previous was in reference (empty change vector).
                    // Only allow ref→ref continuation within the SAME reference block.
                    // In an l-EDS, consecutive reference blocks are always separated by
                    // a degenerate symbol, so cross-block continuations don't exist.
                    if (occ.second.empty()) {
                        int block_start = (block_number >= 2)
                                          ? (int)data_->base_positions[block_number - 2]
                                          : 0;
                        if ((int)occ.first >= block_start) {
                            new_hash_map_[loc].push_back(occ);
                        }
                    }
                    // Case 3: Previous was in change, check if change is in valid set
                    else if (occ.second.back() <= data_->set_sizes[block_number - 1]) {
                        new_hash_map_[loc].push_back(occ);
                    }
                }
            }
        }
    }
}

void BioFMI::process_changes_matches(const String& chunk, size_t chunk_idx) {
    auto change_locations = sdsl::locate(data_->changes_index, chunk);

    for (auto loc : change_locations) {
        // Determine position in changes sequence
        int block_number = data_->riloc(loc) - 1;
        int change_number = data_->rloc(loc);
        int pre_hash_loc = data_->sloc(change_number);

        // Calculate offset within change content.
        // Entry layout: <prev_#> [cl left-ctx] [change] [cl right-ctx] <#>  (cl = context_length_)
        // Change content starts at file byte (pre_hash_loc + 1 + cl) = pre_hash_loc + context_length_ + 1.
        // offset < 0: match starts in the left context (preceding reference).
        int offset = (int)loc - (pre_hash_loc + (int)context_length_ + 1);
        // previous_outside_change: the current chunk window starts at or before
        // the change content boundary (offset <= 0).  When offset == 0 the
        // window starts exactly at the first char of the change, meaning chunk
        // l-1 chars earlier was in the left context (reference / earlier change).
        // When offset < 0 it is clearly in the left context.
        bool previous_outside_change = (offset <= 0);

        // Every entry has exactly cl chars of left context (padded with separator
        // at boundaries), so the normal path is always valid.
        loc = data_->base_positions[block_number] + offset;

        int change_offset = data_->offsets[change_number - 1];

        if (chunk_idx == 0) {
            // First chunk: save initial position with change number
            if (new_hash_map_.find(loc - change_offset) == new_hash_map_.end()) {
                new_hash_map_[loc - change_offset] = {};
            }
            new_hash_map_[loc - change_offset].push_back({loc, {change_number}});
        } else {
            // Validate continuity with previous chunk
            validate_change_continuity(loc, change_offset, change_number,
                                      block_number, previous_outside_change);
        }
    }
}

void BioFMI::validate_change_continuity(int loc, int change_offset, int change_number,
                                        int block_number, bool previous_outside_change) {
    if (!previous_outside_change) {
        // Previous chunk was in same change string
        auto it = old_hash_map_.find(loc - change_offset - (int)(context_length_ + 1));
        if (it != old_hash_map_.end()) {
            for (const auto& occ : it->second) {
                // Case 2: Same change number (continuous within one change)
                if (!occ.second.empty() && occ.second.back() == change_number) {
                    if (new_hash_map_.find(loc - change_offset) == new_hash_map_.end()) {
                        new_hash_map_[loc - change_offset] = {};
                    }
                    new_hash_map_[loc - change_offset].push_back(occ);
                }
            }
        }
    } else {
        // Previous chunk was in different position
        auto it = old_hash_map_.find(loc - (int)(context_length_ + 1));
        if (it != old_hash_map_.end()) {
            for (auto occ : it->second) {
                if (new_hash_map_.find(loc - change_offset) == new_hash_map_.end()) {
                    new_hash_map_[loc - change_offset] = {};
                }

                // Case 3: Previous in reference, current in change
                if (occ.second.empty()) {
                    occ.second.push_back(change_number);
                    new_hash_map_[loc - change_offset].push_back(occ);
                }
                // Case 4: Previous in change, current in different change
                else if (occ.second.back() <= data_->set_sizes[block_number]) {
                    occ.second.push_back(change_number);
                    new_hash_map_[loc - change_offset].push_back(occ);
                }
            }
        }
    }
}

BioFMI::ResultMap BioFMI::convert_hash_to_result(const HashType& hash_map) {
    ResultMap result;

    for (const auto& [position, occurrences] : hash_map) {
        for (const auto& [origin_pos, changes] : occurrences) {
            // Convert change indices from 1-based (SDSL rank) to 0-based (spec).
            // Internal hash maps use 1-based values for rank/select consistency;
            // the public ResultMap must expose 0-based global alternative indices.
            std::vector<int> zero_based_changes;
            zero_based_changes.reserve(changes.size());
            for (int c : changes) zero_based_changes.push_back(c - 1);
            // Sequence ID 0 (single sequence support for now)
            result[0].push_back({origin_pos, zero_based_changes});
        }
    }

    last_result_ = result;
    return result;
}

void BioFMI::dump_readable(const std::filesystem::path& dump_path) const {
    std::ofstream out(dump_path);
    if (!out.is_open())
        throw std::runtime_error("Cannot open dump file: " + dump_path.string());

    const size_t WIDTH = 60;

    // Replace separator/control chars with '$' for display.
    auto printable = [](char c) -> char {
        return (static_cast<unsigned char>(c) < 32 || c == 127) ? '$' : c;
    };

    out << "=== BioFMI Index Dump ===\n";
    out << "context_length:  " << context_length_ << "\n";
    out << "n (symbols):     " << n_ << "\n";
    out << "m (strings):     " << m_ << "\n";
    out << "N (total chars): " << N_ << "\n\n";

    // --- Reference string + tloc ---
    // Extract original text from FM-index (exclude SDSL sentinel at size()-1).
    std::string ref_text = sdsl::extract(data_->reference_index, (size_t)0,
                                         data_->reference_index.size() - 2);
    out << "=== Reference String (length=" << ref_text.size() << ") ===\n";
    out << "  $ marks reference block boundaries; tloc is 1 at each $.\n\n";
    for (size_t start = 0; start < ref_text.size(); start += WIDTH) {
        size_t end = std::min(start + WIDTH, ref_text.size());
        out << std::setw(8) << start << " |";
        for (size_t i = start; i < end; i++) out << printable(ref_text[i]);
        out << "\n";
        out << "         |tloc: ";
        for (size_t i = start; i < end; i++)
            out << (i < data_->tloc.size() && data_->tloc[i] ? '1' : '.');
        out << "\n";
    }
    out << "\n";

    // --- Changes string + loc + iloc ---
    std::string ch_text = sdsl::extract(data_->changes_index, (size_t)0,
                                        data_->changes_index.size() - 2);
    out << "=== Changes String (length=" << ch_text.size() << ") ===\n";
    out << "  Each $-delimited segment: left_ctx + change + right_ctx.\n";
    out << "  loc:  1 at end of each change's content (before right_ctx + $).\n";
    out << "  iloc: also 1 for the last string in each degenerate set.\n\n";
    for (size_t start = 0; start < ch_text.size(); start += WIDTH) {
        size_t end = std::min(start + WIDTH, ch_text.size());
        out << std::setw(8) << start << " |";
        for (size_t i = start; i < end; i++) out << printable(ch_text[i]);
        out << "\n";
        out << "         |loc : ";
        for (size_t i = start; i < end; i++)
            out << (i < data_->loc.size() && data_->loc[i] ? '1' : '.');
        out << "\n";
        out << "         |iloc: ";
        for (size_t i = start; i < end; i++)
            out << (i < data_->iloc.size() && data_->iloc[i] ? '1' : '.');
        out << "\n";
    }
    out << "\n";

    // --- Metadata arrays ---
    out << "=== Metadata Arrays ===\n";
    out << "base_positions (abp) — cumulative reference length before each symbol:\n  [";
    for (size_t i = 0; i < data_->base_positions.size(); i++) {
        if (i) out << ", ";
        out << data_->base_positions[i];
    }
    out << "]\n\n";

    out << "set_sizes (ss) — cumulative string count through each degenerate set:\n  [";
    for (size_t i = 0; i < data_->set_sizes.size(); i++) {
        if (i) out << ", ";
        out << data_->set_sizes[i];
    }
    out << "]\n\n";

    out << "offsets (aof) — length of each change string (excluding context):\n  [";
    for (size_t i = 0; i < data_->offsets.size(); i++) {
        if (i) out << ", ";
        out << data_->offsets[i];
    }
    out << "]\n";
}

BioFMI::IndexSnapshot BioFMI::get_snapshot() const {
    IndexSnapshot s;

    s.ref_text = sdsl::extract(data_->reference_index, (size_t)0,
                               data_->reference_index.size() - 2);
    s.changes_text = sdsl::extract(data_->changes_index, (size_t)0,
                                   data_->changes_index.size() - 2);

    s.base_positions = data_->base_positions;
    s.set_sizes      = data_->set_sizes;
    s.offsets        = data_->offsets;

    for (size_t i = 0; i < data_->tloc.size(); i++)
        if (data_->tloc[i]) s.tloc_ones.push_back(i);
    for (size_t i = 0; i < data_->loc.size(); i++)
        if (data_->loc[i]) s.loc_ones.push_back(i);
    for (size_t i = 0; i < data_->iloc.size(); i++)
        if (data_->iloc[i]) s.iloc_ones.push_back(i);

    return s;
}

} // namespace biofmi
