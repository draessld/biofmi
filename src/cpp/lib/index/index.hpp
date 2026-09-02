#ifndef BIOFMI_INDEX_HPP
#define BIOFMI_INDEX_HPP

#include <edsparser/common.hpp>
#include <edsparser/formats/eds.hpp>
#include <edsparser/formats/sources.hpp>

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
// PathSet and Sources are declared at global scope by edsparser's sources.hpp,
// unlike the eds.hpp types above which live in namespace edsparser.
using ::PathSet;
using ::Sources;
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

    /**
     * One reported occurrence: where the match starts, which alternatives it
     * traverses, and which genomes carry it.
     */
    struct Occurrence {
        Position position;            // 0-based; see docs/locate_spec.md
        std::vector<int> changes;     // 0-based global alternative indices, in order

        /**
         * The genomes carrying this occurrence, as a complement-encoded PathSet
         * ({0} = every path). Use expand_paths() rather than reading it directly.
         *
         * Meaningful only in LINEAR mode. With no sources attached the search
         * never constrains anything and this is always {0} — which says "every
         * path" because nothing ruled any out, not because the occurrence was
         * shown to lie on all of them. Check has_sources() before reporting it.
         */
        PathSet paths;
    };

    // Result type: map from sequence ID to the occurrences found.
    using ResultMap = std::unordered_map<int, std::vector<Occurrence>>;

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

    /**
     * Shortest tail still worth searching as its own chunk.
     *
     * A pattern of arbitrary length leaves r = |P| mod (l+1) characters after
     * the full chunks. Searching that tail as a short chunk gets less selective
     * as r shrinks: a chunk of r characters has only |alphabet|^r distinct
     * values, so on a large text the lookup returns a large candidate set.
     * Measured at l=3 (a 4-character chunk) on an 8 MB panel: 1.3 s per pattern,
     * against 0.16 ms at l=9.
     *
     * The intended remedy is to verify a short tail against the surviving
     * candidates instead of looking it up. That path is **not implemented** —
     * see extend_candidates() for what is missing — so this currently only
     * accepts 0, meaning "always search the tail". Setting anything else throws
     * rather than silently returning wrong answers.
     */
    void set_tail_threshold(size_t t);
    size_t tail_threshold() const { return tail_threshold_; }

    /**
     * Attach the source (haplotype) sets of the indexed l-EDS, enabling
     * LINEAR/source-aware search.
     *
     * Without this, locate() pairs every alternative of one degenerate symbol
     * with every alternative of the next, which is the CARTESIAN language and
     * over-reports on a LINEAR l-EDS (issue B4). With it, a running path-set
     * intersection is carried along each candidate match and a branch dies the
     * moment no path carries the whole thing.
     *
     * `format` is passed through to Sources::load(); the default auto-detects
     * from the file. The file must describe the *same* l-EDS that was indexed —
     * its cardinality is checked against the index's string count and a
     * mismatch throws rather than silently mis-associating source sets.
     *
     * Must be called after the index is built or loaded.
     */
    void attach_sources(const std::filesystem::path& sources_file);
    void attach_sources(const std::filesystem::path& sources_file,
                        Sources::Format format);

    // True when source-aware (LINEAR) search is active.
    bool has_sources() const { return sources_ != nullptr; }

    // Number of paths (genomes) in the attached sources; 0 when none attached.
    size_t num_paths() const { return num_paths_; }

    /**
     * Expand an Occurrence's `paths` into explicit ascending 1-based path ids.
     *
     * PathSet is complement-encoded — {0} is every path, {0,5} is every path
     * except 5 — so callers that iterate it directly get the wrong answer on
     * roughly half the possible values. This resolves the encoding against the
     * attached panel size.
     *
     * Returns empty when no sources are attached: without them the index has no
     * basis to name genomes, and returning all of them would be an assertion it
     * cannot support.
     */
    std::vector<int> expand_paths(const PathSet& paths) const;

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

    // Snapshot of internal parse_eds() output for structural tests.
    // All positions are 0-based byte offsets into the respective index text.
    struct IndexSnapshot {
        std::string ref_text;            // full reference file content (with '#' separators)
        std::string changes_text;        // full changes file content (with '#' separators)
        std::vector<int> base_positions; // cumulative T0 length before each degenerate set
        std::vector<int> set_sizes;      // cumulative string count through each degenerate set
        std::vector<int> offsets;        // length of each individual degenerate string
        std::vector<size_t> tloc_ones;   // positions i where tloc[i] == 1
        std::vector<size_t> loc_ones;    // positions i where loc[i] == 1
        std::vector<size_t> iloc_ones;   // positions i where iloc[i] == 1
    };
    IndexSnapshot get_snapshot() const;

    // Get the last query results
    const ResultMap& get_last_result() const { return last_result_; }

    /**
     * Per-chunk cost trace for one locate() call.
     *
     * locate() splits a pattern into (l+1)-character chunks and stops the moment
     * the candidate set empties, so the wall time of a query mixes two unrelated
     * quantities: how expensive a chunk is, and how many chunks the pattern
     * survived. A pattern that dies in chunk 0 and one that runs 100 chunks are
     * not comparable, and averaging them measures the pattern set rather than
     * the index. Separating the two is what this records.
     *
     * `ref_hits`/`chg_hits` are raw sdsl::locate() hit counts for the chunk —
     * what the FM-index returned before continuity with the previous chunk is
     * checked. `cand_out` is what survived that check. The gap between them is
     * the selectivity of the stitch, which is the quantity the chunked design
     * actually turns on.
     */
    struct ChunkStat {
        size_t chunk_idx;   // 0-based position in the plan
        size_t chunk_len;   // characters searched (l+1, or a short tail)
        bool   verify;      // tail verified against candidates, never searched
        double time_us;     // wall time for this chunk alone
        size_t ref_hits;    // sdsl::locate() hits in the reference index
        size_t chg_hits;    // sdsl::locate() hits in the changes index
        size_t cand_in;     // candidates carried in from the previous chunk
        size_t cand_out;    // candidates surviving this chunk
    };

    /**
     * Collect a ChunkStat per chunk on every subsequent locate().
     *
     * Off by default: the timer is per chunk, and on a 2-chunk pattern the two
     * clock reads are a measurable fraction of the query. Enable it for cost
     * measurement, not for production search.
     */
    void set_trace(bool on) { trace_enabled_ = on; }
    bool trace_enabled() const { return trace_enabled_; }

    // Trace of the most recent locate(). Empty when tracing is off.
    const std::vector<ChunkStat>& last_trace() const { return trace_; }

    /**
     * Print results in human-readable format.
     *
     * In LINEAR mode each occurrence is annotated with the genomes carrying it.
     * `list_samples` selects the full id list; by default only the count is
     * shown, because a conserved region matches in every genome in the panel and
     * a few hundred ids per line buries the positions they belong to.
     * In CARTESIAN mode nothing is annotated — see Occurrence::paths.
     */
    void print_result(const ResultMap& result, std::ostream& os = std::cout,
                      bool list_samples = false) const;

