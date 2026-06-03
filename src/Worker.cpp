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
        if (!photonQueue || !hitQueue || !finalHitQueue || !emittingQueue || !image || !camera || !buffer || !materialLibrary || !lightQueue)
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
            // pipeline below, fan-out is back-pressured via EmittingQueue rather than
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

            // Stage 1: raycast pending photons. Claim-output-first: a batch of
            // fetchSize photons produces up to fetchSize bounce-hits that must be
            // pushed to BOTH the hitQueue (splat path) and the EmittingQueue
            // (daughter path). processPhotons first flushes any per-worker overflow
            // from a prior iteration, and only fetches new source photons when both
            // overflow buffers are empty — so nothing is ever dropped, only deferred.
            //
            // We run the stage if there is work to do (new photons OR pending
            // overflow). The internal logic decides whether it can fetch.
            const bool photonOverflowPending = !m_hitOverflow.empty() || !m_emittingOverflow.empty();
            if (photonQueue->available() > 0 || photonOverflowPending)
            {
                if (!processPhotons())
                {
                    break;
                }
                drainedAnything = true;
            }

            // Stage 2: spawn daughters from pending bounce-hits, ONLY if the PhotonQueue
            // has space for the full N daughters of the next hit. processEmissions
            // queries daughterPhotonCount() on the next hit's material, checks capacity,
            // and either pops + spawns or leaves the hit in place for next iteration.
            if (emittingQueue->available() > 0)
            {
                if (!processEmissions())
                {
                    break;
                }
                drainedAnything = true;
            }

            // Stage 3: camera-visibility check. Like Stage 1 this is claim-output-
            // first — visible hits must be pushed to the finalHitQueue, and any
            // that don't fit are parked in m_finalOverflow and retried before the
            // next fetch. Run while there is hitQueue work OR pending overflow.
            if (hitQueue->available() > 0 || !m_finalOverflow.empty())
            {
                if (!processHits())
                {
                    break;
                }
                drainedAnything = true;
            }

            if (finalHitQueue->available() > 0)
            {
                if (!processFinalHits())
                {
                    break;
                }
                drainedAnything = true;
            }

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
        m_hitOverflow.size() + m_emittingOverflow.size() + m_finalOverflow.size(),
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

        double photonBrightness = lightQueue->getPhotonBrightness(object->name());

        auto photons = photonQueue->initialize(photonCount);

        std::static_pointer_cast<Light>(object)->emit(photons, photonBrightness, m_generator);

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

bool Worker::flushIntoQueue(std::vector<PhotonHit>& items, WorkQueue<PhotonHit>& queue)
{
    if (items.empty())
    {
        return true;
    }

    // initialize() clamps to the queue's remaining capacity, so the returned
    // block tells us exactly how many items the queue can accept right now.
    auto block = queue.initialize(items.size());
    const size_t placed = block.size();

    for (size_t i = 0; i < placed; ++i)
    {
        block[i] = items[i];
    }
    queue.ready(block);

    if (placed == items.size())
    {
        items.clear();
        return true;
    }

    // Partial placement: keep the unplaced tail for a later iteration. Nothing
    // is dropped — this is the back-pressure hold.
    items.erase(items.begin(), items.begin() + placed);
    return false;
}

