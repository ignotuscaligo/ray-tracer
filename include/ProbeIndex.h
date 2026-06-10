#pragma once

#include "Vector.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// Phase 2a probe-guided gather: the PROBE INDEX.
//
// A probe is a world-space point the camera can see directly OR through any chain
// of delta (mirror/glass) surfaces — i.e. the FIRST NON-DELTA surface a camera
// ray reaches after being extended through every specular bounce. The probe pass
// (see ProbeGather::collectGatherPoints — the single camera-side specular tracer)
// casts camera rays, extends each through delta surfaces, and records the
// diffuse/glossy point it lands on. The probe positions are the record positions.
//
// The set of probe points is EXACTLY the set of surface locations whose deposited
// radiance can ever reach the camera. During the photon pass a non-delta bounce
// is KEPT (stored raw) only if it lies within gather range of some probe, and
// DISCARDED otherwise (it can never be gathered into any pixel). This bounds the
// raw-bounce storage by visible-surface-area instead of by total photon count,
// which is what makes storing raw, uncompressed bounces affordable and retires
// the quantized density grid.
//
// Spatial structure: a uniform grid keyed on quantized position. The keep query
// `anyWithin(p, r)` only needs a boolean ("is there a probe near p?"), so the
// grid stores, per occupied cell, just whether it holds any probe — the query
// scans the cell neighborhood that could contain a probe within r and does an
// exact squared-distance test against the probe points in those cells.
//
// The index is built once (single-threaded) after the probe pass and is
// READ-ONLY during the photon pass; many worker threads call anyWithin()
// concurrently, which is safe because there are no concurrent writers.
class ProbeIndex
{
public:
    // Build an index over `probes` with a cubic cell edge length `cellSize`
    // (world units). `keepRadius` is the default keep radius used by the
    // single-argument anyWithin() — set it to the gather footprint scale so a
    // bounce is kept iff a probe lies within one gather footprint. cellSize is
    // clamped to a positive value; choosing cellSize == keepRadius makes the keep
    // query touch a 3x3x3 cell neighborhood.
    ProbeIndex(const std::vector<Vector>& probes, double cellSize, double keepRadius);

    // True if any probe lies within Euclidean distance `r` of `p`.
    bool anyWithin(const Vector& p, double r) const;

    // True if any probe lies within the configured keepRadius of `p` (the
    // keep-test used during the photon pass).
    bool anyWithinKeepRadius(const Vector& p) const { return anyWithin(p, m_keepRadius); }

    double cellSize() const noexcept { return m_cellSize; }
    double keepRadius() const noexcept { return m_keepRadius; }
    std::size_t probeCount() const noexcept { return m_probes.size(); }
    std::size_t cellCount() const noexcept { return m_cells.size(); }

private:
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
            std::uint64_t h = static_cast<std::uint64_t>(k.x) * 73856093ULL;
            h ^= static_cast<std::uint64_t>(k.y) * 19349663ULL;
            h ^= static_cast<std::uint64_t>(k.z) * 83492791ULL;
            return static_cast<std::size_t>(h);
        }
    };

    CellKey cellOf(const Vector& p) const noexcept;

    double m_cellSize;
    double m_invCellSize;
    double m_keepRadius;
    std::vector<Vector> m_probes;
    // Cell -> indices into m_probes that fall in that cell.
    std::unordered_map<CellKey, std::vector<std::size_t>, CellKeyHash> m_cells;
};
