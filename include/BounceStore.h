#pragma once

#include "Color.h"
#include "Vector.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// Phase 2a probe-guided gather: the RAW BOUNCE STORE.
//
// During the photon pass, every non-delta bounce that survives the probe
// KEEP-TEST (it lies within gather range of some camera-visible probe — see
// ProbeIndex) is appended here RAW: its world position, the incoming photon
// travel direction, and the photon's carried power. No quantization, no spatial
// compression — this is the data the old per-photon bounce cloud held, but the
// keep-test bounds it to visible-surface-area instead of total photon count.
//
// The unified gather (ProbeGather) reads these back: a camera ray extends through
// delta surfaces to its first non-delta hit, then sums BRDF(incoming, toCamera) *
// power over the raw bounces within the pixel footprint and divides by the
// footprint area. This single path renders BOTH directly-visible diffuse
// (extension depth 0) and reflected/refracted diffuse (extension depth > 0) at
// identical fidelity, retiring the separate splat + density-grid mechanisms.
//
// Append: a single preallocated buffer plus one atomic write cursor. append()
// does a relaxed fetch_add to claim a slot; within capacity it writes the record
// lock-free, past capacity it drops and bumps an overflow counter. The buffer is
// never reallocated, so reader access (the grid build + the gather) is valid once
// the photon pass drains.
// Compact deposit record (36 B, not 80). `position`/`incoming` are stored as 3
// floats each rather than `Vector` (32 B, AVX-padded) because the store holds
// MILLIONS of these and the gather only needs single-precision positions for the
// radius search and BRDF evaluation. The accessors return `Vector` so call sites
// stay typed. Trivially copyable for the lock-free append.
struct RawBounce
{
    float px = 0.0f, py = 0.0f, pz = 0.0f;  // world-space bounce position
    float ix = 0.0f, iy = 0.0f, iz = 0.0f;  // incoming photon travel direction
    Color power{0.0f, 0.0f, 0.0f};          // photon's carried power at this bounce

    RawBounce() = default;
    RawBounce(const Vector& position, const Vector& incoming, const Color& pow)
        : px(static_cast<float>(position.x))
        , py(static_cast<float>(position.y))
        , pz(static_cast<float>(position.z))
        , ix(static_cast<float>(incoming.x))
        , iy(static_cast<float>(incoming.y))
        , iz(static_cast<float>(incoming.z))
        , power(pow)
    {
    }

    Vector position() const noexcept { return Vector{px, py, pz}; }
    Vector incoming() const noexcept { return Vector{ix, iy, iz}; }
};

class BounceStore
{
public:
    // Construct with a fixed slot capacity. The whole buffer is allocated up
    // front (default-constructed; records are overwritten on append).
    explicit BounceStore(std::size_t capacity);

    // Lock-free append. Claims the next slot via an atomic fetch-add. Returns
    // true if stored, false if the budget was exhausted (record discarded,
    // overflow counter bumped). Safe to call concurrently from worker threads.
    bool append(const RawBounce& record) noexcept;

    // Number of records actually stored (== min(claimed, capacity)). Read only
    // after the photon pass drains.
    std::size_t size() const noexcept;
    std::size_t capacity() const noexcept { return m_capacity; }

    // Total append attempts (claimed slots, including those past capacity). The
    // difference (attempts - size) is the bounces dropped by the budget; the
    // difference between this and the scene's total non-delta bounce count is the
    // bounces culled by the probe keep-test (the memory-bound evidence).
    std::uint64_t attemptedCount() const noexcept { return m_writeCursor.load(); }
    bool budgetHit() const noexcept { return m_writeCursor.load() > m_capacity; }

    std::size_t memoryBytes() const noexcept;

    const RawBounce& operator[](std::size_t index) const noexcept { return m_records[index]; }

    // ===== Spatial index (built post-pass, single-threaded) =====
    //
    // Build a uniform grid over the populated prefix [0, size()) using cubic
    // cells of edge `cellSize`. Must be called after the photon pass drains and
    // before any gather query.
    void buildIndex(double cellSize);

    // Indices into the store of all bounces within radius r of p. Exactly the
    // records with |record.position - p| <= r. Requires buildIndex() first.
    std::vector<std::size_t> radiusSearch(const Vector& p, double r) const;

    double cellSize() const noexcept { return m_cellSize; }

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

    std::vector<RawBounce> m_records;
    std::atomic<std::size_t> m_writeCursor{0};
    std::size_t m_capacity;

    double m_cellSize = 1.0;
    double m_invCellSize = 1.0;
    std::unordered_map<CellKey, std::vector<std::size_t>, CellKeyHash> m_cells;
};
