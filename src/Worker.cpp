#include "Worker.h"

#include "AnimationQuery.h"
#include "Color.h"
#include "Light.h"
#include "Pixel.h"
#include "Utility.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <optional>
#include <thread>

namespace
{

constexpr double selfHitThreshold = std::numeric_limits<double>::epsilon();

std::atomic<size_t> g_deltaHitsTotal{0};
std::atomic<size_t> g_deltaHitsAccepted{0};
std::atomic<size_t> g_deltaHitsRejectedBackface{0};
std::atomic<size_t> g_deltaHitsRejectedConeOffset{0};

// Drop counters. Every site where the forward photon pipeline discards work
// because a destination WorkQueue was full increments one of these by the
// number of items dropped. They prove the lossy-drop bug exists (Step A) and
// must read zero once claim-output-first back-pressure lands (Step B).
//   - g_droppedEmitting: bounce-hits dropped at the emitting-queue producer
//     in processPhotons (raycast path) and any short-alloc there.
//   - g_droppedRequeue: emitting hits dropped when processEmissions could not
//     re-enqueue hits it declined to service this iteration.
//   - g_droppedHit: bounce-hits silently truncated at the hitQueue producer
//     in processPhotons when the returned block was shorter than requested.
std::atomic<size_t> g_droppedEmitting{0};
std::atomic<size_t> g_droppedRequeue{0};
std::atomic<size_t> g_droppedHit{0};
// g_droppedFinal: valid camera-visible hits dropped at the finalHitQueue
// producer in processHits when its returned block was short (finalHitQueue is
// the smallest queue, so it saturates too). These never reach the splat sink.
std::atomic<size_t> g_droppedFinal{0};

}

namespace WorkerDebug
{
size_t deltaHitsTotal() { return g_deltaHitsTotal.load(); }
size_t deltaHitsAccepted() { return g_deltaHitsAccepted.load(); }
size_t deltaHitsRejectedBackface() { return g_deltaHitsRejectedBackface.load(); }
size_t deltaHitsRejectedConeOffset() { return g_deltaHitsRejectedConeOffset.load(); }
void resetDeltaHitCounters()
{
    g_deltaHitsTotal.store(0);
    g_deltaHitsAccepted.store(0);
    g_deltaHitsRejectedBackface.store(0);
    g_deltaHitsRejectedConeOffset.store(0);
}

size_t droppedEmitting() { return g_droppedEmitting.load(); }
size_t droppedRequeue() { return g_droppedRequeue.load(); }
size_t droppedHit() { return g_droppedHit.load(); }
size_t droppedFinal() { return g_droppedFinal.load(); }
size_t droppedTotal()
{
    return g_droppedEmitting.load() + g_droppedRequeue.load()
         + g_droppedHit.load() + g_droppedFinal.load();
}
void resetDropCounters()
{
    g_droppedEmitting.store(0);
    g_droppedRequeue.store(0);
    g_droppedHit.store(0);
    g_droppedFinal.store(0);
}
}

Worker::Worker(size_t index, size_t fetchSize)
    : m_index(index)
    , m_fetchSize(fetchSize)
    , m_running(false)
    , m_suspend(false)
{
}

void Worker::start()
{
    if (m_running)
    {
        return;
    }

    m_running = true;
    m_suspend = false;

    m_thread = std::thread([this]() {
        exec();
    });
}

void Worker::suspend()
{
    m_suspend = true;
}

void Worker::resume()
{
    m_suspend = false;
}

void Worker::stop()
{
    m_running = false;
    m_thread.join();
}

