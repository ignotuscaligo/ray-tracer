#pragma once

#include "Color.h"
#include "Vector.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

// Storage pivot (replaces BounceCloud + HashGrid for the reflection path).
//
// A QUANTIZED DENSITY GRID. Instead of storing one BounceRecord per non-delta
// photon bounce (90M records, ~15.6 GiB for a full MirrorTest), each non-delta
// bounce's energy is accumulated into the spatial GRID CELL it lands in
// (key = floor(position / cellSize)). The photon is then discarded. Storage is
// bounded by the number of OCCUPIED CELLS, not by the photon count: a bright
// spot where millions of photons land collapses into a single cell's running
// sum, and empty regions of space cost nothing.
//
// Per-cell data (v1): a Lambertian surface's outgoing radiance is
// view-independent, so an IRRADIANCE accumulator per cell is sufficient. We
// store the running sum of deposited photon power (Color) and the deposit count.
// At lookup we normalize:
//
//   irradiance(cell) = (1 / N) * sumPower / cellArea
//
// where N = photonsPerLight (the single count normalization the photon mapper
// applies once) and cellArea = cellSize^2 (the world-space area one cell face
// projects onto a surface — the same role the per-pixel pi*r^2 footprint played
// in the old gather). A diffuse surface's reflected radiance toward any viewer
// is then  albedo/pi * irradiance, which the mirror gather computes by
// multiplying the cell irradiance by the reflected surface's BRDF.
//
// Concurrency: workers deposit from many threads during the photon pass. The
// grid is sharded into a fixed number of independently-locked buckets keyed by a
// hash of the cell coordinate, so concurrent add()s to different cells (the
// common case) contend only when they hash to the same shard. Each shard is a
// small std::unordered_map guarded by its own mutex. This keeps memory bounded
// by occupied cells while remaining race-free without a global lock. lookup() is
// only called after the photon pass drains (read-only), so it takes the same
// shard lock purely for well-defined access; there is no reader/writer race in
// practice.
class DensityGrid
{
public:
    // Per-cell accumulator. Running sum of deposited photon power and the number
    // of deposits that landed in the cell.
    struct Cell
    {
        Color power{0.0f, 0.0f, 0.0f};
        std::uint64_t count = 0;
    };

    // Construct with a cubic cell edge length (world units). cellSize must be > 0;
    // a coarser cell uses less memory and yields blurrier reflections, a finer
    // cell uses more memory and yields sharper reflections.
    explicit DensityGrid(double cellSize);

    // Accumulate `power` (a deposited photon's carried energy) into the cell that
    // contains `position`. Thread-safe; callable concurrently from any number of
    // worker threads. Increments the cell's deposit count.
    void add(const Vector& position, const Color& power);

    // Look up the cell containing `position` and return its IRRADIANCE estimate:
    //   (1 / photonsPerLight) * sumPower / (cellSize * cellSize).
    // Returns black for an empty / never-deposited cell. `photonsPerLight` is the
    // photon-count normalization N. Read-only; valid after the photon pass drains.
    Color lookupIrradiance(const Vector& position, double photonsPerLight) const;

    // Raw accumulated power summed in the cell containing `position` (no
    // normalization). Black if the cell is empty. Exposed for tests.
    Cell lookupCell(const Vector& position) const;

    double cellSize() const noexcept { return m_cellSize; }

    // Number of occupied (non-empty) cells across all shards. This is the storage
    // footprint driver — the headline memory metric.
    std::size_t cellCount() const noexcept;

    // Total deposits accumulated across all cells (sum of per-cell counts). Useful
    // to confirm the grid saw the same deposit volume the old cloud would have.
    std::uint64_t depositCount() const noexcept;

    // Approximate resident bytes: occupied cells * per-cell map-node cost plus the
    // shard table overhead. An estimate for the memory-win report, not an exact
    // allocator measurement.
    std::size_t memoryBytes() const noexcept;

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

    // Number of independently-locked shards. A power of two so the shard index is
    // a cheap mask of the cell hash. 64 keeps cross-cell contention low at the
    // worker counts in use (<= 32) without a large fixed overhead.
    static constexpr std::size_t kShardCount = 64;

    CellKey cellOf(const Vector& p) const noexcept;
    std::size_t shardOf(const CellKey& key) const noexcept;

    struct Shard
    {
        mutable std::mutex mutex;
        std::unordered_map<CellKey, Cell, CellKeyHash> cells;
    };

    double m_cellSize;
    double m_invCellSize;
    // unique_ptr-free fixed array of shards. std::mutex is not movable, so the
    // shards live in a vector sized once at construction and never resized.
    std::vector<Shard> m_shards{kShardCount};
};
