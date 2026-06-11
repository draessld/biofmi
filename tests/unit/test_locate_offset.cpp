/**
 * Offset arithmetic unit test for BioFMI::process_changes_matches()
 *
 * Verifies that change_number / pre_hash_loc / offset are computed correctly
 * by cross-referencing the byte layout from get_snapshot() against the
 * positions returned by locate().
 *
 * EDS: "AAATTT{G,C}AAATTT", l=3  (chunk_size = l+1 = 4)
 *
 * changes_text = "#TTTGAAA#TTTCAAA#"
 *   positions:    0         1
 *                 0123456789012345678
 *   loc[0]=1, loc[8]=1, loc[16]=1
 *   Pre-hash '#' for change 1 ('G') = sloc(1) = 0.
 *   Pre-hash '#' for change 2 ('C') = sloc(2) = 8.
 *
 * Chunk "TTTG" found at file pos 1:
 *   change_number = rloc(1) = 1    (loc[0] is the only 1-bit before pos 1)
 *   pre_hash_loc  = sloc(1) = 0
 *   offset = 1 - (0+3+1) = -3  → previous_outside_change = true
 *   loc_new = base_positions[0] + (-3) = 6-3 = 3
 *   key = loc_new - offsets[0] = 3-1 = 2;  origin = (3, change 1)
 *
 * Chunk "TTTC" found at file pos 9:
 *   change_number = rloc(9) = 2    (loc[0] and loc[8] precede pos 9)
 *   pre_hash_loc  = sloc(2) = 8
 *   offset = 9 - (8+3+1) = -3  → same arithmetic, same loc_new = 3
 *   key = 3-1 = 2;  origin = (3, change 2)
 *
 * After chunk 1 "AAAT" at T0[6]: find(6-4)=find(2) hits both entries.
 *   change→ref: back() ≤ set_sizes[0]=2 → keep.
 *
 * Expected locate() results:
 *   "AAAT"     → { (0,[]), (6,[]) }                 (pure reference)
 *   "TTTGAAAT" → { (3,[0]) }                        (ref→'G'→ref)
 *   "TTTCAAAT" → { (3,[1]) }                        (ref→'C'→ref)
 */

#include "index/index.hpp"
#include <cassert>
#include <iostream>
#include <set>
#include <sstream>

using namespace biofmi;

static BioFMI build(const std::string& eds_str, int l) {
    std::istringstream ss(eds_str);
    EDS eds(ss);
    BioFMI idx(std::move(eds), l);
    idx.build();
    return idx;
}

using Occ = std::pair<int, std::vector<int>>;
static std::set<Occ> flatten(const BioFMI::ResultMap& r) {
    std::set<Occ> s;
    for (const auto& [seq, occs] : r)
        for (const auto& o : occs)
            s.insert(o);
    return s;
}

static void assert_occs(const std::set<Occ>& got, const std::set<Occ>& expected,
                        const char* label) {
    if (got == expected) return;
    std::cerr << "\n[FAIL] " << label << "\n";
    std::abort();
}

void test_offset_arithmetic() {
    std::cout << "Offset arithmetic: AAATTT{G,C}AAATTT l=3... ";

    auto idx = build("AAATTT{G,C}AAATTT", 3);
    auto snap = idx.get_snapshot();

    // Verify the byte layout assumed in the worked example above.
    assert(snap.changes_text == "#TTTGAAA#TTTCAAA#");
    assert((snap.base_positions == std::vector<int>{6, 12}));
    assert((snap.offsets == std::vector<int>{1, 1}));
    assert((snap.loc_ones == std::vector<size_t>{0, 8, 16}));

    // Pure-reference: "AAAT" appears at T0[0] and T0[6].
    assert_occs(flatten(idx.locate("AAAT")),
                std::set<Occ>({{0, {}}, {6, {}}}),
                "locate AAAT");

    // ref → change('G') → ref: origin = T0[3], change index 0 (0-based).
    assert_occs(flatten(idx.locate("TTTGAAAT")),
                std::set<Occ>({{3, {0}}}),
                "locate TTTGAAAT");

    // ref → change('C') → ref: same origin, change index 1 (0-based).
    assert_occs(flatten(idx.locate("TTTCAAAT")),
                std::set<Occ>({{3, {1}}}),
                "locate TTTCAAAT");

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "===========================================\n";
    std::cout << "OFFSET ARITHMETIC UNIT TESTS\n";
    std::cout << "===========================================\n\n";
    try {
        test_offset_arithmetic();
        std::cout << "\n===========================================\n";
        std::cout << "ALL OFFSET TESTS PASSED\n";
        std::cout << "===========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nFAILED: " << e.what() << "\n";
        return 1;
    }
}
