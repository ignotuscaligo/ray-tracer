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

}

namespace WorkerDebug
{
size_t splatTotal() { return g_splatTotal.load(); }
size_t splatRadiusClamped() { return g_splatRadiusClamped.load(); }
size_t splatLuminanceClamped() { return g_splatLuminanceClamped.load(); }
void resetSplatCounters()
{
    g_splatTotal.store(0);
    g_splatRadiusClamped.store(0);
    g_splatLuminanceClamped.store(0);
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
        if (!photonQueue || !image || !camera || !buffer || !materialLibrary || !lightQueue)
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

            // Emit a batch of fresh photons whenever the light still owes some and
            // the photon queue has headroom. Single-photon trace-to-completion keeps
            // the population constant (one outgoing photon per bounce), so a small
            // fixed headroom margin is all the gate needs.
            if (lightQueue->remainingPhotons() > 0 && photonQueue->freeSpace() > m_fetchSize * 2)
            {
                if (!processLights())
                {
                    break;
                }
                drainedAnything = true;
            }

            // Trace pending photons to completion: each fetched photon is followed
            // through all its bounces (intersect -> deposit + splat -> scatter one
            // continuation) inside processPhotons, with no per-bounce requeue. The
            // image is produced by the camera splat during the pass plus the
            // post-pass gather over the density grid.
            if (photonQueue->available() > 0)
            {
                if (!processPhotons())
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

void Worker::setTerminationThreshold(double terminationThreshold)
{
    m_terminationThreshold = std::max(0.0, terminationThreshold);
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

bool Worker::processLights()
{
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

        photonQueue->ready(photons);

        break;
    }

    return true;
}

bool Worker::processPhotons()
{
    auto photonsBlock = photonQueue->fetch(m_fetchSize);

    // Single local slot used to scatter the next photon in the trace-to-completion
    // loop. generateDaughters() writes into a WorkQueue<Photon>::Block; we wrap a
    // one-element vector in a Block so the SAME scatter primitive the pipeline has
    // always used produces the continuation photon — identical RNG draw, identical
    // weight math — but in-place instead of via a requeue. Allocated once per call.
    std::vector<Photon> scatterSlot(1);

    for (auto& sourcePhoton : photonsBlock)
    {
        // Trace this emitted photon to COMPLETION on this worker: intersect,
        // deposit + splat at each bounce, scatter exactly one importance-sampled
        // continuation photon, and repeat until the photon decays below the
        // termination floor, reaches the bounce cap, or escapes the scene. No
        // per-bounce requeue: the whole random walk stays in this loop on this
        // worker's RNG (a Monte-Carlo reshuffle relative to the old requeue path,
        // not a change in the expected image).
        Photon photon = sourcePhoton;

        while (true)
        {
            // Skip / terminate photons with no brightness left.
            if (photon.color.brightness() < std::numeric_limits<double>::epsilon())
            {
                break;
            }

            // Nearest-hit raycast across all volumes (front-most valid hit).
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

            if (!validHit)
            {
                // Escaped the scene — the random walk ends.
                break;
            }

            PhotonHit photonHit = m_volumeHitBuffer[minIndex];
            std::shared_ptr<Material> material = materialLibrary->fetchByIndex(photonHit.hit.material);

            // Storage pivot M3: accumulate this NON-DELTA bounce's energy into the
            // QUANTIZED DENSITY GRID cell it landed in. Storage is bounded by
            // occupied cells, not photon count. A Lambertian surface's outgoing
            // radiance is view-independent, so the cell accumulates the incoming
            // photon power (an irradiance accumulator); the mirror gather reads it
            // back and multiplies by the reflected surface's BRDF. Pure mirrors /
            // delta materials are excluded — a delta bounce has no diffuse deposit;
            // it is the ray-extension case in the gather. The add is sharded +
            // locked per cell, so distinct cells do not serialize workers.
            if (densityGrid && material && !material->isDelta())
            {
                densityGrid->add(photonHit.hit.position, photonHit.photon.color);
            }

            // Storage pivot M2: DIRECT CAMERA SPLAT for camera-visible non-delta
            // surfaces. Projects this bounce into the camera and accumulates its
            // outgoing radiance into the pixel buffer (sharp direct image).
            splatToCamera(photonHit, material);

            // Continue the random walk only while BOTH terminators allow it; the
            // photon dies at WHICHEVER fires FIRST:
            //   1. BOUNCE CAP (the scene's $bounceThreshold): a HARD per-photon
            //      ceiling on path depth. A photon that has already bounced
            //      m_bounceThreshold times is terminal. Scenes set bounceThreshold
            //      2 or 3 expecting short paths, and that value is honored exactly.
            //   2. ABSOLUTE DECAY: kill the photon once its current magnitude falls
            //      below a fixed absolute floor (terminationThreshold, in photon-
            //      magnitude / flux units). Monotonic — every BSDF weight is <= 1,
            //      so magnitude only decreases across bounces. The floor is absolute
            //      (not relative to emission), so a BRIGHTER photon survives MORE
            //      bounces; the bounce cap keeps that bounded. No Russian-roulette
            //      survivor reweight: termination is a hard decay + cutoff.
            const bool decayAlive = photonDecayAlive(photonHit.photon, m_terminationThreshold);
            if (!decayAlive || photonHit.photon.bounces >= static_cast<int>(m_bounceThreshold))
            {
                break;
            }

            if (!material)
            {
                break;
            }

            // Scatter exactly ONE importance-sampled continuation photon (the
            // single-photon model: every bounce is 1-in-1-out, population constant).
            // generateDaughters with totalDaughters=1 is the identical primitive the
            // requeue path used — same sample() draw, same magnitude * BSDF weight.
            WorkQueue<Photon>::Block block(0, 1, scatterSlot);
            material->generateDaughters(
                block,
                /*blockStart=*/0,
                /*globalStart=*/0,
                /*count=*/1,
                /*totalDaughters=*/1,
                photonHit.photon.ray.direction,
                photonHit.hit.normal,
                photonHit.hit.position,
                photonHit.photon.color,
                photonHit.photon.time,
                photonHit.photon.bounces,
                photonHit.photon.lightId,
                m_generator);

            // Continue the walk with the scattered photon.
            photon = scatterSlot[0];
        }
    }

    // The whole batch has been traced to completion; release the source slots.
    photonQueue->release(photonsBlock);

    return true;
}
