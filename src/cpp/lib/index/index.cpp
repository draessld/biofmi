#include "index.hpp"
#include <cstdlib>
#include <cstdio>

// SDSL headers included in implementation only
#include <sdsl/suffix_arrays.hpp>

#include <chrono>
#include <iomanip>

namespace biofmi {

// Diagnostic tracing of the chunk stitch, enabled with BIOFMI_TRACE=1. Prints
// one line per changes-index hit: chunk, position, change number, offset within
// the alternative, block, previous_outside_change, T0 and hash key. Every stitch
// bug found so far was diagnosed from this — the keys colliding, or an offset
// landing in context rather than content, is visible immediately and is very
// hard to reason out on paper. One branch on a cached static when off.
static bool trace_on() {
    static const bool on = (getenv("BIOFMI_TRACE") != nullptr);
    return on;
}
#define TRACE(...) do { if (trace_on()) { fprintf(stderr, __VA_ARGS__); } } while (0)


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

    // Degenerate-string -> global-string-id map, for source-aware search.
    sdsl::store_to_file(deg_to_global_, base_name + ".d2g");

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

    // Optional: indexes built before source-aware search have no .d2g. Leave the
    // map empty rather than failing — such an index still answers CARTESIAN
    // queries, and attach_sources() is what rejects the combination.
    if (std::filesystem::exists(base_name + ".d2g")) {
        sdsl::load_from_file(deg_to_global_, base_name + ".d2g");
    } else {
        deg_to_global_.clear();
    }

    // Build rank and select support structures from loaded bit vectors
    data_->riloc = sdsl::rank_support_v<>(&data_->iloc);
    data_->rloc = sdsl::rank_support_v<>(&data_->loc);
    data_->rtloc = sdsl::rank_support_v<>(&data_->tloc);
    data_->sloc = sdsl::select_support_mcl<>(&data_->loc);
}

// ---------------------------------------------------------------- sources

void BioFMI::attach_sources(const std::filesystem::path& sources_file) {
    auto src = Sources::load(sources_file);
    attach_sources_impl(std::move(src), sources_file);
}

void BioFMI::attach_sources(const std::filesystem::path& sources_file,
                            Sources::Format format) {
    auto src = Sources::load(sources_file, format);
    attach_sources_impl(std::move(src), sources_file);
}

void BioFMI::attach_sources_impl(std::shared_ptr<Sources> src,
                                 const std::filesystem::path& sources_file) {
    if (!src) {
        throw std::runtime_error("Could not load sources from " + sources_file.string());
    }

    // Without the map there is no way to turn a change_number into the global
    // string id Sources is keyed by, so source-aware search would silently
    // associate the wrong path sets. Refuse instead.
    if (deg_to_global_.empty()) {
        throw std::runtime_error(
            "Index has no .d2g map, so sources cannot be attached: it was built "
            "before source-aware search existed. Rebuild it with biofmi-build.");
    }

    // The sources must describe the same l-EDS that was indexed. m_ counts every
    // string, degenerate and common alike, which is exactly what cardinality is.
    if (m_ != 0 && src->cardinality() != m_) {
        throw std::runtime_error(
            "Sources cardinality (" + std::to_string(src->cardinality()) +
            ") does not match the indexed l-EDS string count (" + std::to_string(m_) +
            "): " + sources_file.string() + " describes a different EDS.");
    }

    sources_ = std::move(src);
    num_paths_ = sources_->num_paths();
}

bool BioFMI::pathset_empty(const PathSet& s, size_t num_paths) {
    if (s.empty()) return true;             // explicit empty set
    if (s.front() != 0) return false;       // explicit, non-empty
    // Complement: {0} plus k distinct exceptions covers nothing iff k == num_paths.
    // num_paths == 0 means "unknown", in which case only a bare {0} is universal
    // and we cannot prove emptiness — stay conservative and keep the branch.
    if (num_paths == 0) return false;
    return (s.size() - 1) >= num_paths;
}

bool BioFMI::eds_starts_degenerate() const {
    // parse_eds() pushes a leading 0 into base_positions in exactly this case,
    // so the first entry being 0 is the record of it.
    return !data_->base_positions.empty() && data_->base_positions[0] == 0;
}

