#include "BounceStore.h"

#include <cmath>

BounceStore::BounceStore(std::size_t capacity)
    : m_records(capacity)
    , m_capacity(capacity)
{
}

bool BounceStore::append(const RawBounce& record) noexcept
{
    const std::size_t slot = m_writeCursor.fetch_add(1, std::memory_order_relaxed);
    if (slot >= m_capacity)
    {
        return false;  // budget exhausted; record dropped (counted via attemptedCount)
    }
    m_records[slot] = record;
    return true;
}

std::size_t BounceStore::size() const noexcept
{
    const std::size_t claimed = m_writeCursor.load();
    return claimed < m_capacity ? claimed : m_capacity;
}

std::size_t BounceStore::memoryBytes() const noexcept
{
    return m_capacity * sizeof(RawBounce);
}

BounceStore::CellKey BounceStore::cellOf(const Vector& p) const noexcept
{
    return CellKey{
        static_cast<std::int64_t>(std::floor(p.x * m_invCellSize)),
        static_cast<std::int64_t>(std::floor(p.y * m_invCellSize)),
        static_cast<std::int64_t>(std::floor(p.z * m_invCellSize)),
    };
}

void BounceStore::buildIndex(double cellSize)
{
    m_cellSize = cellSize > 0.0 ? cellSize : 1.0;
    m_invCellSize = 1.0 / m_cellSize;
    m_cells.clear();

    const std::size_t count = size();
    for (std::size_t i = 0; i < count; ++i)
    {
        m_cells[cellOf(m_records[i].position)].push_back(i);
    }
}

std::vector<std::size_t> BounceStore::radiusSearch(const Vector& p, double r) const
{
    std::vector<std::size_t> result;
    if (r <= 0.0 || m_cells.empty())
    {
        return result;
    }

    const double r2 = r * r;
    const CellKey center = cellOf(p);
    const std::int64_t reach =
        static_cast<std::int64_t>(std::ceil(r * m_invCellSize));

    for (std::int64_t dz = -reach; dz <= reach; ++dz)
    {
        for (std::int64_t dy = -reach; dy <= reach; ++dy)
        {
            for (std::int64_t dx = -reach; dx <= reach; ++dx)
            {
                const CellKey key{center.x + dx, center.y + dy, center.z + dz};
                const auto it = m_cells.find(key);
                if (it == m_cells.end())
                {
                    continue;
                }
                for (const std::size_t index : it->second)
                {
                    const Vector& pos = m_records[index].position;
                    const double ddx = pos.x - p.x;
                    const double ddy = pos.y - p.y;
                    const double ddz = pos.z - p.z;
                    if (ddx * ddx + ddy * ddy + ddz * ddz <= r2)
                    {
                        result.push_back(index);
                    }
                }
            }
        }
    }
    return result;
}