private:
    // One in-flight candidate match.
    //
    // `paths` is the intersection of the source sets of every alternative the
    // match has traversed so far. It only ever shrinks, so a branch that empties
    // is dead permanently and is pruned at the stitch rather than at the end of
    // the pattern. When no sources are attached it stays universal ({0}) and
    // every intersection is a no-op, which is exactly the old behaviour.
    //
    // It is not bookkeeping: it *is* the set of samples carrying the occurrence.
    struct OccurrenceInfo {
        Position origin;             // T0 start of the first chunk
        std::vector<int> changes;    // 1-based degenerate string numbers traversed
        PathSet paths;               // running source intersection ({0} = all)

        /**
         * Where the next chunk must begin, as the pair (in_change, next_set).
         *
         * The hash key already fixes the T0 coordinate, and T0 alone is not
         * enough. A degenerate set consumes no T0, so the position just before
         * set s and the position just after it are the same coordinate,
         * base_positions[s]. Two candidates ending there — one that has
         * traversed the set, one that has not — collide on the key and were
         * indistinguishable, so the one still short of the set could be
         * continued by a chunk lying beyond it, hopping a degenerate symbol
         * without spelling any of its alternatives.
         *
         * `in_change` is the 1-based change_number when the next character lies
         * strictly inside that alternative, and 0 when it lies in the reference.
         * `next_set` is the 0-based index of the first degenerate set the match
         * has not yet passed — which is exactly what separates the two sides of
         * a boundary.
         *
         * Recording only "which alternative the previous chunk touched", as
         * `last_change` did, conflates ending *inside* an alternative with
         * ending *after* it and says nothing about sets crossed in between.
         * Every positional check bolted onto the case analysis was a partial
         * stand-in for this pair.
         */
        int in_change = 0;
        int next_set  = 0;
    };
    using HashType = std::unordered_map<Position, std::vector<OccurrenceInfo>>;

    // Index data (PIMPL pattern to hide some SDSL details)
    struct IndexData;
    std::unique_ptr<IndexData> data_;

    Length context_length_;
    size_t tail_threshold_ = 0;
    EDS eds_;
    ResultMap last_result_;

    // Source-aware search state. Null unless attach_sources() was called.
    std::shared_ptr<Sources> sources_;
    size_t num_paths_ = 0;

    // Degenerate-string number -> global string id.
    //
    // locate() works in `change_number`, a 1-based rank over degenerate strings
    // only (cf. offsets[change_number - 1]). Sources is indexed by global string
    // id over *all* strings, common symbols included; the two differ by the
    // number of non-degenerate symbols passed so far. That count is not
    // recoverable from the index artifacts, and a loaded index has no EDS to ask
    // (load() never populates eds_), so the mapping is built during parse_eds()
    // — where the walk is left-to-right and the counter is free — and persisted
    // as `.d2g`.
    std::vector<int64_t> deg_to_global_;

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

    // Per-chunk cost trace; see set_trace().
    bool trace_enabled_ = false;
    std::vector<ChunkStat> trace_;

    // Internal methods
    void parse_eds();
    void build_reference_index();
    void build_changes_index();
    void build_metadata_structures();

    /**
     * One step of the chunked search.
     *
     * `step` is how far back in T0 the previous chunk started — always l+1 here.
     * It is a parameter rather than a constant because an earlier attempt varied
     * it for an overlapping final chunk; that turned out to be unsound (see
     * plan_chunks) and the parameter is kept only so the assumption is explicit
     * at each stitch rather than buried in three copies of `context_length_ + 1`.
     */
    struct ChunkPlan {
        size_t start;   // offset into the pattern
        size_t len;     // characters in this chunk
        int    step;    // T0 distance back to the previous chunk's start
        bool   verify;  // extend candidates rather than search (Threshold mode)
    };

    // Split a pattern into chunks according to the active tail mode. Throws if
    // the pattern is too short to yield a single full chunk.
    std::vector<ChunkPlan> plan_chunks(size_t pattern_len) const;

    /**
     * Advance every surviving candidate by the tail characters, checking them
     * at the one position each candidate allows rather than asking the index
     * where the tail occurs.
     *
     * INCOMPLETE — not reachable while set_tail_threshold() rejects non-zero
     * values. It resolves a tail that lies wholly in the reference, and one that
     * lies wholly inside a single alternative, but not a tail that **crosses a
     * symbol boundary** — "CAA" continuing from reference into an alternative,
     * which is the common case. Finishing it means walking the tail character by
     * character across symbol boundaries, branching at each degenerate symbol:
     * a small path-walker over the index, since a loaded index has no EDS to
     * ask. Validated by test_locate_arbitrary once it exists.
     */
    void extend_candidates(const String& tail, int step);

    // T0 coordinate -> position in the reference index text, or -1 when the
    // coordinate does not land in a reference segment. Inverse of the
    // `t0 = ref_pos - rtloc(ref_pos)` mapping process_reference_matches() uses.
    int t0_to_ref_pos(int t0) const;

    // Locate helper methods
    // Both return the raw sdsl::locate() hit count for the chunk, so a trace
    // can separate what the index returned from what survived the stitch.
    size_t process_reference_matches(const String& chunk, size_t chunk_idx, int step);
    size_t process_changes_matches(const String& chunk, size_t chunk_idx, int step);
    void validate_change_continuity(int loc, int alt_len, int change_number,
                                    int set_idx, bool starts_inside,
                                    bool ends_inside, int step);
    ResultMap convert_hash_to_result(const HashType& hash_map);

    // Source set of a degenerate string, by the 1-based `change_number` locate()
    // works in. Returns universal ({0}) when no sources are attached, so callers
    // need no null check and the fold degenerates to the CARTESIAN behaviour.
    PathSet source_of_change(int change_number) const;

    // Content length of a degenerate alternative, by 1-based change_number.
    int change_offset_of(int change_number) const;

    // True when the EDS opens with a degenerate symbol, which shifts the
    // correspondence between a reference block and the sets around it by one.
    bool eds_starts_degenerate() const;

    // 0-based index of the degenerate set immediately preceding reference block
    // `block_number` (the rtloc rank process_reference_matches works in), or -1
    // when the block opens the EDS. One more than this is the set immediately
    // *following* the block, which is what a candidate in that block carries as
    // `next_set`.
    int set_before_block(int block_number) const;

    // Every zero-length alternative of degenerate set `set_idx`, as 1-based
    // change numbers, appended to `out`. Such an alternative makes the reference
    // blocks on either side adjacent along that path, so a match may cross
    // between them without spelling a character. A set may list the empty string
    // more than once — `{ATG,,}` is legal EDS — and those are distinct
    // alternatives carrying distinct source sets, so all of them are returned.
    void empty_alternatives_of(int set_idx, std::vector<int>& out) const;

    /**
     * Advance a candidate across the degenerate sets [occ.next_set, target),
     * appending every surviving way through to `out`.
     *
     * A match reaches `target` without spelling a character only if every set on
     * the way sits exactly at T0 `t0` and offers a zero-length alternative,
     * which the match then traverses — so each one is appended to `changes` and
     * folded into `paths`, exactly as a non-empty alternative would be.
     *
     * Where a set offers several such alternatives the candidate *branches*: the
     * match runs through each of them separately and each is its own occurrence,
     * distinguished by `changes` and by the genomes carrying it. That is why
     * this yields a list rather than mutating in place — collapsing it to the
     * first empty alternative silently dropped the others.
     *
     * `out` is left untouched when no way through survives.
     */
    void bridge_empty_sets(const OccurrenceInfo& occ, int target, int t0,
                           std::vector<OccurrenceInfo>& out) const;

    // Shared tail of the two attach_sources() overloads.
    void attach_sources_impl(std::shared_ptr<Sources> src,
                             const std::filesystem::path& sources_file);

    // Emptiness under complement encoding. A PathSet leading with 0 is a
    // complement: {0} is every path, {0,e1..ek} is every path except those, so
    // it denotes the empty set exactly when k == num_paths. Testing .empty()
    // alone would miss that and keep a branch no genome carries alive.
    static bool pathset_empty(const PathSet& s, size_t num_paths);


};

} // namespace biofmi

#endif // BIOFMI_INDEX_HPP
