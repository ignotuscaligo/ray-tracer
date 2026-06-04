#include "Gather.h"

#include "Hit.h"
#include "Material.h"
#include "Ray.h"
#include "Utility.h"
#include "Volume.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <optional>
#include <thread>

namespace Gather
{

namespace
{

constexpr double kSelfHitThreshold = std::numeric_limits<double>::epsilon();

// Closest visible surface point along a camera ray. Mirrors the photon pass's
// own first-hit logic (Worker::processPhotons): cast against every Volume, keep
// the nearest forward hit beyond a self-hit epsilon. The gather uses the exact
// same geometry/animation the deposits were generated against.
std::optional<Hit> firstHit(const std::vector<std::shared_ptr<Object>>& objects,
                            const Ray& ray,
                            std::vector<Hit>& castBuffer,
                            float time,
                            const AnimationQuery* animation)
{
    std::optional<Hit> closest;
    for (const auto& object : objects)
    {
        if (!object->hasType<Volume>())
        {
            continue;
        }

        std::optional<Hit> hit = std::static_pointer_cast<Volume>(object)->castRayAt(
            ray, castBuffer, time, animation);

        if (hit && hit->distance > kSelfHitThreshold &&
            (!closest || hit->distance < closest->distance))
        {
            closest = hit;
        }
    }
    return closest;
}

// Per-pixel gather worker state. Each thread owns its own scratch buffers so the
// pixel loop is embarrassingly parallel (the cloud + grid are read-only here).
struct PixelStats
{
    size_t hit = 0;
    size_t gathered = 0;
    size_t delta = 0;
    size_t miss = 0;
    double maxRadius = 0.0;
    size_t depositsAccum = 0;  // total deposits summed over gathered pixels
    double maxRadiance = 0.0;  // peak luminance written to a pixel (pre-exposure)
};

void gatherRows(size_t rowBegin,
                size_t rowEnd,
                const std::vector<std::shared_ptr<Object>>& objects,
                const Camera& camera,
                const BounceCloud& cloud,
                const HashGrid& grid,
                const MaterialLibrary& materials,
                const AnimationQuery* animation,
                double invN,
                double pixelHalfAngle,
                Buffer& buffer,
                PixelStats& stats)
{
    const size_t width = camera.width();
    const Vector cameraPos = camera.position();

    std::vector<Hit> castBuffer;

    for (size_t y = rowBegin; y < rowEnd; ++y)
    {
        for (size_t x = 0; x < width; ++x)
        {
            const PixelCoords coord{x, y};
            const Vector dir = Vector::normalized(camera.pixelDirection(coord));
            const Ray ray{cameraPos, dir};

            // Time 0: the gather is evaluated at the photon pass's static scene
            // state. (Per-pixel temporal gather is a later concern; the cloud was
            // deposited across the exposure window already.)
            std::optional<Hit> hit = firstHit(objects, ray, castBuffer, 0.0f, animation);

            if (!hit)
            {
                ++stats.miss;
                continue;
            }
            ++stats.hit;

            std::shared_ptr<Material> material = materials.fetchByIndex(hit->material);
            if (!material)
            {
                continue;
            }

            // Specular / delta visible surface: leave black this sub-wave. The
            // density estimate is undefined for a delta BRDF (zero probability of
            // a deposit lying exactly on the mirror direction); 4c handles these
            // by extending the camera ray through the reflection.
            if (material->isDelta())
            {
                ++stats.delta;
                continue;
            }

            // Fixed footprint: the world-space radius the pixel's solid angle
            // projects to at this depth. r = d * tan(halfAngle).
            const double r = hit->distance * std::tan(pixelHalfAngle);
            if (r <= 0.0)
            {
                continue;
            }
            if (r > stats.maxRadius)
            {
                stats.maxRadius = r;
            }

            const std::vector<std::size_t> neighbors = grid.radiusSearch(hit->position, r);
            if (neighbors.empty())
            {
                continue;
            }

            // wo: direction from the surface toward the camera (BRDF convention —
            // points away from the surface).
            const Vector toCamera = cameraPos - hit->position;
            const double toCameraMag = toCamera.magnitude();
            if (toCameraMag <= 0.0)
            {
                continue;
            }
            const Vector wo = toCamera / toCameraMag;

            // Density estimate: sum BRDF(d.incoming -> camera) * d.power over the
            // deposits in the footprint, then normalize by the disc area pi*r^2.
            Color radiance{0.0f, 0.0f, 0.0f};
            for (std::size_t index : neighbors)
            {
                const BounceRecord& record = cloud[index];

                // wi: direction the deposited photon came FROM (the BRDF's first
                // argument). record.incoming is the photon's travel direction
                // (into the surface), so wi = -incoming.
                const Vector wi = -record.incoming;

                // Evaluate the BRDF at the HIT surface against (wi, wo). The
                // deposit material matches the hit material (deposits land on this
                // surface), so evaluating on `material` is correct and avoids a
                // per-deposit library lookup. evaluate() returns 0 when either
                // direction is below the surface (wrong hemisphere), so deposits
                // from grazing/back-facing photons drop out naturally.
                const Color f = material->evaluate(wi, wo, hit->normal);
                radiance += f * record.power;
            }

            const double area = Utility::pi * r * r;
            const float scale = static_cast<float>(invN / area);
            const Color pixelLuminance = radiance * scale;
            buffer.addColor(coord, pixelLuminance);

            const double peak = std::max({pixelLuminance.red, pixelLuminance.green, pixelLuminance.blue});
            if (peak > stats.maxRadiance)
            {
                stats.maxRadiance = peak;
            }

            ++stats.gathered;
            stats.depositsAccum += neighbors.size();
        }
    }
}

}  // namespace

GatherResult run(const std::vector<std::shared_ptr<Object>>& objects,
                 const std::shared_ptr<Camera>& camera,
                 const BounceCloud& cloud,
                 const HashGrid& grid,
                 const MaterialLibrary& materials,
                 const AnimationQuery* animation,
                 double photonsPerLight,
                 size_t workerCount)
{
    GatherResult result;
    const size_t width = camera->width();
    const size_t height = camera->height();
    result.buffer = std::make_shared<Buffer>(width, height);
    result.buffer->clear();

    if (height == 0 || width == 0)
    {
        return result;
    }

    const double invN = (photonsPerLight > 0.0) ? (1.0 / photonsPerLight) : 0.0;

    // Half the angular size of a single pixel along the vertical axis. The
    // footprint radius is hitDistance * tan(pixelHalfAngle): the half-extent on
    // the surface of the cone one pixel subtends.
    const double pixelHalfAngle =
        0.5 * Utility::radians(camera->verticalFieldOfView()) / static_cast<double>(height);

    const size_t threads = std::max<size_t>(1, workerCount);
    const size_t effectiveThreads = std::min(threads, height);

    std::vector<PixelStats> perThread(effectiveThreads);
    std::vector<std::thread> pool;
    pool.reserve(effectiveThreads);

    const size_t rowsPerThread = (height + effectiveThreads - 1) / effectiveThreads;

    for (size_t t = 0; t < effectiveThreads; ++t)
    {
        const size_t rowBegin = t * rowsPerThread;
        const size_t rowEnd = std::min(height, rowBegin + rowsPerThread);
        if (rowBegin >= rowEnd)
        {
            break;
        }

        pool.emplace_back([&, rowBegin, rowEnd, t]() {
            gatherRows(rowBegin, rowEnd, objects, *camera, cloud, grid, materials,
                       animation, invN, pixelHalfAngle, *result.buffer, perThread[t]);
        });
    }

    for (auto& thread : pool)
    {
        thread.join();
    }

    size_t depositsAccum = 0;
    for (const auto& s : perThread)
    {
        result.pixelsHit += s.hit;
        result.pixelsGathered += s.gathered;
        result.pixelsDelta += s.delta;
        result.pixelsMiss += s.miss;
        result.maxGatherRadius = std::max(result.maxGatherRadius, s.maxRadius);
        result.maxRadiance = std::max(result.maxRadiance, s.maxRadiance);
        depositsAccum += s.depositsAccum;
    }
    result.meanDepositsPerGather =
        (result.pixelsGathered > 0)
            ? static_cast<double>(depositsAccum) / static_cast<double>(result.pixelsGathered)
            : 0.0;

    return result;
}

}  // namespace Gather
