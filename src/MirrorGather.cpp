#include "MirrorGather.h"

#include "DielectricMaterial.h"
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

// Camera samples per pixel for STOCHASTIC delta surfaces (glass). Each sample
// makes one independent Fresnel reflect/refract pick; averaging them removes the
// per-pixel noise that the stochastic choice introduces. Mirrors are deterministic
// and use a single sample regardless. This is the samples-per-pixel lever — raise
// it for cleaner glass at linear cost, lower it for speed.
constexpr int kCameraSamplesPerPixel = 16;

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
    // (No photonsPerLight: the grid gather is a pure additive sum now — the 1/N is
    // baked at emission, so the lookup needs no count normalization.)
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

    // Dielectric (glass) and mirror are both delta materials extended by a SINGLE
    // continued ray. For glass we no longer trace both Fresnel branches (that was
    // exponential, 2^depth, because every entry/exit/internal interface forked).
    // Instead we let the material's sample() make ONE stochastic Fresnel pick:
    // reflect with probability R, refract with probability 1-R, returning weight =
    // tint. Because the choice probability matches the lobe weight, the R / (1-R)
    // cancels and the single-ray throughput is an UNBIASED estimator of the old
    // R*reflect + (1-R)*refract combination — exactly the mirror's
    // `weight * child` bookkeeping. Glass and photon passes now share sample().
    if (material->isDelta())
    {
        const UnitVector hitNormal = UnitVector::alreadyNormalized(hit->normal);
        const BSDFSample s = material->sample(direction, hitNormal, generator);
        if (!s.valid)
        {
            return Color{0.0f, 0.0f, 0.0f};
        }
        const Vector nextDir = Vector::normalized(s.direction);
        // Offset along the CHOSEN outgoing direction rather than the geometric
        // normal: a reflected ray leaves on the incident side, a refracted ray
        // leaves through the surface to the far side. Stepping along nextDir pushes
        // the child origin off the surface correctly in either case (the old
        // two-branch code offset reflect along the normal and refract along the
        // transmitted dir — stepping along nextDir unifies both).
        const Vector nextOrigin = hit->position + nextDir * kReflectionEpsilon;
        const Color child = reflectedRadiance(
            ctx, nextOrigin, nextDir, depth + 1, castBuffer, generator, outNonDeltaSeen);
        return s.weight * child;
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
    // glossy material treats this as a same-side query. The hit normal is unit by
    // construction (geometry producers normalize it); wrap for the typed BSDF API.
    const UnitVector hitNormal = UnitVector::alreadyNormalized(hit->normal);
    const Color brdf = material->evaluate(wo, wo, hitNormal);
    const Color irradiance = ctx.grid.lookupIrradiance(hit->position);

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

    std::vector<Hit> castBuffer;
    RandomGenerator generator;

    for (size_t y = rowBegin; y < rowEnd; ++y)
    {
        for (size_t x = 0; x < width; ++x)
        {
            const PixelCoords coord{x, y};
            // Deterministic pixel-center primary ray (origin + direction from the
            // camera projection; orthographic varies the origin across the plane).
            const Ray ray = camera.generatePrimaryRay(coord);
            const Vector dir = ray.direction;

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

            // First visible surface is a delta material (mirror or glass): extend
            // the camera ray through it and look up the grid at whatever non-delta
            // surface(s) it reaches.
            ++stats.pixelsDelta;

            // A mirror is deterministic (one valid reflected direction), so a single
            // extension is exact. Glass is now STOCHASTIC at the camera — each ray
            // makes one random Fresnel reflect/refract pick — so a single sample is
            // noisy. Average kCameraSamplesPerPixel independent extensions to drive
            // that noise down. For a deterministic material every sample is identical,
            // so the extra samples cost time but don't change the result; we therefore
            // only multi-sample when the first visible surface is a dielectric.
            const bool stochasticDelta =
                (dynamic_cast<DielectricMaterial*>(material.get()) != nullptr);
            const int sampleCount = stochasticDelta ? kCameraSamplesPerPixel : 1;

            bool nonDeltaSeen = false;
            Color accumulated{0.0f, 0.0f, 0.0f};
            bool anyValid = false;

            const UnitVector hitNormal = UnitVector::alreadyNormalized(hit->normal);
            for (int sample = 0; sample < sampleCount; ++sample)
            {
                // Single delta extension: sample() makes the (stochastic, for glass;
                // deterministic, for mirror) outgoing pick and returns its weight.
                const BSDFSample s = material->sample(dir, hitNormal, generator);
                if (!s.valid)
                {
                    continue;
                }
                anyValid = true;
                const Vector nextDir = Vector::normalized(s.direction);
                const Vector nextOrigin = hit->position + nextDir * kReflectionEpsilon;
                accumulated += s.weight * reflectedRadiance(
                    ctx, nextOrigin, nextDir, /*depth=*/1, castBuffer, generator, nonDeltaSeen);
            }

            if (!anyValid)
            {
                ++stats.pixelsBlack;
                continue;
            }

            const Color reflected = accumulated * (1.0f / static_cast<float>(sampleCount));

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
    // photonsPerLight is retained in the public signature for caller stability but
    // is no longer used: the single-photon gather is a pure additive sum (the 1/N
    // count-normalization is baked at emission).
    (void)photonsPerLight;

    Result result;
    const size_t width = camera->width();
    const size_t height = camera->height();
    if (width == 0 || height == 0)
    {
        return result;
    }

    const Context ctx{objects, grid, materials, animation};

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
