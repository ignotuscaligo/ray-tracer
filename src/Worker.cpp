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

// Firefly fix diagnostics. g_splatTotal counts every splat contribution that
// reached the footprint-area stage; g_splatRadiusClamped counts those whose raw
// footprint radius fell below m_minSplatRadius and was floored (these are the
// would-be fireflies). The ratio shows how often the floor engages; a nonzero
// count on a scene with close-to-camera geometry is the fix doing its job.
std::atomic<size_t> g_splatTotal{0};
std::atomic<size_t> g_splatRadiusClamped{0};
// Optional luminance-clamp guard: splats whose contributed luminance exceeded
// the per-splat clamp and were scaled down. Stays 0 when the clamp is disabled.
std::atomic<size_t> g_splatLuminanceClamped{0};

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

size_t splatTotal() { return g_splatTotal.load(); }
size_t splatRadiusClamped() { return g_splatRadiusClamped.load(); }
size_t splatLuminanceClamped() { return g_splatLuminanceClamped.load(); }
void resetSplatCounters()
{
    g_splatTotal.store(0);
    g_splatRadiusClamped.store(0);
    g_splatLuminanceClamped.store(0);
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
        if (!photonQueue || !emitterQueue || !image || !camera || !buffer || !materialLibrary || !lightQueue)
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

void Worker::setTerminationThreshold(double terminationThreshold)
{
    m_terminationThreshold = std::max(0.0, terminationThreshold);
}

void Worker::setRussianRoulette(const RussianRouletteConfig& config)
{
    m_russianRoulette = config;
}

void Worker::setDaughterCount(size_t countOverride, double scale)
{
    m_daughterCountOverride = countOverride;
    m_daughterCountScale = scale;
}

void Worker::setPhotonsPerLight(double photonsPerLight)
{
    m_photonsPerLight = photonsPerLight;
}

void Worker::setSplatTargets(const std::vector<SplatTarget>& targets)
{
    m_splatTargets = targets;
}

void Worker::setMinSplatRadius(double minSplatRadius)
{
    m_minSplatRadius = std::max(0.0, minSplatRadius);
}

void Worker::setSplatLuminanceClamp(double clamp)
{
    m_splatLuminanceClamp = std::max(0.0, clamp);
}

void Worker::splatToCamera(const PhotonHit& photonHit, const std::shared_ptr<Material>& material)
{
    // M2/M3 direct camera splat. Project the non-delta hit into each target
    // camera's pixel space, gate on frustum / facing / exposure window / occlusion
    // / debug filters, then accumulate the bounce's outgoing radiance toward that
    // camera into its pixel buffer. The photon is discarded — no per-photon
    // storage for the direct path. Looping the targets (one per scene camera)
    // preserves multi-camera and debug cameras (bounce/light filters) under the
    // splat model: each camera's direct image is produced here, not by a gather.
    if (!material || material->isDelta() || m_photonsPerLight <= 0.0)
    {
        return;
    }

    const Vector wi = -photonHit.photon.ray.direction;  // direction photon came from

    // Single-photon model: NO 1/N count-normalization here. Each photon's magnitude
    // was baked as Phi/N at emission, so the splat sums per-photon contributions
    // additively. Only the geometric divide by footprint AREA (pi r^2) below
    // remains — that is a units conversion, not a count.

    for (const SplatTarget& target : m_splatTargets)
    {
        const std::shared_ptr<Camera>& cam = target.camera;
        if (!cam || !target.buffer)
        {
            continue;
        }

        // Debug-camera deposit filters: a $bounceFilter camera takes only deposits
        // at the requested bounce depth, a $lightFilter camera only those from the
        // requested light. -1 admits everything (the default full image).
        if (target.bounceFilter >= 0 && photonHit.photon.bounces != target.bounceFilter)
        {
            continue;
        }
        if (target.lightFilter >= 0 && photonHit.photon.lightId != target.lightFilter)
        {
            continue;
        }

        const size_t imageWidth = cam->width();
        const size_t imageHeight = cam->height();
        if (imageWidth == 0 || imageHeight == 0)
        {
            continue;
        }

        // Continuous pixel coordinate (and an early frustum reject).
        std::optional<Camera::SubPixelCoords> subCoord =
            cam->coordForPointSubPixel(photonHit.hit.position);
        if (!subCoord)
        {
            continue;
        }

        // Nearest integer pixel — the splat writes the photon's full contribution
        // into its exact projected pixel (sharp direct visibility, no smear).
        const PixelCoords coord{
            std::min(imageWidth - 1, static_cast<size_t>(std::max(0.0, std::floor(subCoord->x)))),
            std::min(imageHeight - 1, static_cast<size_t>(std::max(0.0, std::floor(subCoord->y)))),
        };

        // Per-pixel exposure-window gate (motion blur / rolling shutter).
        const Camera::ExposureWindow window = cam->exposureWindowForPixel(coord);
        if (!window.contains(photonHit.photon.time))
        {
            continue;
        }

        const Vector cameraPosition = cam->position();
        const Vector toCamera = cameraPosition - photonHit.hit.position;
        const double cameraDistance = toCamera.magnitude();
        if (cameraDistance < selfHitThreshold)
        {
            continue;
        }
        const Vector toCameraDir = toCamera / cameraDistance;

        // Facing check: the surface must face this camera. The same dot product is the
        // foreshortening cosine between the surface normal and the camera direction —
        // it must scale the splat contribution (see below). Without it, grazing hits
        // at a sphere's silhouette deposit full energy into the thin rim band they
        // project onto, producing a bright over-exposed rim. cos(theta) -> 0 at the
        // grazing edge foreshortens that energy to near-zero, which is correct.
        const double cosCamera = Vector::dot(toCameraDir, photonHit.hit.normal);
        if (cosCamera <= 0.0)
        {
            continue;
        }

        // Occlusion: cast from the hit toward the camera; if a nearer surface lies
        // between the hit and the camera, this bounce is not directly visible.
        const Ray ray{photonHit.hit.position, toCameraDir};
        std::optional<Hit> closestHit;
        for (auto& object : objects)
        {
            if (!object->hasType<Volume>())
            {
                continue;
            }
            std::optional<Hit> hit = std::static_pointer_cast<Volume>(object)->castRayAt(
                ray, m_castBuffer, photonHit.photon.time, animationQuery.get());
            if (hit && hit->distance > selfHitThreshold &&
                (!closestHit || hit->distance < closestHit->distance))
            {
                closestHit = hit;
            }
        }
        if (closestHit && closestHit->distance < cameraDistance)
        {
            continue;  // occluded
        }

        // Outgoing radiance toward the camera: BRDF(wi -> wo) * cos(theta) * power,
        // normalized by the photon count (1/N) and the pixel's world-space footprint
        // area (pi r^2) so the buffer holds physical luminance the existing tonemap
        // consumes unchanged. cos(theta) = dot(normal, toCameraDir) is the foreshortening
        // term (captured above as cosCamera). r is the footprint radius at the hit depth.
        const double pixelHalfAngle =
            0.5 * Utility::radians(cam->verticalFieldOfView()) /
            static_cast<double>(imageHeight);
        // Firefly fix: floor the footprint radius. The raw r is the pixel's
        // world-space radius at this hit's depth; for an indirect photon landing
        // close to the camera r collapses and 1/(pi r^2) explodes, spiking the
        // pixel to white. Clamping r up to m_minSplatRadius caps that weight at
        // 1/(pi r_min^2) so no single splat can dominate a pixel.
        //
        // NOTE on energy: this forward splat writes each contribution into ONE
        // pixel (no gather, no kernel). Flooring r therefore lowers that pixel's
        // weight WITHOUT redistributing the excess to neighbors — for the floored
        // splats it is mildly energy-lossy, not the strictly energy-preserving
        // spread a radius gather would give. The loss is bounded and confined to
        // the would-be-fireflies (only splats with r < r_min are touched); a true
        // spread would require splatting across the footprint's pixel disc, which
        // is a larger change to the splat and is not needed for the cases this
        // floor is meant to catch. m_minSplatRadius is a world-space length tied
        // to the scene-depth pixel footprint (see setMinSplatRadius); 0 disables.
        const double rawRadius = cameraDistance * std::tan(pixelHalfAngle);
        const double r = Utility::flooredSplatRadius(rawRadius, m_minSplatRadius);
        g_splatTotal.fetch_add(1, std::memory_order_relaxed);
        if (r > rawRadius)
        {
            g_splatRadiusClamped.fetch_add(1, std::memory_order_relaxed);
        }
        if (r <= 0.0)
        {
            continue;
        }

        const Color brdf = material->evaluate(wi, toCameraDir, photonHit.hit.normal);

        const double area = Utility::pi * r * r;
        const float scale = static_cast<float>(cosCamera / area);
        const Color contribution = brdf * photonHit.photon.color * scale;

        if (contribution.brightness() <= 0.0f)
        {
            continue;
        }

        // Optional extreme-firefly guard. The radius floor above only bounds the
        // geometric 1/(pi r^2) blowup; a firefly whose energy comes from a
        // degenerate transport path (normal footprint, photon-count-invariant —
        // e.g. the collinear point-light/sphere-top/camera dot) slips past it.
        // If a (generous, high) clamp is set, scale any splat whose brightness
        // exceeds it down to the clamp, preserving hue (uniform channel scale).
        // Disabled by default (m_splatLuminanceClamp <= 0): no scaling, image
        // unchanged. Set high enough that only pathological outliers are touched.
        Color clamped = contribution;
        if (m_splatLuminanceClamp > 0.0)
        {
            const double brightness = static_cast<double>(contribution.brightness());
            if (brightness > m_splatLuminanceClamp)
            {
                const float factor =
                    static_cast<float>(m_splatLuminanceClamp / brightness);
                clamped = contribution * factor;
                g_splatLuminanceClamped.fetch_add(1, std::memory_order_relaxed);
            }
        }

        target.buffer->addColor(coord, clamped);
    }
}

size_t Worker::resolveDaughterCount(size_t materialCount) const
{
    // SINGLE-PHOTON light tracing: every bounce scatters EXACTLY ONE outgoing
    // photon, so the photon population stays constant (only emission adds
    // photons; each bounce is 1-in-1-out). The material's native
    // daughterPhotonCount() is intentionally IGNORED for the core model — the
    // fan-out it described is gone.
    //
    // The legacy $daughterCount override is kept only as an explicit escape hatch
    // (e.g. old N-sweep scenes): a value > 1 forces that many independent
    // stochastic samples per bounce, growing the population again. With NO 1/N
    // split downstream this multiplies energy by the count, so it is NOT
    // energy-equivalent to single-photon and is for experiments only. The default
    // (override 0) is the canonical single photon.
    (void)materialCount;
    if (m_daughterCountOverride > 0)
    {
        return m_daughterCountOverride;
    }
    return 1;
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

    // Wave 6: track each light's index in the scene light list (lights in object
    // declaration order). The light-id is stamped onto every emitted photon below
    // and inherited by daughters, so each deposit can be attributed to its source
    // light by the per-light debug camera.
    int lightIndex = -1;
    for (auto& object : objects)
    {
        if (!object->hasType<Light>())
        {
            continue;
        }

        ++lightIndex;

        size_t photonCount = lightQueue->fetchPhotons(object->name(), m_fetchSize);

        if (photonCount == 0)
        {
            continue;
        }

        double photonFlux = lightQueue->getPhotonFlux(object->name());

        auto photons = photonQueue->initialize(photonCount);

        std::static_pointer_cast<Light>(object)->emit(photons, photonFlux, m_generator);

        // Stamp the source light-id on every freshly-emitted photon (emit() resets
        // bounces/ray/color but leaves lightId for us to set here, so emit()
        // implementations don't each need to know their scene index).
        for (auto& photon : photons)
        {
            photon.lightId = lightIndex;
        }

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

            for (size_t i = 0; i < m_volumeHitBuffer.size(); ++i)
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

            // Storage pivot M3: accumulate this NON-DELTA bounce's energy into the
            // QUANTIZED DENSITY GRID cell it landed in, then discard the photon.
            // This replaces the per-photon BounceCloud record entirely: storage is
            // bounded by occupied cells, not photon count. A Lambertian surface's
            // outgoing radiance is view-independent, so the cell accumulates the
            // incoming photon power (an irradiance accumulator); the mirror gather
            // reads it back and multiplies by the reflected surface's BRDF. Pure
            // mirrors / delta materials are excluded — a delta bounce has no diffuse
            // deposit; it is the ray-extension case in the gather. The add is
            // sharded + locked per cell, so distinct cells do not serialize workers.
            if (densityGrid && material && !material->isDelta())
            {
                densityGrid->add(photonHit.hit.position, photonHit.photon.color);
            }

            // Storage pivot M2: DIRECT CAMERA SPLAT for camera-visible non-delta
            // surfaces. Projects this bounce into the camera and accumulates its
            // outgoing radiance into the pixel buffer (sharp direct image), then
            // the photon is discarded — no per-photon storage for the direct path.
            splatToCamera(photonHit, material);

            // Continue the random walk only while BOTH terminators allow it; the
            // photon dies at WHICHEVER fires FIRST:
            //   1. BOUNCE CAP (the scene's $bounceThreshold): a HARD per-photon
            //      ceiling on path depth. A photon that has already bounced
            //      m_bounceThreshold times is terminal — no further emitter is
            //      pushed. This is the real depth bound: scenes set bounceThreshold
            //      2 or 3 expecting short paths, and that value is honored exactly.
            //   2. ABSOLUTE DECAY: kill the photon once its current magnitude falls
            //      below a fixed absolute floor (terminationThreshold, in photon-
            //      magnitude / flux units). This is monotonic — every BSDF weight is
            //      <= 1, so magnitude only decreases across bounces. Because the
            //      floor is absolute (not relative to emission), a BRIGHTER photon
            //      survives MORE bounces than a dimmer one before crossing it; the
            //      bounce cap above is what keeps that bounded.
            //      No Russian-roulette survivor reweight: termination is a hard
            //      decay + cutoff, NOT a probabilistic 1/p boost.
            const bool decayAlive = photonDecayAlive(photonHit.photon, m_terminationThreshold);

            if (decayAlive && photonHit.photon.bounces < static_cast<int>(m_bounceThreshold))
            {
                // Single-photon continuation: push ONE compact Emitter carrying the
                // hit's generation state. resolveDaughterCount returns 1 (constant
                // population) unless an experiment override forces more.
                const size_t materialCount = material ? material->daughterPhotonCount() : 1;
                const size_t n = resolveDaughterCount(materialCount);
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
                emitter.lightId,
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
