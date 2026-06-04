#include "Gather.h"

#include "Hit.h"
#include "Material.h"
#include "RandomGenerator.h"
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

// Wave 4c: ray-extension through specular (delta) surfaces. A mirror hit does not
// gather (the density estimate is undefined for a delta BRDF — zero probability of
// a deposit lying exactly on the mirror direction); instead the camera path is
// EXTENDED along the perfect reflection and the gather happens at whatever the
// mirror sees. To bound mirror-to-mirror loops we cap the reflection recursion;
// past the cap the path returns black. The epsilon offset pushes the reflection
// origin off the surface so the next raycast does not immediately self-intersect.
constexpr int kMaxSpecularDepth = 8;
constexpr double kReflectionEpsilon = 1e-3;

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

// Read-only inputs shared by every gatherRadiance call within a thread. Bundled so
// the recursion's signature stays small; everything here is either immutable scene
// data (cloud/grid/materials/animation) or a precomputed constant (invN, half-angle).
struct GatherContext
{
    const std::vector<std::shared_ptr<Object>>& objects;
    const BounceCloud& cloud;
    const HashGrid& grid;
    const MaterialLibrary& materials;
    const AnimationQuery* animation;
    double invN;
    double pixelHalfAngle;
    // Wave 6 debug-camera deposit filters. -1 disables a filter.
    int bounceFilter = -1;
    int lightFilter = -1;
};

// Density-estimate gather at a NON-DELTA surface hit: the unchanged Wave 4b estimate
// of outgoing radiance toward `wo` (the direction the path arrived from, pointing
// away from the surface). Returns physical luminance (1/N + footprint normalized);
// 0 when no deposits land in the footprint. Records the footprint radius and deposit
// count into stats for the diagnostics.
Color gatherDensity(const GatherContext& ctx,
                    const Hit& hit,
                    const std::shared_ptr<Material>& material,
                    const Vector& wo,
                    double pathLength,
                    PixelStats& stats)
{
    // Fixed footprint: the world-space radius the pixel's solid angle projects to at
    // this depth. r = d * tan(halfAngle), where d is the TOTAL path length from the
    // camera (summed across reflection segments), not just the last segment — the
    // pixel cone keeps widening as it travels, so a surface seen in a mirror is sized
    // by how far the light path actually traveled to reach it.
    const double r = pathLength * std::tan(ctx.pixelHalfAngle);
    if (r <= 0.0)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }
    if (r > stats.maxRadius)
    {
        stats.maxRadius = r;
    }

    const std::vector<std::size_t> neighbors = ctx.grid.radiusSearch(hit.position, r);
    if (neighbors.empty())
    {
        return Color{0.0f, 0.0f, 0.0f};
    }

    // Density estimate: sum BRDF(d.incoming -> wo) * d.power over the deposits in the
    // footprint, then normalize by the disc area pi*r^2.
    Color radiance{0.0f, 0.0f, 0.0f};
    size_t contributing = 0;
    for (std::size_t index : neighbors)
    {
        const BounceRecord& record = ctx.cloud[index];

        // Wave 6 debug-camera filters: skip deposits that don't match the requested
        // bounce depth / light-id. A disabled filter (-1) admits everything.
        if (ctx.bounceFilter >= 0 && record.bounces != ctx.bounceFilter)
        {
            continue;
        }
        if (ctx.lightFilter >= 0 && record.lightId != ctx.lightFilter)
        {
            continue;
        }

        // wi: direction the deposited photon came FROM (the BRDF's first argument).
        // record.incoming is the photon's travel direction (into the surface), so
        // wi = -incoming.
        const Vector wi = -record.incoming;

        // Evaluate the BRDF at the HIT surface against (wi, wo). The deposit material
        // matches the hit material (deposits land on this surface), so evaluating on
        // `material` is correct and avoids a per-deposit library lookup. evaluate()
        // returns 0 when either direction is below the surface (wrong hemisphere), so
        // deposits from grazing/back-facing photons drop out naturally.
        const Color f = material->evaluate(wi, wo, hit.normal);
        radiance += f * record.power;
        ++contributing;
    }

    // With a debug filter active a footprint may legitimately contain zero matching
    // deposits even though the pixel saw geometry; that pixel contributes nothing
    // and is not counted as gathered (keeps the diagnostics honest per-pass).
    if (contributing == 0)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }

    const double area = Utility::pi * r * r;
    const float scale = static_cast<float>(ctx.invN / area);

    stats.depositsAccum += contributing;
    ++stats.gathered;

    return radiance * scale;
}