void Worker::exec()
{
    try
    {
        if (!photonQueue || !hitQueue || !finalHitQueue || !emitterQueue || !image || !camera || !buffer || !materialLibrary || !lightQueue)
        {
            std::cout << m_index << ": ABORT: missing required references!" << std::endl;
            m_running = false;
        }

        while (m_running)
        {
            if (m_suspend)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            bool drainedAnything = false;

            // Gate light emission on headroom in the PhotonQueue. With the dual-source
            // pipeline below, fan-out is back-pressured via the EmitterQueue rather than
            // happening in-line at bounce time, so we no longer need the 64x worst-case
            // headroom that the old immediate-spawn loop required.
            if (lightQueue->remainingPhotons() > 0 && photonQueue->freeSpace() > m_fetchSize * 2)
            {
                if (!processLights())
                {
                    break;
                }
                drainedAnything = true;
            }

            // Stage 1: raycast pending photons. Wave 4b: a batch of fetchSize
            // photons produces bounce-hits that are DEPOSITED into the cloud
            // (gather source) and, for bounceable hits, one compact Emitter each
            // to the EmitterQueue (lazy daughter path). The forward-splat hitQueue
            // push is removed. processPhotons flushes any emitter overflow from a
            // prior iteration and only fetches new source photons when the emitter
            // overflow is empty — so nothing is dropped, only deferred.
            const bool photonOverflowPending = !m_emitterOverflow.empty();
            if (photonQueue->available() > 0 || photonOverflowPending)
            {
                if (!processPhotons())
                {
                    break;
                }
                drainedAnything = true;
            }

            // Stage 2: LAZY daughter fan-out. Pull compact emitters from the
            // EmitterQueue, RESERVE photon-queue space first (claim-output-first),
            // and generate only as many daughters as fit into the reservation.
            // An emitter with daughters still remaining goes back in the queue;
            // when its count hits zero it is done. No emitter is dropped — if no
            // space can be reserved this round it is left in / returned to the
            // queue for a later iteration (lossless).
            if (emitterQueue->available() > 0 || !m_emitterOverflow.empty())
            {
                if (!processEmissions())
                {
                    break;
                }
                drainedAnything = true;
            }

            // Wave 4b: Stage 3 (camera-visibility check, processHits) and the
            // splat sink (processFinalHits) are REMOVED. The forward splat that
            // turned the camera into an accumulator is gone; the image is produced
            // after the pass by the gather over the deposit cloud. Only the
            // photon pass (emit -> raycast+deposit -> lazy daughter fan-out) runs
            // here now.

            if (!drainedAnything)
            {
                std::this_thread::yield();
            }
        }
    }
    catch (const std::exception& e)
    {
        m_exception = std::current_exception();
        m_running = false;
    }
}

std::exception_ptr Worker::exception()
{
    return m_exception;
}

void Worker::setBounceThreshold(size_t bounceThreshold)
{
    m_bounceThreshold = bounceThreshold;
}

void Worker::syncOverflowGauge()
{
    m_pendingOverflowGauge.store(
        m_emitterOverflow.size(),
        std::memory_order_relaxed);
}

size_t Worker::pendingOverflow() const
{
    return m_pendingOverflowGauge.load(std::memory_order_relaxed);
}

bool Worker::processLights()
{
    auto workStart = std::chrono::system_clock::now();

    for (auto& object : objects)
    {
        if (!object->hasType<Light>())
        {
            continue;
        }

        size_t photonCount = lightQueue->fetchPhotons(object->name(), m_fetchSize);

        if (photonCount == 0)
        {
            continue;
        }

        double photonFlux = lightQueue->getPhotonFlux(object->name());

        auto photons = photonQueue->initialize(photonCount);

        std::static_pointer_cast<Light>(object)->emit(photons, photonFlux, m_generator);

        // Stamp each freshly-emitted photon with a random time within the camera's
        // global exposure window (vision doc pillar 2 — "Photons with attached emission
        // timestamp"). The animation query is then consulted per-photon at this time
        // when raycasting, which is what makes motion blur fall out naturally without
        // any temporal supersampling.
        //
        // If the window is infinite (default), all photons get time=0 — preserving
        // the pre-motion-blur baseline. Only when the camera has a bounded window do we
        // sample real per-photon times.
        const Camera::ExposureWindow window = camera->globalExposureWindow();
        if (std::isfinite(window.start) && std::isfinite(window.end) && window.end > window.start)
        {
            const float span = window.end - window.start;
            for (auto& photon : photons)
            {
                photon.time = window.start + static_cast<float>(m_generator.value(span));
            }
        }

        emitProcessed += photons.size();

        photonQueue->ready(photons);

        break;
    }

    auto workEnd = std::chrono::system_clock::now();
    auto workDuration = std::chrono::duration_cast<std::chrono::microseconds>(workEnd - workStart);
    emitDuration += workDuration.count();

    return true;
}

