#include "index.hpp"

// SDSL headers included in implementation only
#include <sdsl/suffix_arrays.hpp>

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
      eds_(std::move(eds)) {
}

BioFMI::BioFMI(const std::filesystem::path& eds_file, Length context_length)
    : data_(std::make_unique<IndexData>()),
      context_length_(context_length) {
    // TODO: Load EDS from file
    (void)eds_file;
}

BioFMI::BioFMI(const std::filesystem::path& index_dir)
    : data_(std::make_unique<IndexData>()) {
    // TODO: Load index from directory
    (void)index_dir;
}

BioFMI::~BioFMI() = default;
BioFMI::BioFMI(BioFMI&&) noexcept = default;
BioFMI& BioFMI::operator=(BioFMI&&) noexcept = default;

void BioFMI::build() {
    // TODO: Implement index building
}

void BioFMI::save(const std::filesystem::path& output_dir) {
    // TODO: Implement save
    (void)output_dir;
}

void BioFMI::load(const std::filesystem::path& index_dir) {
    // TODO: Implement load
    (void)index_dir;
}

BioFMI::ResultMap BioFMI::locate(const String& pattern) {
    // TODO: Implement locate
    (void)pattern;
    return ResultMap{};
}

size_t BioFMI::count(const String& pattern) {
    // TODO: Implement count
    (void)pattern;
    return 0;
}

BioFMI::IndexStats BioFMI::get_statistics() const {
    // TODO: Implement statistics
    return IndexStats{};
}

void BioFMI::print_statistics(std::ostream& os) const {
    // TODO: Implement print statistics
    (void)os;
}

void BioFMI::print_result(const ResultMap& result, std::ostream& os) const {
    // TODO: Implement print result
    (void)result;
    (void)os;
}

void BioFMI::parse_eds() {
    // TODO: Implement
}

void BioFMI::build_reference_index() {
    // TODO: Implement
}

void BioFMI::build_changes_index() {
    // TODO: Implement
}

void BioFMI::build_metadata_structures() {
    // TODO: Implement
}

BioFMI::ResultMap BioFMI::locate_short(const String& pattern) {
    // TODO: Implement
    (void)pattern;
    return ResultMap{};
}

BioFMI::ResultMap BioFMI::locate_long(const String& pattern) {
    // TODO: Implement
    (void)pattern;
    return ResultMap{};
}

bool BioFMI::validate_chunk_positions(const std::vector<Position>& positions) {
    // TODO: Implement
    (void)positions;
    return false;
}

} // namespace biofmi