int BioFMI::set_before_block(int block_number) const {
    // rtloc ranks the separators of the reference text `#seg#seg#...`, so the
    // first block is block_number 1. Without a leading degenerate symbol that
    // block has no set before it (-1) and block j is preceded by set j-2; with
    // one, every block is shifted by a set and block j is preceded by set j-1.
    return block_number - (eds_starts_degenerate() ? 1 : 2);
}

void BioFMI::empty_alternatives_of(int set_idx, std::vector<int>& out) const {
    out.clear();
    if (set_idx < 0 || set_idx >= (int)data_->set_sizes.size()) return;

    const int first = (set_idx == 0) ? 1 : data_->set_sizes[set_idx - 1] + 1;
    const int last  = data_->set_sizes[set_idx];
    for (int cn = first; cn <= last; cn++)
        if (data_->offsets[cn - 1] == 0) out.push_back(cn);
}

void BioFMI::bridge_empty_sets(const OccurrenceInfo& occ, int target, int t0,
                               std::vector<OccurrenceInfo>& out) const {
    std::vector<OccurrenceInfo> live{occ};
    std::vector<int> empties;

    for (int s = occ.next_set; s < target; s++) {
        // A set can be crossed without spelling a character only where it sits
        // exactly at this T0 — anywhere else the match would have to skip real
        // reference text — and only through a zero-length alternative.
        if (s >= (int)data_->base_positions.size() ||
            (int)data_->base_positions[s] != t0) return;

        empty_alternatives_of(s, empties);
        if (empties.empty()) return;

        std::vector<OccurrenceInfo> next;
        next.reserve(live.size() * empties.size());
        for (const auto& cand : live) {
            for (int cn : empties) {
                PathSet ps = Sources::intersect_sources(cand.paths, source_of_change(cn));
                if (pathset_empty(ps, num_paths_)) continue;

                // Traversed even though it contributes no character: the match
                // exists only on the path that chose it, and `changes` is what
                // drives the source intersection.
                OccurrenceInfo branch = cand;
                branch.changes.push_back(cn);
                branch.paths = std::move(ps);
                next.push_back(std::move(branch));
            }
        }
        if (next.empty()) return;
        live = std::move(next);
    }

    for (auto& cand : live) {
        cand.next_set = target;
        out.push_back(std::move(cand));
    }
}

int BioFMI::change_offset_of(int change_number) const {
    return data_->offsets[change_number - 1];
}

