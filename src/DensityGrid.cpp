#include "DensityGrid.h"

#include <cmath>

DensityGrid::DensityGrid(double cellSize)
    : m_cellSize(cellSize > 0.0 ? cellSize : 1.0)
    , m_invCellSize(1.0 / (cellSize > 0.0 ? cellSize : 1.0))
{
}

DensityGrid::CellKey DensityGrid::cellOf(const Vector& p) const noexcept
{
    return CellKey{
        static_cast<std::int64_t>(std::floor(p.x * m_invCellSize)),
        static_cast<std::int64_t>(std::floor(p.y * m_invCellSize)),
        static_cast<std::int64_t>(std::floor(p.z * m_invCellSize)),
    };
}

std::size_t DensityGrid::shardOf(const CellKey& key) const noexcept
{
    return CellKeyHash{}(key) & (kShardCount - 1);
}

void DensityGrid::add(const Vector& position, const Color& power)
{
    const CellKey key = cellOf(position);
    Shard& shard = m_shards[shardOf(key)];

    std::lock_guard<std::mutex> lock(shard.mutex);
    Cell& cell = shard.cells[key];
    cell.power += power;
    cell.count += 1;
}

DensityGrid::Cell DensityGrid::lookupCell(const Vector& position) const
{
    const CellKey key = cellOf(position);
    const Shard& shard = m_shards[shardOf(key)];

    std::lock_guard<std::mutex> lock(shard.mutex);
    const auto it = shard.cells.find(key);
    if (it == shard.cells.end())
    {
        return Cell{};
    }
    return it->second;
}

Color DensityGrid::lookupIrradiance(const Vector& position, double photonsPerLight) const
{
    const Cell cell = lookupCell(position);
    if (cell.count == 0)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }

    const double invN = (photonsPerLight > 0.0) ? (1.0 / photonsPerLight) : 0.0;
    const double cellArea = m_cellSize * m_cellSize;
    if (cellArea <= 0.0)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }

    const float scale = static_cast<float>(invN / cellArea);
    return cell.power * scale;
}

std::size_t DensityGrid::cellCount() const noexcept
{
    std::size_t total = 0;
    for (const Shard& shard : m_shards)
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        total += shard.cells.size();
    }
    return total;
}

std::uint64_t DensityGrid::depositCount() const noexcept
{
    std::uint64_t total = 0;
    for (const Shard& shard : m_shards)
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        for (const auto& entry : shard.cells)
        {
            total += entry.second.count;
        }
    }
    return total;
}

std::size_t DensityGrid::memoryBytes() const noexcept
{
    // Per occupied cell: the map node holds a CellKey (24 B) + Cell (Color 12 B +
    // count 8 B = 20 B, padded to 24) plus typical unordered_map node bookkeeping
    // (a next pointer + cached hash, ~16 B). Round to 64 B/cell as a conservative
    // resident estimate, then add the bucket arrays (~1 pointer per bucket, and
    // libstdc++/libc++ keep roughly one bucket per element).
    const std::size_t cells = cellCount();
    constexpr std::size_t kBytesPerCell = 64;
    constexpr std::size_t kBytesPerBucket = sizeof(void*);
    return cells * (kBytesPerCell + kBytesPerBucket);
}
