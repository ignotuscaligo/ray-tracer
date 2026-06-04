#pragma once

#include "BounceCloud.h"
#include "Vector.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// Uniform spatial hash grid over the points deposited in a BounceCloud.
//
// Build: after the photon pass fully drains, quantize each record's deposit
// position to an integer cell coordinate (floor(p / cellSize)) and bucket the
// record's index under a hash of that cell. Build is single-pass and reads the
// cloud densely; it does not copy the records, only their indices.
//
// Query: radiusSearch(p, r) returns the indices of all deposits within Euclidean
// distance r of p. It visits every cell whose extent can possibly contain a
// point within r (the cell of p expanded by ceil(r / cellSize) in each axis),
// collects the candidate indices, and filters each by the exact squared
// distance — so it returns EXACTLY the points within r, with no false positives.
//
// cellSize defaults to the expected gather radius: a query of radius == cellSize
// then touches a 3x3x3 neighborhood, which is the standard near-neighbor density
// trade-off (cells much smaller than the query radius inflate the cell count;
// much larger cells inflate the per-cell candidate scan).
class HashGrid
{
public:
    // Build a grid over the populated prefix [0, cloud.size()) of `cloud`, using
    // cubic cells of edge length `cellSize` (world units). cellSize must be > 0.
    HashGrid(const BounceCloud& cloud, double cellSize);

    // Return the indices into the cloud of all records within radius `r` of `p`.
    // Exactly the deposits with |record.position - p| <= r are returned.
    std::vector<std::size_t> radiusSearch(const Vector& p, double r) const;

    double cellSize() const noexcept { return m_cellSize; }
    std::size_t cellCount() const noexcept { return m_cells.size(); }
    std::size_t pointCount() const noexcept { return m_pointCount; }

private:
    // Integer cell coordinate.
    struct CellKey
    {
        std::int64_t x;
        std::int64_t y;
        std::int64_t z;

        bool operator==(const CellKey& other) const noexcept
        {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct CellKeyHash
    {
        std::size_t operator()(const CellKey& k) const noexcept
        {
            // Mix three 64-bit lanes. Large odd primes spread adjacent cells
            // across the table so neighboring cells don't collide into one
            // bucket (which would defeat the spatial locality of the grid).
            std::uint64_t h = static_cast<std::uint64_t>(k.x) * 73856093ULL;
            h ^= static_cast<std::uint64_t>(k.y) * 19349663ULL;
            h ^= static_cast<std::uint64_t>(k.z) * 83492791ULL;
            return static_cast<std::size_t>(h);
        }
    };

    CellKey cellOf(const Vector& p) const noexcept;

    const BounceCloud& m_cloud;
    double m_cellSize;
    double m_invCellSize;
    std::size_t m_pointCount = 0;
    std::unordered_map<CellKey, std::vector<std::size_t>, CellKeyHash> m_cells;
};