bool Worker::processPhotons()
{
    auto workStart = std::chrono::system_clock::now();

    // Claim-output-first: before consuming any new source photons, drain any
    // bounce-hits parked from a previous iteration into their queues. If either
    // overflow can't fully drain, do NOT fetch new photons this round — that
    // would grow the overflow without bound. Leave the source photons in the
    // photonQueue (we simply don't fetch) and let the worker run other stages,
    // which frees downstream room. This is what makes the stage lossless.
    if (!m_hitOverflow.empty() || !m_emittingOverflow.empty())
    {
        flushIntoQueue(m_hitOverflow, *hitQueue);
        flushIntoQueue(m_emittingOverflow, *emittingQueue);
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
        // Every bounce-hit goes to hitQueue (splat path). The two paths are
        // independent downstream consumers, so we stage each into its own
        // overflow buffer and flush. Whatever doesn't fit stays parked and is
        // retried on the next iteration before any new photons are fetched —
        // nothing is dropped.
        for (auto& photonHit : m_hitBuffer)
        {
            m_hitOverflow.push_back(photonHit);

            // Bounce-hits below the threshold also feed the EmittingQueue
            // (daughter-spawn path). Terminal hits only contribute via the splat
            // path, so they're excluded from the emitting set.
            if (photonHit.photon.bounces < m_bounceThreshold)
            {
                m_emittingOverflow.push_back(photonHit);
            }
        }

        flushIntoQueue(m_hitOverflow, *hitQueue);
        flushIntoQueue(m_emittingOverflow, *emittingQueue);
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

    // Pull a single batch from the EmittingQueue. We then make a back-pressure
    // decision per-hit: only spawn daughters for hits whose material's
    // daughterPhotonCount fits in the PhotonQueue's currently free space. Hits
    // we can't service this iteration are re-pushed to the back of the queue.
    auto emissionsBlock = emittingQueue->fetch(m_fetchSize);

    if (emissionsBlock.size() == 0)
    {
        emittingQueue->release(emissionsBlock);
        auto workEnd = std::chrono::system_clock::now();
        emitDuration += std::chrono::duration_cast<std::chrono::microseconds>(workEnd - workStart).count();
        return true;
    }

    // Two-pass: first pass walks the batch, classifying each hit as "spawn now"
    // (fits in remaining PhotonQueue capacity) or "requeue" (doesn't fit). We
    // accumulate the total daughter count needed for the spawn-now set so we
    // can make one PhotonQueue allocation.
    std::vector<size_t> spawnIndices;
    std::vector<size_t> requeueIndices;
    spawnIndices.reserve(emissionsBlock.size());
    requeueIndices.reserve(emissionsBlock.size());

    size_t totalDaughters = 0;
    // Snapshot freeSpace once at the top. It can race with other workers'
    // allocations, but the photonQueue->initialize call below clamps to actual
    // remaining capacity so over-counting only causes a short allocation, not
    // corruption. Under-counting causes a hit to be requeued unnecessarily,
    // which is harmless — it'll be picked up next iteration.
    size_t budget = photonQueue->freeSpace();

    for (size_t i = 0; i < emissionsBlock.size(); ++i)
    {
        const PhotonHit& photonHit = emissionsBlock[i];
        std::shared_ptr<Material> material = materialLibrary->fetchByIndex(photonHit.hit.material);
        const size_t n = material ? material->daughterPhotonCount() : 1;

        if (n <= budget)
        {
            spawnIndices.push_back(i);
            totalDaughters += n;
            budget -= n;
        }
        else
        {
            requeueIndices.push_back(i);
        }
    }

    if (totalDaughters > 0)
    {
        auto bouncedBlock = photonQueue->initialize(totalDaughters);
        const size_t allocated = bouncedBlock.size();
        size_t photonIndex = 0;

        // If allocated < totalDaughters (a race was lost vs another worker's
        // allocation), the spawn loop below detects the shortfall per-hit and
        // appends the unserviced hits to requeueIndices — they are NOT dropped
        // here, they go back to the EmittingQueue for a later iteration.

        for (size_t spawnIdx : spawnIndices)
        {
            const PhotonHit& photonHit = emissionsBlock[spawnIdx];
            std::shared_ptr<Material> material = materialLibrary->fetchByIndex(photonHit.hit.material);
            const size_t n = material ? material->daughterPhotonCount() : 1;

            size_t startIndex = photonIndex;
            size_t endIndex = startIndex + n;

            if (endIndex > allocated)
            {
                // PhotonQueue exhausted mid-batch — requeue the remaining hits.
                requeueIndices.push_back(spawnIdx);
                continue;
            }

            material->bounce(bouncedBlock, startIndex, endIndex, photonHit, m_generator);
            photonIndex += n;
        }

        photonQueue->ready(bouncedBlock);
    }

    // Requeue: any hit we couldn't service this round goes back to the
    // EmittingQueue so a later iteration tries again. Order is not preserved
    // (and doesn't need to be — photon bounces commute).
    //
    // Lossless requeue: copy the un-serviced hits OUT of the fetched block
    // first, then release the block (which frees at least requeueIndices.size()
    // slots), then re-enqueue. We must copy before releasing because release
    // makes the slots available to other workers, who could overwrite them.
    // Any remainder that still doesn't fit (lost a race) is parked in the
    // persistent m_emittingOverflow and flushed by processPhotons — never
    // dropped.
    if (!requeueIndices.empty())
    {
        std::vector<PhotonHit> requeueHits;
        requeueHits.reserve(requeueIndices.size());
        for (size_t idx : requeueIndices)
        {
            requeueHits.push_back(emissionsBlock[idx]);
        }

        emittingQueue->release(emissionsBlock);

        flushIntoQueue(requeueHits, *emittingQueue);
        if (!requeueHits.empty())
        {
            m_emittingOverflow.insert(
                m_emittingOverflow.end(), requeueHits.begin(), requeueHits.end());
        }
        syncOverflowGauge();
    }
    else
    {
        emittingQueue->release(emissionsBlock);
    }

    auto workEnd = std::chrono::system_clock::now();
    auto workDuration = std::chrono::duration_cast<std::chrono::microseconds>(workEnd - workStart);
    emitDuration += workDuration.count();

    return true;
}

bool Worker::processHits()
{
    auto workStart = std::chrono::system_clock::now();

    // Claim-output-first: flush any camera-visible hits parked from a prior
    // iteration into the finalHitQueue before fetching more hitQueue work. If
    // the overflow can't fully drain, don't fetch — leave hitQueue items in
    // place and let processFinalHits drain the sink to free room. Lossless.
    if (!m_finalOverflow.empty())
    {
        flushIntoQueue(m_finalOverflow, *finalHitQueue);
        syncOverflowGauge();

        auto workEnd = std::chrono::system_clock::now();
        hitDuration += std::chrono::duration_cast<std::chrono::microseconds>(workEnd - workStart).count();
        return true;
    }

    auto hitsBlock = hitQueue->fetch(m_fetchSize);

    m_hitBuffer.clear();

    Vector cameraPosition = camera->position();
    Vector cameraNormal = camera->forward();

    for (auto& photonHit : hitsBlock)
    {
        std::optional<PixelCoords> coord = camera->coordForPoint(photonHit.hit.position);

        // Not within the camera frustum, skip
        if (!coord)
        {
            continue;
        }

        Vector pixelDirection = camera->pixelDirection(*coord);
        double dot = Vector::dot(pixelDirection, photonHit.hit.normal);

        // Not facing the pixel, skip
        if (dot >= 0.0)
        {
            continue;
        }

        Vector path = cameraPosition - photonHit.hit.position;
        double cameraDistance = path.magnitude();

        if (cameraDistance < selfHitThreshold)
        {
            continue;
        }

        Ray ray{photonHit.hit.position, path / cameraDistance};

        std::optional<Hit> closestHit;

        // Do any objects obscure this hit?
        for (auto& object : objects)
        {
            if (!object->hasType<Volume>())
            {
                continue;
            }

            std::optional<Hit> hit = std::static_pointer_cast<Volume>(object)->castRayAt(
                ray, m_castBuffer, photonHit.photon.time, animationQuery.get());

            if (hit)
            {
                if (hit->distance > selfHitThreshold && (!closestHit || hit->distance < closestHit->distance))
                {
                    closestHit = hit;
                }
            }
        }

        // If no object was hit, or the closest hit object is behind the camera, the hit is valid
        if (!closestHit || closestHit->distance > cameraDistance)
        {
            m_hitBuffer.push_back(photonHit);
        }
    }

    if (!m_hitBuffer.empty())
    {
        // Stage the camera-visible hits into the persistent overflow buffer and
        // flush. Whatever doesn't fit stays parked and is retried before the
        // next fetch — never dropped.
        m_finalOverflow.insert(m_finalOverflow.end(), m_hitBuffer.begin(), m_hitBuffer.end());
        flushIntoQueue(m_finalOverflow, *finalHitQueue);
    }

    syncOverflowGauge();

    hitsProcessed += hitsBlock.size();

    hitQueue->release(hitsBlock);

    auto workEnd = std::chrono::system_clock::now();
    auto workDuration = std::chrono::duration_cast<std::chrono::microseconds>(workEnd - workStart);
    hitDuration += workDuration.count();

    return true;
}

namespace
{

// 1-pixel-radius cone splat: distribute `contribution` across up to 4 neighboring integer
// pixels around the sub-pixel coordinate (fx, fy) using a linear-falloff kernel of radius
// 1.0 pixel. A perfectly centered bouncehit contributes 100% to a single pixel; a hit at
// the boundary between two pixels contributes ~50% to each; a hit at a 4-pixel corner
// distributes (1 - sqrt(0.5)) ≈ 0.293 to each of the four (slightly over unity in that
// corner case — acceptable for v1 since the cone's bias is uniform over the image and
// energy conservation is not catastrophically broken).
//
// This is the implementation of the "Mirror Visibility Model" cone gate in
// research/ray-tracer-architecture-vision.md: same kernel doing two jobs at once —
// resolving mirror reflections that delta-BRDF colorForHit can't render, and providing
// sub-pixel anti-aliasing for non-delta materials.
void splatCone(Buffer& buffer, double fx, double fy, size_t width, size_t height, const Color& contribution)
{
    if (contribution.brightness() <= 0.0f)
    {
        return;
    }

    const long lx = static_cast<long>(std::floor(fx));
    const long ly = static_cast<long>(std::floor(fy));

    for (long dy = 0; dy <= 1; ++dy)
    {
        for (long dx = 0; dx <= 1; ++dx)
        {
            const long px = lx + dx;
            const long py = ly + dy;

            if (px < 0 || py < 0 || px >= static_cast<long>(width) || py >= static_cast<long>(height))
            {
                continue;
            }

            const double ddx = static_cast<double>(px) - fx;
            const double ddy = static_cast<double>(py) - fy;
            const double dist = std::sqrt(ddx * ddx + ddy * ddy);
            if (dist >= 1.0)
            {
                continue;
            }

            const float weight = static_cast<float>(1.0 - dist);
            buffer.addColor(PixelCoords{static_cast<size_t>(px), static_cast<size_t>(py)}, contribution * weight);
        }
    }
}

}

bool Worker::processFinalHits()
{
    auto workStart = std::chrono::system_clock::now();
    auto hitsBlock = finalHitQueue->fetch(m_fetchSize);

    const Vector cameraPosition = camera->position();
    const size_t imageWidth = camera->width();
    const size_t imageHeight = camera->height();

    // Pixel angular size (radians per pixel) along the vertical axis. The "1-pixel radius"
    // direction cone for delta materials gates on angular deviation from the line-of-sight
    // to the camera, measured in these units.
    const double pixelAngularSize = Utility::radians(camera->verticalFieldOfView()) /
                                    static_cast<double>(imageHeight);

    for (auto& photonHit : hitsBlock)
    {
        std::optional<Camera::SubPixelCoords> subCoord = camera->coordForPointSubPixel(photonHit.hit.position);

        if (!subCoord)
        {
            continue;
        }

        // Integer-pixel coord for the per-pixel exposure window query. Sub-pixel-precise
        // gating is overkill — exposure windows are continuous across a frame, and the
        // current shutter models (global, rolling) vary at most per-row.
        PixelCoords nearestCoord{
            std::min(imageWidth - 1, static_cast<size_t>(std::max(0.0, std::round(subCoord->x)))),
            std::min(imageHeight - 1, static_cast<size_t>(std::max(0.0, std::round(subCoord->y))))
        };

        // Time gate: drop bounces whose emission timestamp falls outside the camera's
        // exposure window for this pixel. With the default base-class window (infinite),
        // every photon is accepted and behavior is identical to the pre-gate pipeline.
        Camera::ExposureWindow window = camera->exposureWindowForPixel(nearestCoord);
        if (!window.contains(photonHit.photon.time))
        {
            continue;
        }

        std::shared_ptr<Material> material = materialLibrary->fetchByIndex(photonHit.hit.material);
        if (!material)
        {
            continue;
        }

        Color contribution{0.0f, 0.0f, 0.0f};

        if (material->isDelta())
        {
            g_deltaHitsTotal.fetch_add(1);
            // Delta BRDF (mirror): the photon bounces in a single deterministic direction.
            // The bouncehit is visible ONLY if that reflected direction points back toward
            // the camera within a 1-pixel-radius angular cone. This is the new mirror
            // visibility path — pre-cone, delta materials rendered as black.
            const Vector incident = photonHit.photon.ray.direction;
            const Vector reflected = Vector::normalized(Vector::reflected(incident, photonHit.hit.normal));
            const Vector toCamera = cameraPosition - photonHit.hit.position;
            const double toCameraMag = toCamera.magnitude();
            if (toCameraMag <= 0.0)
            {
                continue;
            }
            const Vector toCameraDir = toCamera / toCameraMag;
            const double cosOff = Vector::dot(reflected, toCameraDir);
            if (cosOff <= 0.0)
            {
                g_deltaHitsRejectedBackface.fetch_add(1);
                continue;
            }
            // acos is well-defined here because cosOff is in (0, 1]. Numerical guard.
            const double angularOffset = std::acos(std::min(1.0, cosOff));

            // Delta-cone radius (in angular pixels). A strict 1-pixel-radius cone is what
            // the architecture vision doc specifies, but the photon budget required for
            // visible mirror reflections at that radius is impractical (per-pixel
            // acceptance rate ~5e-7 against a full sphere; at 20M photons, expected
            // accepted samples per mirror pixel is far less than one).
            //
            // The pragmatic fix for v1: widen the directional cone to kDeltaConeRadius
            // angular pixels and divide each accepted contribution by kDeltaConeRadius^2
            // for energy conservation. The expected per-pixel value is unchanged; per-
            // pixel variance drops by kDeltaConeRadius^2. The cost is that the mirror
            // image is softened by ~kDeltaConeRadius pixels (an emergent "frosted mirror"
            // look at large values).
            //
            // The architecturally correct long-term fix is the "fuzzing" optimization
            // sketched in the vision doc (track which bouncehits reach the camera, re-
            // emit photons from the source toward those paths). When that lands, this
            // widening reverts to 1.0 — both code paths describe the same physical model.
            constexpr double kDeltaConeRadius = 1.0;
            const double pixelOffset = angularOffset / (pixelAngularSize * kDeltaConeRadius);
            if (pixelOffset >= 1.0)
            {
                g_deltaHitsRejectedConeOffset.fetch_add(1);
                continue;
            }
            g_deltaHitsAccepted.fetch_add(1);
            // Linear falloff toward the cone edge. No 1/K^2 energy compensation here —
            // the strict-cone version of this would deliver each photon's full energy to
            // one pixel; widening the cone simply lets more photons land per pixel. The
            // per-photon contribution stays at the natural delta-reflection magnitude,
            // which produces "frosted mirror" appearance at the chosen radius rather
            // than perfect specular, but at correct overall brightness. Cone radius
            // becomes the roughness knob; K=1 is perfect specular and physically exact
            // but requires impractical photon counts to produce signal.
            const float directionWeight = static_cast<float>(1.0 - pixelOffset);

            // Energy = incoming color × mirror albedo. sample() is deterministic for
            // delta materials, so reusing it for the throughput weight is exact.
            BSDFSample s = material->sample(incident, photonHit.hit.normal, m_generator);
            contribution = photonHit.photon.color * s.weight * directionWeight;
        }
        else
        {
            // Non-delta BRDF: the existing camera splat trick. Project the hit's BRDF
            // value in the direction-toward-camera and modulate by the incoming photon
            // color. The sub-pixel cone below provides anti-aliasing.
            const Vector pixelDirection = camera->pixelDirection(nearestCoord);
            contribution = material->colorForHit(pixelDirection, photonHit);
        }

        splatCone(*buffer, subCoord->x, subCoord->y, imageWidth, imageHeight, contribution);
    }

    finalHitsProcessed += hitsBlock.size();

    finalHitQueue->release(hitsBlock);

    auto workEnd = std::chrono::system_clock::now();
    auto workDuration = std::chrono::duration_cast<std::chrono::microseconds>(workEnd - workStart);
    writeDuration += workDuration.count();

    return true;
}
