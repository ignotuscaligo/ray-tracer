#include "MirrorGather.h"

#include "Hit.h"
#include "Material.h"
#include "RandomGenerator.h"
#include "Ray.h"
#include "Vector.h"
#include "Volume.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <optional>
#include <thread>

namespace MirrorGather
{

namespace
{

constexpr double kSelfHitThreshold = std::numeric_limits<double>::epsilon();
constexpr int kMaxSpecularDepth = 8;
constexpr double kReflectionEpsilon = 1e-3;

// Closest visible surface along a ray (mirrors the gather/photon-pass first-hit).
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

struct Context
{
    const std::vector<std::shared_ptr<Object>>& objects;
    const DensityGrid& grid;
    const MaterialLibrary& materials;
    const AnimationQuery* animation;
    double photonsPerLight;
};

// Follow the perfect reflection from a delta surface until a non-delta surface,
// then return the radiance it reflects toward the viewer (the grid irradiance at
// that point times the surface's diffuse BRDF). Returns black if the path misses
// geometry, exceeds the specular depth, or lands on an empty grid cell.
//
// `outNonDeltaSeen` is set true once any non-delta surface is reached (so the
// caller can tell "reflected the room but the cell was empty" from "reflected
// into the void"); used only for diagnostics.
Color reflectedRadiance(const Context& ctx,
                        const Vector& origin,
                        const Vector& direction,
                        int depth,
                        std::vector<Hit>& castBuffer,
                        RandomGenerator& generator,
                        bool& outNonDeltaSeen)
{
    if (depth >= kMaxSpecularDepth)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }

    const Ray ray{origin, direction};
    std::optional<Hit> hit = firstHit(ctx.objects, ray, castBuffer, 0.0f, ctx.animation);
    if (!hit)
    {
        return Color{0.0f, 0.0f, 0.0f};  // reflected into the background
    }

    std::shared_ptr<Material> material = ctx.materials.fetchByIndex(hit->material);
    if (!material)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }

    if (material->isDelta())
    {
        // Chained mirror: keep extending the reflection.
        const BSDFSample s = material->sample(direction, hit->normal, generator);
        if (!s.valid)
        {
            return Color{0.0f, 0.0f, 0.0f};
        }
        const Vector reflectedDir = Vector::normalized(s.direction);
        const Vector nextOrigin = hit->position + hit->normal * kReflectionEpsilon;
        const Color reflected = reflectedRadiance(
            ctx, nextOrigin, reflectedDir, depth + 1, castBuffer, generator, outNonDeltaSeen);
        return s.weight * reflected;
    }

    // Non-delta surface: the mirror sees a diffuse/glossy point. Its outgoing
    // radiance toward the viewer is view-independent for a Lambertian surface:
    //   L = (albedo / pi) * E = BRDF * irradiance
    // The grid lookup supplies the irradiance estimate E for the cell; the
    // material's evaluate() supplies the BRDF (albedo/pi for Lambertian — the wi
    // argument is unused for a Lambertian, and irradiance already integrates over
    // incoming directions). wo points back toward the viewer (opposite the
    // reflected ray we cast in).
    outNonDeltaSeen = true;

    const Vector wo = -direction;
    // wi is unused by Lambertian::evaluate; pass wo so any hemisphere check in a
    // glossy material treats this as a same-side query.
    const Color brdf = material->evaluate(wo, wo, hit->normal);
    const Color irradiance = ctx.grid.lookupIrradiance(hit->position, ctx.photonsPerLight);

    return brdf * irradiance;
}

void gatherRows(size_t rowBegin,
                size_t rowEnd,
                const Context& ctx,
                const Camera& camera,
                Buffer& buffer,
                Result& stats)
{
    const size_t width = camera.width();
    const Vector cameraPos = camera.position();

    std::vector<Hit> castBuffer;
    RandomGenerator generator;

    for (size_t y = rowBegin; y < rowEnd; ++y)
    {
        for (size_t x = 0; x < width; ++x)
        {
            const PixelCoords coord{x, y};
            const Vector dir = Vector::normalized(camera.pixelDirection(coord));

            const Ray ray{cameraPos, dir};
            std::optional<Hit> hit = firstHit(ctx.objects, ray, castBuffer, 0.0f, ctx.animation);
            if (!hit)
            {
                continue;  // background; splat owns / leaves black
            }

            std::shared_ptr<Material> material = ctx.materials.fetchByIndex(hit->material);
            if (!material || !material->isDelta())
            {
                continue;  // direct non-delta pixel — owned by the splat, skip
            }

            // First visible surface is a mirror: extend the reflection and look up
            // the grid at whatever non-delta surface it reaches.
            ++stats.pixelsDelta;

            const BSDFSample s = material->sample(dir, hit->normal, generator);
            if (!s.valid)
            {
                ++stats.pixelsBlack;
                continue;
            }
            const Vector reflectedDir = Vector::normalized(s.direction);
            const Vector nextOrigin = hit->position + hit->normal * kReflectionEpsilon;

            bool nonDeltaSeen = false;
            const Color reflected = s.weight * reflectedRadiance(
                ctx, nextOrigin, reflectedDir, /*depth=*/1, castBuffer, generator, nonDeltaSeen);

            if (reflected.red == 0.0f && reflected.green == 0.0f && reflected.blue == 0.0f)
            {
                ++stats.pixelsBlack;
                continue;
            }

            buffer.addColor(coord, reflected);
            ++stats.pixelsReflected;
            const double peak = std::max({static_cast<double>(reflected.red),
                                          static_cast<double>(reflected.green),
                                          static_cast<double>(reflected.blue)});
            if (peak > stats.maxRadiance)
            {
                stats.maxRadiance = peak;
            }
            stats.sumRadiance += 0.2126 * reflected.red + 0.7152 * reflected.green +
                                 0.0722 * reflected.blue;
        }
    }
}

}  // namespace

Result run(const std::vector<std::shared_ptr<Object>>& objects,
           const std::shared_ptr<Camera>& camera,
           const DensityGrid& grid,
           const MaterialLibrary& materials,
           const AnimationQuery* animation,
           double photonsPerLight,
           size_t workerCount,
           Buffer& buffer)
{
    Result result;
    const size_t width = camera->width();
    const size_t height = camera->height();
    if (width == 0 || height == 0)
    {
        return result;
    }

    const Context ctx{objects, grid, materials, animation, photonsPerLight};

    const size_t threads = std::max<size_t>(1, workerCount);
    const size_t effectiveThreads = std::min(threads, height);

    std::vector<Result> perThread(effectiveThreads);
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
            gatherRows(rowBegin, rowEnd, ctx, *camera, buffer, perThread[t]);
        });
    }
    for (auto& thread : pool)
    {
        thread.join();
    }

    for (const auto& s : perThread)
    {
        result.pixelsDelta += s.pixelsDelta;
        result.pixelsReflected += s.pixelsReflected;
        result.pixelsBlack += s.pixelsBlack;
        result.maxRadiance = std::max(result.maxRadiance, s.maxRadiance);
        result.sumRadiance += s.sumRadiance;
    }
    return result;
}

}  // namespace MirrorGather