bool Worker::processPhotons()
{
    auto workStart = std::chrono::system_clock::now();

    // Claim-output-first: before consuming any new source photons, drain any
    // emitters parked from a previous iteration into the emitter queue. If the
    // overflow can't fully drain, do NOT fetch new photons this round — that
    // would grow the overflow without bound. Leave the source photons in the
    // photonQueue (we simply don't fetch) and let the worker run other stages,
    // which frees downstream room. This is what makes the stage lossless.
    // (Wave 4b: the hit-overflow / splat path is gone; only the emitter daughter
    // path remains.)
    if (!m_emitterOverflow.empty())
    {
        flushIntoQueue(m_emitterOverflow, *emitterQueue);
        syncOverflowGauge();

        auto workEnd = std::chrono::system_clock::now();
        photonDuration += std::chrono::duration_cast<std::chrono::microseconds>(workEnd - workStart).count();
        return true;
    }

    auto photonsBlock = photonQueue->fetch(m_fetchSize);

    m_hitBuffer.clear();

    for (auto& photon : photonsBlock)
    {
        // Skip photons with no brightness left, will drop them from the queue
        if (photon.color.brightness() < std::numeric_limits<double>::epsilon())
        {
            continue;
        }

        m_volumeHitBuffer.clear();

        for (auto& object : objects)
        {
            if (!object->hasType<Volume>())
            {
                continue;
            }

            std::optional<Hit> hit = std::static_pointer_cast<Volume>(object)->castRayAt(
                photon.ray, m_castBuffer, photon.time, animationQuery.get());

            if (hit)
            {
                m_volumeHitBuffer.push_back({photon, *hit});
            }
        }

        if (!m_volumeHitBuffer.empty())
        {
            double minDistance = std::numeric_limits<double>::max();
            size_t minIndex = 0;
            bool validHit = false;

            for (int i = 0; i < m_volumeHitBuffer.size(); ++i)
            {
                if (m_volumeHitBuffer[i].hit.distance < minDistance && m_volumeHitBuffer[i].hit.distance > selfHitThreshold)
                {
                    validHit = true;
                    minIndex = i;
                    minDistance = m_volumeHitBuffer[i].hit.distance;
                }
            }

            if (validHit)
            {
                m_hitBuffer.push_back(m_volumeHitBuffer[minIndex]);
            }
        }
    }

    if (!m_hitBuffer.empty())
    {
        // Wave 4b: the forward splat is REMOVED. Bounce-hits are no longer pushed
        // to the hitQueue (the splat / camera-as-accumulator path is gone). The
        // image is produced after the pass by the gather over the cloud. Each
        // non-delta hit is DEPOSITED into the cloud, and bounceable hits still
        // feed the EmitterQueue (daughter path) so multi-bounce light transport
        // continues to fill the cloud.
        for (auto& photonHit : m_hitBuffer)
        {
            std::shared_ptr<Material> material = materialLibrary->fetchByIndex(photonHit.hit.material);

            // Wave 4a deposit: append a BounceRecord for every hit on a NON-DELTA
            // surface (Lambertian diffuse + glossy Microfacet). Pure mirrors /
            // delta materials are excluded — those become the ray-extension case
            // in Wave 4c, not a stored deposit. Deposit ALL non-delta hits, not
            // just bounceable ones: a terminal non-delta hit (at the bounce
            // threshold) is still a valid sample the gather will query.
            // The append is a lock-free atomic fetch-add, so it does not
            // serialize the workers; if the budget is exhausted it no-ops.
            if (bounceCloud && material && !material->isDelta())
            {
                bounceCloud->append(BounceRecord{
                    photonHit.hit.position,
                    photonHit.photon.ray.direction,
                    photonHit.photon.color,
                    photonHit.hit.normal,
                    photonHit.hit.material,
                    photonHit.photon.time,
                });
            }

            // Bounce-hits below the threshold feed the EmitterQueue (daughter
            // path) to continue the random walk. Wave 3: push ONE compact Emitter
            // carrying the hit's generation state + the material's daughter count;
            // daughters are materialized lazily in processEmissions into reserved
            // photon-queue space.
            if (photonHit.photon.bounces < m_bounceThreshold)
            {
                const size_t n = material ? material->daughterPhotonCount() : 1;
                if (n > 0)
                {
                    m_emitterOverflow.emplace_back(photonHit, static_cast<std::uint32_t>(n));
                }
            }
        }

        flushIntoQueue(m_emitterOverflow, *emitterQueue);
    }

    syncOverflowGauge();

    photonsProcessed += photonsBlock.size();

    // We have fully captured this batch's output into the queues + overflow
    // buffers, so the source photons can be released. They are not lost: their
    // bounce-hits are either enqueued or parked in the persistent overflow.
    photonQueue->release(photonsBlock);

    auto workEnd = std::chrono::system_clock::now();
    auto workDuration = std::chrono::duration_cast<std::chrono::microseconds>(workEnd - workStart);
    photonDuration += workDuration.count();

    return true;
}