PathSet BioFMI::source_of_change(int change_number) const {
    // Universal when no sources: every intersection becomes a no-op and locate()
    // behaves exactly as it did before, i.e. CARTESIAN.
    if (!sources_) return PathSet{0};

    const size_t deg_idx = static_cast<size_t>(change_number - 1);  // 1-based -> 0-based
    if (deg_idx >= deg_to_global_.size()) {
        throw std::out_of_range(
            "change_number " + std::to_string(change_number) +
            " is outside the degenerate-string map (" +
            std::to_string(deg_to_global_.size()) + " entries)");
    }
    return sources_->read_source(static_cast<size_t>(deg_to_global_[deg_idx]));
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
    //     look up find(6 - 4) = find(2) → hit.  Case change→ref: back()=1 ≤ set_sizes[0]=2 → keep.
    //     → new_hash_map_ = { 6: [(3,{1})] }
    //
    //   convert_hash_to_result: origin=3, changes {1} → 0-based {0}.
    //   Result: { position=3, changes=[0] }

    // Clear hash maps
    new_hash_map_.clear();
    old_hash_map_.clear();

    const std::vector<ChunkPlan> plan = plan_chunks(pattern.size());

    if (trace_enabled_) {
        trace_.clear();
        trace_.reserve(plan.size());
    }

    for (size_t chunk_idx = 0; chunk_idx < plan.size(); chunk_idx++) {
        const ChunkPlan& cp = plan[chunk_idx];
        String chunk = pattern.substr(cp.start, cp.len);

        const size_t cand_in = trace_enabled_ ? old_hash_map_.size() : 0;
        size_t ref_hits = 0, chg_hits = 0;
        std::chrono::steady_clock::time_point t0;
        if (trace_enabled_) t0 = std::chrono::steady_clock::now();

        if (cp.verify) {
            // Threshold mode, short tail: the surviving candidates already fix
            // where these characters must sit, so check them there instead of
            // asking the FM-index where they occur.
            extend_candidates(chunk, cp.step);
        } else {
            ref_hits = process_reference_matches(chunk, chunk_idx, cp.step);
            chg_hits = process_changes_matches(chunk, chunk_idx, cp.step);
        }

        if (trace_enabled_) {
            const double us = std::chrono::duration<double, std::micro>(
                                  std::chrono::steady_clock::now() - t0).count();
            trace_.push_back(ChunkStat{chunk_idx, cp.len, cp.verify, us,
                                       ref_hits, chg_hits, cand_in,
                                       new_hash_map_.size()});
        }

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

void BioFMI::set_tail_threshold(size_t t) {
    if (t != 0) {
        throw std::invalid_argument(
            "tail_threshold must be 0: verifying a short tail against candidates "
            "is not implemented for tails that cross a symbol boundary, so any "
            "other value would silently drop matches. See extend_candidates().");
    }
    tail_threshold_ = t;
}

std::vector<BioFMI::ChunkPlan> BioFMI::plan_chunks(size_t pattern_len) const {
    // Chunk size is l+1: l chars of context plus one of content, keeping the
    // invariant context_size == chunk_size - 1.
    const size_t chunk_size = context_length_ + 1;
    const int    full_step  = (int)chunk_size;

    if (pattern_len < chunk_size) {
        throw std::runtime_error(
            "Pattern must be at least context_length + 1 (" +
            std::to_string(chunk_size) + ") characters; got " +
            std::to_string(pattern_len));
    }

    const size_t q = pattern_len / chunk_size;   // full chunks
    const size_t r = pattern_len % chunk_size;   // tail

    std::vector<ChunkPlan> plan;
    plan.reserve(q + 1);
    for (size_t k = 0; k < q; k++) {
        plan.push_back({k * chunk_size, chunk_size, full_step, false});
    }
    if (r == 0) return plan;                     // exact multiple: unchanged

    // Search the tail only while it is still selective enough to be worth a
    // lookup; below that, verify it against the surviving candidates.
    //
    // An overlapping final chunk — take the last l+1 characters, so every lookup
    // stays full-length — looks attractive and is unsound. The key a chunk stores
    // subtracts the change content of the *whole* chunk, while an overlapping
    // successor advances only r characters into it, so the fixed-step lookup
    // `loc - r` misses whenever change content falls in the overlapped region.
    // It produces false negatives on exactly those patterns whose overlap
    // straddles a degenerate symbol; test_locate_arbitrary caught it.
    const bool searchable = r >= tail_threshold_;
    plan.push_back({q * chunk_size, r, full_step, !searchable});
    return plan;
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

void BioFMI::print_result(const ResultMap& result, std::ostream& os,
                          bool list_samples) const {
    // Sample sets are appended only in LINEAR mode. In CARTESIAN mode every set
    // is {0} for want of any constraint, and printing "all 294 genomes" next to
    // every hit would read as a finding rather than as an absence of one.
    const bool show_samples = has_sources();

    for (const auto& [seq_id, occurrences] : result) {
        for (const auto& occ : occurrences) {
            os << occ.position << "[ ";
            for (int change_num : occ.changes) {
                os << change_num << " ";
            }
            os << "]";
            if (show_samples) {
                const std::vector<int> ids = expand_paths(occ.paths);
                os << " samples=" << ids.size();
                if (list_samples) {
                    os << "{ ";
                    for (int id : ids) os << id << " ";
                    os << "}";
                }
            }
            os << "\n";
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

    deg_to_global_.clear();

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
                // `chi` counts strings across *all* symbols, so chi + i is this
                // alternative's global string id — the id Sources is keyed by.
                // Pushed in degenerate order, so the array is indexed by
                // (change_number - 1) at query time.
                deg_to_global_.push_back(static_cast<int64_t>(chi + i));
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

// ------------------------------------------------------- tail verification

void BioFMI::extend_candidates(const String& tail, int step) {
    // The continuity rule the searching path uses is: a chunk whose T0 start is
    // `loc` continues a candidate stored under key `loc - step`. Read backwards,
    // a candidate under key K fixes where its continuation must begin —
    //
    //     reference, or a *different* change   ->  T0 = K + step
    //     the *same* change, continuing        ->  T0 = K + change_offset + step
    //
    // — so instead of asking the FM-index where `tail` occurs (the lookup this
    // mode exists to avoid), check the text at the one or two places each
    // candidate permits.
    // Note this leaves `in_change`/`next_set` as the previous chunk left them.
    // A verified tail is always the final chunk, so no stitch ever reads them
    // again; finishing this path across a symbol boundary means maintaining them
    // here too.
    const size_t tlen = tail.size();
    if (tlen == 0) { new_hash_map_ = old_hash_map_; return; }

    const int cl = (int)context_length_;

    for (const auto& [key, occs] : old_hash_map_) {
        if (occs.empty()) continue;
        const int p = key + step;          // continuation in reference / new change
        if (p < 0) continue;

        // ---- (a) the continuation lies in the reference ---------------------
        const int rpos = t0_to_ref_pos(p);
        if (rpos >= 0 && rpos + (int)tlen <= (int)data_->reference_index.size()) {
            const std::string got =
                sdsl::extract(data_->reference_index, (size_t)rpos, (size_t)(rpos + tlen - 1));
            // A separator inside the window means the run crossed a segment
            // boundary, so this is not a contiguous reference stretch.
            if (got == tail && got.find(CHANGE_SEPARATOR) == std::string::npos) {
                for (const auto& occ : occs) {
                    // Reference adds no alternative: changes and paths ride through.
                    new_hash_map_[p].push_back(occ);
                }
            }
        }

        // ---- (b) the continuation lies inside a degenerate alternative ------
        // Every alternative of the set beginning at this T0 position is a
        // candidate; which ones survive is decided by the text and, in LINEAR
        // mode, by whether any genome carries the whole match.
        for (int block = 0; block < (int)data_->set_sizes.size(); block++) {
            if (data_->base_positions.empty() ||
                block >= (int)data_->base_positions.size()) break;

            const int first = (block == 0) ? 1 : data_->set_sizes[block - 1] + 1;
            const int last  = data_->set_sizes[block];

            for (int cn = first; cn <= last; cn++) {
                const int change_offset = data_->offsets[cn - 1];

                // Two ways in: crossing into this alternative (T0 = K + step), or
                // continuing inside it (T0 = K + change_offset + step).
                for (int variant = 0; variant < 2; variant++) {
                    const int want_t0 = (variant == 0) ? p : key + change_offset + step;
                    const int base    = data_->base_positions[block];
                    const int off     = want_t0 - base;
                    if (off < 0 || off >= change_offset) continue;

                    const int pre  = (int)data_->sloc(cn);
                    const int cpos = pre + cl + 1 + off;
                    if (cpos < 0 || cpos + (int)tlen > (int)data_->changes_index.size()) continue;

                    const std::string got =
                        sdsl::extract(data_->changes_index, (size_t)cpos, (size_t)(cpos + tlen - 1));
                    if (got != tail) continue;

                    const PathSet here = source_of_change(cn);
                    for (auto occ : occs) {          // by value: branches diverge
                        const bool same = !occ.changes.empty() && occ.changes.back() == cn;
                        if (variant == 1 && !same) continue;   // "continuing" needs the same alt
                        if (variant == 0 && same)  continue;   // "crossing" needs a new one

                        if (!same) {
                            PathSet next = Sources::intersect_sources(occ.paths, here);
                            if (pathset_empty(next, num_paths_)) continue;
                            occ.changes.push_back(cn);
                            occ.paths = std::move(next);
                        }
                        new_hash_map_[want_t0 - change_offset].push_back(std::move(occ));
                    }
                }
            }
        }
    }
}

int BioFMI::t0_to_ref_pos(int t0) const {
    // The reference text is seg0 # seg1 # ... , so a T0 coordinate and its
    // position in that text differ by exactly the number of separators before
    // it. process_reference_matches() goes the other way with
    // `t0 = ref_pos - rtloc(ref_pos)`; this inverts it by finding the k for
    // which that holds, k being the count of preceding separators.
    if (t0 < 0) return -1;
    // The mapping is many-to-one: the reference text is `#seg#seg#...`, so a
    // separator and the character after it share a T0 coordinate (ref[18]='#'
    // and ref[19]='G' both give t0=16). Only the character is a real position,
    // so separators are skipped — taking the first hit lands on the '#' and
    // reads the wrong window.
    const int n_seg = (int)data_->base_positions.size() + 2;
    for (int k = 0; k <= n_seg; k++) {
        const int ref_pos = t0 + k;
        if (ref_pos >= (int)data_->reference_index.size()) return -1;
        if ((int)data_->rtloc(ref_pos) != k) continue;
        if (data_->tloc[ref_pos]) continue;          // a separator, not a character
        return ref_pos;
    }
    return -1;
}

size_t BioFMI::process_reference_matches(const String& chunk, size_t chunk_idx, int step) {
    auto ref_locations = sdsl::locate(data_->reference_index, chunk);
    const size_t hits = ref_locations.size();

    for (auto loc : ref_locations) {
        // Determine block number using tloc rank
        int block_number = data_->rtloc(loc);

        // Adjust position: strip leading $-separators to get 0-based T0 coordinate.
        // rtloc(loc) gives 1-based rank of $ chars up to and including loc, so
        // subtracting it removes exactly the separators that precede this char.
        loc = loc - block_number;

        // A reference chunk lies wholly within one block — consecutive blocks are
        // separated by a '#' no chunk can match across — so a match sitting here
        // has passed every set before the block and none after it.
        const int after = set_before_block(block_number) + 1;

        if (chunk_idx == 0) {
            // First chunk: save initial position. A pure-reference chunk traverses
            // no alternative, so it constrains nothing and seeds the universal set.
            new_hash_map_[loc] = {OccurrenceInfo{(Position)loc, {}, PathSet{0}, 0, after}};
        } else {
            // Validate continuity with previous chunk
            auto it = old_hash_map_.find(loc - step);
            if (it == old_hash_map_.end()) continue;

            std::vector<OccurrenceInfo> bridged;
            for (auto occ : it->second) {   // by value: it is moved on from here
                // A candidate that stopped inside an alternative resumes inside
                // it. Pure reference cannot pick it up.
                if (occ.in_change != 0) continue;

                // The key already fixed the T0 coordinate; what it cannot fix is
                // which side of a degenerate set the candidate stands on, since
                // a set consumes no T0 and both sides are base_positions[s].
                // next_set is the only thing that separates them, and getting
                // this wrong is how a match hopped a whole symbol.
                if (occ.next_set > after) continue;

                if (occ.next_set == after) {
                    new_hash_map_[loc].push_back(std::move(occ));
                    continue;
                }

                // Reaching this block from further back means crossing a set
                // without spelling it, which only a zero-length alternative
                // allows. Then the two blocks really are adjacent along that
                // path, and "GTAATTTT" against
                // ACGTACGTAA{G,CC,}TTTTGGGGAC{A,T}CCCCAAAG is the match it looks
                // like rather than the silent miss it used to be.
                bridged.clear();
                bridge_empty_sets(occ, after, loc, bridged);
                for (auto& b : bridged) new_hash_map_[loc].push_back(std::move(b));
            }
        }
    }

    return hits;
}

size_t BioFMI::process_changes_matches(const String& chunk, size_t chunk_idx, int step) {
    auto change_locations = sdsl::locate(data_->changes_index, chunk);
    const size_t hits = change_locations.size();

    for (auto loc : change_locations) {
        // iloc marks the end of each degenerate *set*, so this rank is the
        // 0-based set index — the same index base_positions is keyed by.
        int set_idx = data_->riloc(loc) - 1;
        int change_number = data_->rloc(loc);
        int pre_hash_loc = data_->sloc(change_number);

        // Calculate offset within change content.
        // Entry layout: <prev_#> [cl left-ctx] [change] [cl right-ctx] <#>  (cl = context_length_)
        // Change content starts at file byte (pre_hash_loc + 1 + cl) = pre_hash_loc + context_length_ + 1.
        // offset < 0: match starts in the left context (preceding reference).
        int offset = (int)loc - (pre_hash_loc + (int)context_length_ + 1);
        const int alt_len   = change_offset_of(change_number);
        const int chunk_len = (int)chunk.size();

        // The hit must actually touch the alternative, not sit entirely inside
        // the context flanking it. That context replicates *reference* text, so
        // a hit confined to it is a reference match wearing a change's clothes —
        // crediting it would append a change_number the match never traverses.
        //
        // For a full l+1 chunk this is free: each flank is only l characters, so
        // a chunk of l+1 cannot fit inside one. That invariant is precisely why
        // the chunk size is l+1, and a shorter tail chunk is the one case that
        // breaks it — without this check, searching a 1-character tail reported
        // "AATTT" as passing through both alternatives of a symbol it never
        // reaches (test_locate_arbitrary).
        if (offset + chunk_len <= 0 || offset >= alt_len) continue;

        // Where the chunk sits against the alternative decides both what it can
        // continue and what it leaves behind:
        //   starts_inside — it began past the alternative's first character, so
        //                   the previous chunk must have stopped inside it too;
        //   ends_inside   — it stops before the last character, so the next
        //                   chunk must resume inside it.
        const bool starts_inside = (offset > 0);
        const bool ends_inside   = (offset + chunk_len < alt_len);

        // Every entry has exactly cl chars of left context (padded with separator
        // at boundaries), so the normal path is always valid.
        const auto loc_raw = loc;
        loc = data_->base_positions[set_idx] + offset;

        TRACE("  [chg] chunk=%zu loc_cx=%d cn=%d off=%d alt_len=%d set=%d in=%d out_set=%d T0=%d key=%d\n",
              chunk_idx, (int)loc_raw, change_number, offset, alt_len, set_idx,
              (int)starts_inside, ends_inside ? set_idx : set_idx + 1,
              (int)loc, (int)(loc - alt_len));

        if (chunk_idx == 0) {
            // First chunk: save initial position with change number. Seed the
            // running set with this alternative's sources — every later stitch
            // intersects into it, so the whole match is constrained from here.
            PathSet seed = source_of_change(change_number);
            if (pathset_empty(seed, num_paths_)) continue;   // no path carries it

            new_hash_map_[loc - alt_len].push_back(
                OccurrenceInfo{(Position)loc, {change_number}, std::move(seed),
                               ends_inside ? change_number : 0,
                               ends_inside ? set_idx : set_idx + 1});
        } else {
            // Validate continuity with previous chunk
            validate_change_continuity(loc, alt_len, change_number, set_idx,
                                       starts_inside, ends_inside, step);
        }
    }

    return hits;
}

void BioFMI::validate_change_continuity(int loc, int alt_len, int change_number,
                                        int set_idx, bool starts_inside,
                                        bool ends_inside, int step) {
    // The key this hit stores: its T0 start less the alternative's whole content
    // length, which is the virtual coordinate the next chunk looks up.
    const int key_out = loc - alt_len;

    // The end state every surviving candidate inherits from this chunk.
    const int out_change = ends_inside ? change_number : 0;
    const int out_set    = ends_inside ? set_idx : set_idx + 1;

    if (starts_inside) {
        // The chunk begins strictly inside the alternative, so the only thing it
        // can continue is a candidate that stopped strictly inside the same one.
        auto it = old_hash_map_.find(key_out - step);
        if (it == old_hash_map_.end()) return;

        for (auto occ : it->second) {
            if (occ.in_change != change_number) continue;
            // Still the same alternative: it contributes no new constraint,
            // `paths` is already correct and `changes` already names it.
            occ.in_change = out_change;
            occ.next_set  = out_set;
            new_hash_map_[key_out].push_back(std::move(occ));
        }
        return;
    }

    // The chunk begins in the reference preceding this set — or exactly at the
    // alternative's first character — and crosses into it. This is where B4
    // lived: the old code appended the new change number to every partial match
    // unconditionally, pairing every alternative of one degenerate symbol with
    // every alternative of the next. That is the CARTESIAN language. The LINEAR
    // language needs the two to share a path, so fold this alternative's sources
    // into the running set and drop the branch when nothing survives.
    //
    // The fold has to be an accumulation, not a pairwise check: non-empty
    // intersection is not transitive, so validating only adjacent pairs still
    // admits matches no single genome carries.
    const PathSet here = source_of_change(change_number);

    auto it = old_hash_map_.find(loc - step);
    if (it == old_hash_map_.end()) return;

    std::vector<OccurrenceInfo> reaching;
    for (auto occ : it->second) {   // by value: branches must not share `paths`
        // A candidate that stopped inside an alternative resumes inside it, not
        // in the reference this chunk starts from.
        if (occ.in_change != 0) continue;

        // Entering this set means standing in front of it. A candidate that has
        // already passed it cannot re-enter — that is what let one match report
        // two alternatives of a single symbol, `changes` like [10, 9] — and one
        // still short of it may cross what lies between only through zero-length
        // alternatives. Otherwise the match hops a degenerate symbol without
        // spelling it, which is how "TGTCCACA" came to report a path no string
        // spells, and how `TTGTGACT` picked up a second entry that skipped
        // {TGT,CTTG} entirely.
        if (occ.next_set > set_idx) continue;

        reaching.clear();
        if (occ.next_set == set_idx) reaching.push_back(std::move(occ));
        else bridge_empty_sets(occ, set_idx, loc, reaching);

        for (auto& cand : reaching) {
            PathSet next = Sources::intersect_sources(cand.paths, here);
            if (pathset_empty(next, num_paths_)) continue;   // no genome carries it

            cand.changes.push_back(change_number);
            cand.paths = std::move(next);
            cand.in_change = out_change;
            cand.next_set  = out_set;

            new_hash_map_[key_out].push_back(std::move(cand));
        }
    }
}

std::vector<int> BioFMI::expand_paths(const PathSet& paths) const {
    // Without sources the index cannot name genomes: every set is {0} because
    // nothing was ever intersected, so expanding it would assert that the match
    // lies on all 294 paths when in fact none were checked.
    if (!sources_ || num_paths_ == 0) return {};

    const bool complement = !paths.empty() && paths.front() == 0;
    if (!complement) return std::vector<int>(paths.begin(), paths.end());

    // {0, e1..ek} = every path except e1..ek. `paths` is sorted ascending, so a
    // single merge pass over 1..num_paths suffices.
    std::vector<int> out;
    out.reserve(num_paths_);
    size_t ex = 1;  // index into paths, skipping the leading 0
    for (int id = 1; id <= (int)num_paths_; id++) {
        while (ex < paths.size() && paths[ex] < id) ex++;
        if (ex < paths.size() && paths[ex] == id) { ex++; continue; }
        out.push_back(id);
    }
    return out;
}

BioFMI::ResultMap BioFMI::convert_hash_to_result(const HashType& hash_map) {
    ResultMap result;

    for (const auto& [position, occurrences] : hash_map) {
        for (const auto& occ : occurrences) {
            const Position origin_pos = occ.origin;
            const std::vector<int>& changes = occ.changes;
            // Convert change indices from 1-based (SDSL rank) to 0-based (spec).
            // Internal hash maps use 1-based values for rank/select consistency;
            // the public ResultMap must expose 0-based global alternative indices.
            std::vector<int> zero_based_changes;
            zero_based_changes.reserve(changes.size());
            for (int c : changes) zero_based_changes.push_back(c - 1);
            // Sequence ID 0 (single sequence support for now).
            // occ.paths is the accumulated source intersection — the set of
            // genomes carrying this occurrence. It was already computed and
            // tested for emptiness during the search, so reporting it is free.
            result[0].push_back(Occurrence{origin_pos, std::move(zero_based_changes), occ.paths});
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
