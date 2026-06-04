#pragma once

#include "Color.h"
#include "Vector.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

// A single deposited bounce record. This is everything a later GATHER pass
// (Wave 4b) needs to reconstruct the outgoing radiance toward the camera from a
// stored deposit, without re-tracing the photon:
//   - position:  world-space surface point where the photon landed (the deposit
//                location; the hash grid is built over these).
//   - incoming:  the incoming photon's travel direction (into the surface). The
//                gather evaluates the material BRDF against (incoming, toCamera).
//   - power:     the physical magnitude the photon carries at this bounce
//                (post-Wave-2 count-agnostic flux weight, modulated by prior
//                bounces). Stored as Color so it can carry spectral throughput.
//   - normal:    outward surface normal at the deposit (for the BRDF cos term and
//                hemisphere checks at gather time).
//   - material:  index into the MaterialLibrary, so the gather can fetch the BRDF.
//   - time:      photon emission timestamp (for the camera exposure-window gate,
//                Wave 2/3 motion-blur path).
//
// Trivially copyable / standard-layout — the cloud stores these by value in a
// flat preallocated buffer and the gather walks them densely.
struct BounceRecord
{
    Vector position;
    Vector incoming;
    Color power;
    Vector normal;
    std::size_t material = 0;
    float time = 0.0f;
    // Wave 6: bounce depth of the photon at this deposit (0 = direct from the
    // light, 1 = first indirect bounce, ...). Copied from Photon::bounces. Drives
    // the bounce-number debug camera ($bounceFilter): a filtered gather sums only
    // deposits with bounces == N.
    int bounces = 0;
    // Wave 6: index of the light this photon ORIGINATED from (stamped at emission,
    // inherited by every daughter bounce). Drives the per-light debug camera
    // ($lightFilter): a filtered gather sums only deposits with lightId == idx.
    // -1 means "unattributed" (should not occur for light-emitted photons).
    int lightId = -1;
};

// A persistent, thread-safe, append-only store of BounceRecords.
//
// Append mechanism: a single preallocated buffer sized from a budget, plus one
// atomic write cursor. append() does a relaxed fetch_add to claim a slot index;
// if the claimed index is within capacity it writes the record (no lock, no
// contention beyond the single counter) and returns true. If the index is past
// capacity the cloud is full: the record is dropped, an overflow counter is
// bumped, and false is returned. A bounded cloud is acceptable by design — the
// budget cap is reported, not treated as an error.
//
// The buffer is NEVER reallocated after construction, so a writer's pointer into
// it stays valid for the whole photon pass; readers (the hash-grid builder and
// the gather) only run AFTER the pass drains, so there is no reader/writer race.
class BounceCloud
{
public:
    // Construct with a fixed capacity (number of records). The whole buffer is
    // allocated up front; size() worth of it is value-initialized lazily as
    // slots are claimed (we default-construct the vector, which is fine — the
    // records are POD-ish and overwritten on append).
    explicit BounceCloud(std::size_t capacity);

    // Lock-free append. Claims the next slot via an atomic fetch-add. Returns
    // true if the record was stored, false if the budget was exhausted (in which
    // case the record is discarded and the overflow counter is incremented).
    // Safe to call concurrently from any number of worker threads.
    bool append(const BounceRecord& record) noexcept;

    // Number of records actually stored (== min(claimed, capacity)). Only
    // meaningful to read after the photon pass has fully drained.
    std::size_t size() const noexcept;

    std::size_t capacity() const noexcept;

    // True if at least one append was rejected because the budget was hit.
    bool budgetHit() const noexcept;

    // Number of append attempts that were dropped due to the budget cap.
    std::size_t droppedCount() const noexcept;

    // Total bytes the record buffer occupies (capacity * sizeof(BounceRecord)).
    std::size_t memoryBytes() const noexcept;

    // Dense read access for the hash-grid builder and the gather. Valid only
    // after the photon pass drains; indices [0, size()) are populated.
    const BounceRecord& operator[](std::size_t index) const noexcept;
    const BounceRecord* data() const noexcept;

private:
    std::vector<BounceRecord> m_records;
    std::atomic<std::size_t> m_writeCursor{0}; // next slot to claim (may exceed capacity)
    std::atomic<std::size_t> m_dropped{0};
    std::size_t m_capacity;
};
