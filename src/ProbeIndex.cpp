#include "ProbeIndex.h"

#include <cmath>

ProbeIndex::ProbeIndex(const std::vector<Vector>& probes, double cellSize, double keepRadius)
    : m_cellSize(cellSize > 0.0 ? cellSize : 1.0)
    , m_invCellSize(1.0 / (cellSize > 0.0 ? cellSize : 1.0))
    , m_keepRadius(keepRadius > 0.0 ? keepRadius : 0.0)
    , m_probes(probes)
{
    m_cells.reserve(m_probes.size());
    for (std::size_t i = 0; i < m_probes.size(); ++i)
    {
        m_cells[cellOf(m_probes[i])].push_back(i);
    }
}

ProbeIndex::CellKey ProbeIndex::cellOf(const Vector& p) const noexcept
{
    return CellKey{
        static_cast<std::int64_t>(std::floor(p.x * m_invCellSize)),
        static_cast<std::int64_t>(std::floor(p.y * m_invCellSize)),
        static_cast<std::int64_t>(std::floor(p.z * m_invCellSize)),
    };
}

bool ProbeIndex::anyWithin(const Vector& p, double r) const
{
    if (r <= 0.0 || m_probes.empty())
    {
        return false;
    }

    const double r2 = r * r;
    // Cell extent that could contain a probe within r of p: the cell of p,
    // expanded by ceil(r / cellSize) in each axis.
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
                    const Vector& probe = m_probes[index];
                    const double ddx = probe.x - p.x;
                    const double ddy = probe.y - p.y;
                    const double ddz = probe.z - p.z;
                    if (ddx * ddx + ddy * ddy + ddz * ddz <= r2)
                    {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}
