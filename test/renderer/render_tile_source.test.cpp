#include <mbgl/test/util.hpp>

#include <mbgl/renderer/sources/render_tile_source.hpp>

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

using namespace mbgl;

namespace {

TileSortKey keyFor(const CanonicalTileID& canonical, float bearing) {
    return computeTileSortKey(UnwrappedTileID(0, canonical), bearing);
}

} // namespace

// Primary key: z descending — a z=9 tile sorts before a z=8 tile regardless of
// the rotated positions.
TEST(RenderTileSource, SortKeyZDescending) {
    const auto high = keyFor(CanonicalTileID(9, 3, 3), 33.7f);
    const auto low = keyFor(CanonicalTileID(8, 4095, 4095), 33.7f);
    EXPECT_TRUE(high < low);
    EXPECT_FALSE(low < high);
}

// Same z, bearing=0 (identity rotation): rotated position equals canonical
// position, so higher rotY (then higher rotX on equal rotY) sorts first.
TEST(RenderTileSource, SortKeyRotatedPositionDescending) {
    const auto upper = keyFor(CanonicalTileID(8, 3, 5), 0.0f);
    const auto lower = keyFor(CanonicalTileID(8, 3, 4), 0.0f);
    EXPECT_TRUE(upper < lower);

    const auto right = keyFor(CanonicalTileID(8, 3, 4), 0.0f);
    const auto left = keyFor(CanonicalTileID(8, 2, 4), 0.0f);
    EXPECT_TRUE(right < left);
}

// Equal (z, rotY, rotX): float(canonical.x) rounds both 2^24 and 2^24+1 to the
// same float at z=30, so the rotated keys collide and the integer tie-break
// must order the tiles deterministically, cx ascending.
TEST(RenderTileSource, SortKeyTieBreakCanonicalXAscending) {
    const auto lesser = keyFor(CanonicalTileID(30, 1u << 24, 5), 0.0f);
    const auto greater = keyFor(CanonicalTileID(30, (1u << 24) + 1, 5), 0.0f);
    EXPECT_EQ(lesser.rotX, greater.rotX);
    EXPECT_EQ(lesser.rotY, greater.rotY);
    EXPECT_TRUE(lesser < greater);
    EXPECT_FALSE(greater < lesser);
}

// Total-order sanity: sorting a shuffled copy of >=100 keys is deterministic
// and matches a reference sort (permutation invariance over 3 shuffles).
TEST(RenderTileSource, SortKeyTotalOrderDeterministic) {
    std::mt19937 rng(20261125u);
    std::uniform_int_distribution<int> zDist(0, 14);

    const float bearing = 33.7f;
    std::vector<TileSortKey> reference;
    reference.reserve(137);
    for (int i = 0; i < 137; ++i) {
        const auto z = static_cast<uint8_t>(zDist(rng));
        std::uniform_int_distribution<uint32_t> coordDist(0, (1u << z) - 1);
        reference.push_back(keyFor(CanonicalTileID(z, coordDist(rng), coordDist(rng)), bearing));
    }
    std::sort(reference.begin(), reference.end());
    EXPECT_TRUE(std::is_sorted(reference.begin(), reference.end()));

    for (int round = 0; round < 3; ++round) {
        auto shuffled = reference;
        std::shuffle(shuffled.begin(), shuffled.end(), rng);
        std::sort(shuffled.begin(), shuffled.end());
        EXPECT_TRUE(std::is_sorted(shuffled.begin(), shuffled.end()));
        ASSERT_EQ(reference.size(), shuffled.size());
        for (size_t j = 0; j < reference.size(); ++j) {
            EXPECT_FALSE(reference[j] < shuffled[j]);
            EXPECT_FALSE(shuffled[j] < reference[j]);
        }
    }
}
