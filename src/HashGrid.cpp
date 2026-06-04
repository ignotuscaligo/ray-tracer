#include "HashGrid.h"

#include <cmath>

HashGrid::HashGrid(const BounceCloud& cloud, double cellSize)
    : m_cloud(cloud)
    , m_cellSize(cellSize > 0.0 ? cellSize : 1.0)
    , m_invCellSize(1.0 / (cellSize > 0.0 ? cellSize : 1.0))
{
    const std::size_t n = cloud.size();
    m_pointCount = n;

    // Single pass: bucket each record index under its cell key. reserve a
    // generous bucket count to limit rehashing on dense clouds.
    m_cells.reserve(n / 4 + 1);

    for (std::size_t i = 0; i < n; ++i)
    {
        const CellKey key = cellOf(cloud[i].position);
        m_cells[key].push_back(i);
    }
}

HashGrid::CellKey HashGrid::cellOf(const Vector& p) const noexcept
{
    return CellKey{
        static_cast<std::int64_t>(std::floor(p.x * m_invCellSize)),
        static_cast<std::int64_t>(std::floor(p.y * m_invCellSize)),
        static_cast<std::int64_t>(std::floor(p.z * m_invCellSize)),
    };
}

std::vector<std::size_t> HashGrid::radiusSearch(const Vector& p, double r) const
{
    std::vector<std::size_t> result;

    if (r < 0.0)
    {
        return result;
    }

    const double r2 = r * r;

    // The query sphere of radius r around p, centered in cell (cx,cy,cz), can
    // only intersect cells within ceil(r / cellSize) steps along each axis. The
    // center cell itself is always included (reach >= 0), so a query with
    // r <= cellSize touches a 3x3x3 neighborhood.
    const CellKey center = cellOf(p);
    const std::int64_t reach = static_cast<std::int64_t>(std::ceil(r * m_invCellSize));

    for (std::int64_t dz = -reach; dz <= reach; ++dz)
    {
        for (std::int64_t dy = -reach; dy <= reach; ++dy)
        {
            for (std::int64_t dx = -reach; dx <= reach; ++dx)
            {
                const CellKey key{center.x + dx, center.y + dy, center.z + dz};
                auto it = m_cells.find(key);
                if (it == m_cells.end())
                {
                    continue;
                }

                for (std::size_t index : it->second)
                {
                    const Vector& q = m_cloud[index].position;
                    const double ddx = q.x - p.x;
                    const double ddy = q.y - p.y;
                    const double ddz = q.z - p.z;
                    const double dist2 = ddx * ddx + ddy * ddy + ddz * ddz;
                    if (dist2 <= r2)
                    {
                        result.push_back(index);
                    }
                }
            }
        }
    }

    return result;
}
