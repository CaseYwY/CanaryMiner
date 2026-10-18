// Independent CTA ordering contract; dispatch admission is separate.
// Logical tiles retain their coordinates; only their launch order changes.
#pragma once
#include <cstdint>
#if defined(__CUDACC__)
#define PEARL_TRAVERSAL_HD __host__ __device__
#else
#define PEARL_TRAVERSAL_HD
#endif
namespace om::pearl::cta_traversal {
struct Tile { uint32_t row = 0, column = 0; };

// Visit a row-band of small 2D regions, column-first within each region.
// This is not group-M ordering split into smaller identical subsequences:
// columns advance first inside each region, then rows, then the next region.
template<unsigned GroupRows, unsigned GroupColumns>
PEARL_TRAVERSAL_HD constexpr bool supertile(uint32_t ordinal, uint32_t rows,
                                          uint32_t columns, Tile* out) {
    static_assert(GroupRows > 0 && GroupRows <= 64);
    static_assert(GroupColumns > 0 && GroupColumns <= 64);
    if (!out || !rows || !columns || uint64_t(ordinal) >= uint64_t(rows) * columns)
        return false;
    const uint64_t bandSize = uint64_t(GroupRows) * columns;
    const uint32_t firstRow = uint32_t((ordinal / bandSize) * GroupRows);
    const uint32_t height = rows - firstRow < GroupRows ? rows - firstRow : GroupRows;
    const uint64_t inBand = ordinal % bandSize;
    const uint32_t firstColumn = uint32_t(inBand / (uint64_t(height) * GroupColumns) * GroupColumns);
    const uint32_t width = columns - firstColumn < GroupColumns ? columns - firstColumn : GroupColumns;
    const uint64_t inside = inBand % (uint64_t(height) * GroupColumns);
    *out = Tile{firstRow + uint32_t(inside / width), firstColumn + uint32_t(inside % width)};
    return true;
}
} // namespace om::pearl::cta_traversal
#undef PEARL_TRAVERSAL_HD