bool Worker::processEmissions()
{
    auto workStart = std::chrono::system_clock::now();

    // Claim-output-first, mirror of processPhotons: before pulling more emitters,
    // drain any parked from a prior iteration (new emitters that didn't fit, or
    // partially-consumed emitters returned for another pass). If the overflow
    // can't fully drain, do NOT fetch this round — let other stages free room.
    if (!m_emitterOverflow.empty())
    {
        flushIntoQueue(m_emitterOverflow, *emitterQueue);
        syncOverflowGauge();

        auto workEnd = std::chrono::system_clock::now();
        emitDuration += std::chrono::duration_cast<std::chrono::microseconds>(workEnd - workStart).count();
        return true;
    }

    auto emitterBlock = emitterQueue->fetch(m_fetchSize);

    if (emitterBlock.size() == 0)
    {
        emitterQueue->release(emitterBlock);
        auto workEnd = std::chrono::system_clock::now();
        emitDuration += std::chrono::duration_cast<std::chrono::microseconds>(workEnd - workStart).count();
        return true;
    }

    // Total daughters this batch of emitters still owes. A well-formed emitter
    // always has a valid material (it was built from a real hit), so we count its
    // full remaining; a (defensively handled) emitter with a missing material can
    // make no progress and is retired without reserving slots, so it does not
    // contribute to `owed`. This keeps `owed` equal to the number of slots the
    // fill loop below can actually consume, which guarantees we fill exactly the
    // slots we reserve (see the ready() note).
    size_t owed = 0;
    for (auto& emitter : emitterBlock)
    {
        std::shared_ptr<Material> material = materialLibrary->fetchByIndex(emitter.material);
        if (material)
        {
            owed += emitter.remaining();
        }
        else
        {
            emitter.advance(emitter.remaining()); // retire — unreachable in practice
        }
    }

    // RESERVE photon-queue space FIRST (claim-output-first preserves Wave 1's
    // losslessness — daughters are only ever materialized into space we already
    // hold, never speculatively). Reserve min(owed, freeSpace); initialize()
    // clamps again to the true remaining capacity, so a lost race just yields a
    // shorter block. Because the fill loop produces exactly min(owed, reserved)
    // daughters and reserved <= owed, every reserved slot gets filled.
    const size_t want = std::min(owed, photonQueue->freeSpace());
    auto reserved = photonQueue->initialize(want);
    const size_t reservedSlots = reserved.size();

    // Fill the reserved block by walking emitters in order, generating as many of
    // each emitter's REMAINING daughters as fit. An emitter generates its
    // daughters in ascending global-index order across pulls (Emitter::generated
    // tracks the cursor), so the lazy fan-out produces exactly the same daughters
    // as the eager path — only chunked across reserved-space pulls. Any emitter
    // not fully drained this pass is requeued; exhausted emitters are dropped.
    size_t slot = 0;
    std::vector<Emitter> requeue;

    for (size_t i = 0; i < emitterBlock.size(); ++i)
    {
        Emitter& emitter = emitterBlock[i];

        if (slot < reservedSlots && !emitter.done())
        {
            const size_t room = reservedSlots - slot;
            const size_t produce = std::min<size_t>(emitter.remaining(), room);

            std::shared_ptr<Material> material = materialLibrary->fetchByIndex(emitter.material);
            // material is non-null here: null-material emitters were retired above
            // (done()), so they never enter this branch.
            material->generateDaughters(
                reserved,
                /*blockStart=*/slot,
                /*globalStart=*/emitter.generated(),
                /*count=*/produce,
                /*totalDaughters=*/emitter.total(),
                emitter.incident,
                emitter.normal,
                emitter.position,
                emitter.color,
                emitter.time,
                emitter.bounces,
                m_generator);

            emitter.advance(static_cast<std::uint32_t>(produce));
            slot += produce;
        }

        if (!emitter.done())
        {
            requeue.push_back(emitter);
        }
    }

    // Publish the reserved daughters. By the reservation invariant slot ==
    // reservedSlots, so the whole reserved block is filled and ready()'d intact
    // (its endIndex matches what initialize() registered — required by the
    // WorkQueue ready bookkeeping).
    if (reservedSlots > 0)
    {
        photonQueue->ready(reserved);
    }

    // Lossless requeue: the un-finished emitters live in `requeue` (copies).
    // Release the fetched block, then re-enqueue. Anything that still doesn't fit
    // is parked in the persistent overflow and flushed by the claim-output-first
    // head of this function — never dropped.
    emitterQueue->release(emitterBlock);

    if (!requeue.empty())
    {
        flushIntoQueue(requeue, *emitterQueue);
        if (!requeue.empty())
        {
            m_emitterOverflow.insert(m_emitterOverflow.end(), requeue.begin(), requeue.end());
        }
        syncOverflowGauge();
    }

    auto workEnd = std::chrono::system_clock::now();
    auto workDuration = std::chrono::duration_cast<std::chrono::microseconds>(workEnd - workStart);
    emitDuration += workDuration.count();

    return true;
}