// Wave 4c recursive radiance along a ray.
//
//   1. Raycast (origin, direction) to the first surface. Miss -> background (black).
//   2. NON-DELTA hit (diffuse/glossy): run the fixed-footprint density gather toward
//      the camera-side (the direction we came from) and RETURN it. Terminal.
//   3. DELTA hit (Mirror): do NOT gather. Sample the BRDF to get the perfect
//      reflection direction and the surface reflectance, offset the origin off the
//      surface by an epsilon, and RETURN reflectance * gatherRadiance(reflected ray,
//      depth+1). This extends the camera path through the mirror so the gather lands
//      on whatever the mirror sees (room -> reflection; light -> bright spot; another
//      mirror -> recurse).
//   4. Termination: at depth >= kMaxSpecularDepth return black, bounding mirror loops.
//
// `direction` must be unit length and points along travel (into the surface).
// `topLevel` is true only for the camera ray; it drives the hit/delta/miss pixel
// classification counters (those describe the camera-VISIBLE surface, unchanged in
// meaning from 4b — a mirror pixel still counts as `delta`).
Color gatherRadiance(const GatherContext& ctx,
                     const Vector& origin,
                     const Vector& direction,
                     int depth,
                     double pathLength,
                     std::vector<Hit>& castBuffer,
                     RandomGenerator& generator,
                     bool topLevel,
                     PixelStats& stats)
{
    if (depth >= kMaxSpecularDepth)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }

    const Ray ray{origin, direction};

    // Time 0: the gather is evaluated at the photon pass's static scene state. (The
    // cloud was deposited across the exposure window already.)
    std::optional<Hit> hit = firstHit(ctx.objects, ray, castBuffer, 0.0f, ctx.animation);

    if (!hit)
    {
        if (topLevel)
        {
            ++stats.miss;
        }
        return Color{0.0f, 0.0f, 0.0f};
    }
    if (topLevel)
    {
        ++stats.hit;
    }

    std::shared_ptr<Material> material = ctx.materials.fetchByIndex(hit->material);
    if (!material)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }

    if (material->isDelta())
    {
        if (topLevel)
        {
            ++stats.delta;
        }

        // Sample the delta BRDF: returns the perfect reflection direction and the
        // throughput weight (the mirror's albedo, with any Fresnel folded in by the
        // material). The mirror ignores the generator; passing it keeps the call
        // generic across delta materials. `incident` is the travel direction into the
        // surface, i.e. our ray direction.
        const BSDFSample s = material->sample(direction, hit->normal, generator);
        if (!s.valid)
        {
            return Color{0.0f, 0.0f, 0.0f};
        }

        const Vector reflectedDir = Vector::normalized(s.direction);
        // Offset the reflection origin off the surface along the normal so the next
        // raycast does not immediately re-hit this surface.
        const Vector nextOrigin = hit->position + hit->normal * kReflectionEpsilon;

        const Color reflected = gatherRadiance(
            ctx, nextOrigin, reflectedDir, depth + 1, pathLength + hit->distance,
            castBuffer, generator, /*topLevel=*/false, stats);

        return s.weight * reflected;
    }

    // NON-DELTA: density-estimate gather toward the direction we arrived from.
    // wo points away from the surface, back along the incoming ray. The footprint is
    // sized by the total camera path length accumulated up to this surface.
    const Vector wo = -direction;
    return gatherDensity(ctx, *hit, material, wo, pathLength + hit->distance, stats);
}

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
                const GatherFilters& filters,
                Buffer& buffer,
                PixelStats& stats)
{
    const size_t width = camera.width();
    const Vector cameraPos = camera.position();

    const GatherContext ctx{objects, cloud, grid, materials, animation, invN,
                            pixelHalfAngle, filters.bounceFilter, filters.lightFilter};

    std::vector<Hit> castBuffer;
    // Per-thread RNG for delta BRDF sampling. Mirror reflection is deterministic and
    // ignores the draws; this exists only to satisfy the sample() signature and to
    // remain correct if a stochastic delta material is added later.
    RandomGenerator generator;

    for (size_t y = rowBegin; y < rowEnd; ++y)
    {
        for (size_t x = 0; x < width; ++x)
        {
            const PixelCoords coord{x, y};
            const Vector dir = Vector::normalized(camera.pixelDirection(coord));

            const Color pixelLuminance = gatherRadiance(
                ctx, cameraPos, dir, /*depth=*/0, /*pathLength=*/0.0, castBuffer,
                generator, /*topLevel=*/true, stats);

            if (pixelLuminance.red == 0.0f && pixelLuminance.green == 0.0f &&
                pixelLuminance.blue == 0.0f)
            {
                continue;
            }

            buffer.addColor(coord, pixelLuminance);

            const double peak = std::max(
                {pixelLuminance.red, pixelLuminance.green, pixelLuminance.blue});
            if (peak > stats.maxRadiance)
            {
                stats.maxRadiance = peak;
            }
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
                 size_t workerCount,
                 const GatherFilters& filters)
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
                       animation, invN, pixelHalfAngle, filters, *result.buffer,
                       perThread[t]);
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
