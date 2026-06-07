#include "EmissiveGather.h"

#include "AreaLight.h"
#include "Color.h"
#include "Hit.h"
#include "Light.h"
#include "Quaternion.h"
#include "Ray.h"
#include "Vector.h"
#include "Volume.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <thread>

namespace EmissiveGather
{

namespace
{

constexpr double kSelfHitThreshold = std::numeric_limits<double>::epsilon();

// Coincidence margin for the occlusion test (see gatherRows). A scene Volume is
// treated as occluding the emitter only if it lies NEARER than the emitter by
// more than this RELATIVE fraction of the emitter distance. Geometry that is
// coplanar/coincident with the emitter (e.g. a ceiling mesh in the emitter's
// own plane) is hit at t ~= bestT and so falls inside the margin: it does not
// count as an occluder and cannot z-fight the panel dark.
//
// The margin is RELATIVE (a fraction of bestT) rather than an absolute world
// distance, so it scales with scene/camera geometry automatically — no magic
// absolute number to retune per scene. 1e-3 (0.1% of the emitter distance) is
// far larger than the floating-point noise that drives the z-fight (~1e-12 of
// bestT here) yet far smaller than the gap to any genuinely-occluding surface
// (the Cornell sphere/walls sit at well under half the emitter distance, i.e.
// hundreds of times outside this margin), so real occlusion is unaffected.
constexpr double kOcclusionCoincidenceMargin = 1e-3;

// A planar emissive patch resolved from an emitter: its plane, in-plane extent,
// and the constant outgoing radiance it shows toward any viewer.
struct EmissivePatch
{
    Vector center;
    Vector normal;  // emission axis (forward); the lit face
    Vector right;   // in-plane +X (unit)
    Vector up;      // in-plane +Y (unit)
    double halfWidth = 0.0;   // square: half extent along right
    double halfHeight = 0.0;  // square: half extent along up
    double radius = 0.0;      // disc: radius
    bool isDisc = false;
    Color radiance{0.0f, 0.0f, 0.0f};
};

std::vector<EmissivePatch> collectPatches(
    const std::vector<std::shared_ptr<Object>>& objects)
{
    std::vector<EmissivePatch> patches;
    for (const auto& object : objects)
    {
        if (!object->hasType<AreaLight>())
        {
            continue;
        }
        auto light = std::static_pointer_cast<AreaLight>(object);
        const Color radiance = light->surfaceRadiance();
        if (radiance.red <= 0.0f && radiance.green <= 0.0f && radiance.blue <= 0.0f)
        {
            continue;
        }

        const Quaternion orientation = light->rotation();
        EmissivePatch patch;
        patch.center = light->position();
        patch.right = (orientation * Vector::unitX).normalized();
        patch.up = (orientation * Vector::unitY).normalized();
        patch.normal = (orientation * Vector::unitZ).normalized();
        patch.radiance = radiance;
        if (light->shape() == AreaLight::Shape::Disc)
        {
            patch.isDisc = true;
            patch.radius = light->radius();
        }
        else
        {
            patch.halfWidth = light->width() * 0.5;
            patch.halfHeight = light->height() * 0.5;
        }
        patches.push_back(patch);
    }
    return patches;
}

// Intersect a ray with the emissive patch. Returns the hit distance along the
// ray (t > 0) if it strikes the FRONT face (the lit hemisphere) within the
// patch bounds, else nullopt. A Lambertian emitter only emits from its front
// face, so a back-facing view of the fixture shows nothing.
std::optional<double> intersectPatch(const EmissivePatch& patch, const Ray& ray)
{
    const double denom = Vector::dot(ray.direction, patch.normal);
    // Front face only: the camera must look at the emitting side, i.e. the ray
    // travels against the normal (denom < 0).
    if (denom >= -kSelfHitThreshold)
    {
        return std::nullopt;
    }

    const double t = Vector::dot(patch.center - ray.origin, patch.normal) / denom;
    if (t <= kSelfHitThreshold)
    {
        return std::nullopt;
    }

    const Vector hitPoint = ray.origin + (ray.direction * t);
    const Vector local = hitPoint - patch.center;
    const double u = Vector::dot(local, patch.right);
    const double v = Vector::dot(local, patch.up);

    if (patch.isDisc)
    {
        if ((u * u + v * v) > (patch.radius * patch.radius))
        {
            return std::nullopt;
        }
    }
    else
    {
        if (std::abs(u) > patch.halfWidth || std::abs(v) > patch.halfHeight)
        {
            return std::nullopt;
        }
    }
    return t;
}

// Distance to the nearest scene Volume along the ray (occlusion test). Mirrors
// the photon-pass / gather first-hit. Returns +inf if nothing is hit.
double nearestOccluder(const std::vector<std::shared_ptr<Object>>& objects,
                       const Ray& ray,
                       std::vector<Hit>& castBuffer,
                       const AnimationQuery* animation)
{
    double nearest = std::numeric_limits<double>::infinity();
    for (const auto& object : objects)
    {
        if (!object->hasType<Volume>())
        {
            continue;
        }
        std::optional<Hit> hit = std::static_pointer_cast<Volume>(object)->castRayAt(
            ray, castBuffer, 0.0f, animation);
        if (hit && hit->distance > kSelfHitThreshold && hit->distance < nearest)
        {
            nearest = hit->distance;
        }
    }
    return nearest;
}

double luminanceOf(const Color& c)
{
    return 0.2126 * c.red + 0.7152 * c.green + 0.0722 * c.blue;
}

void gatherRows(size_t rowBegin,
                size_t rowEnd,
                const std::vector<std::shared_ptr<Object>>& objects,
                const std::vector<EmissivePatch>& patches,
                const AnimationQuery* animation,
                const Camera& camera,
                Buffer& buffer,
                Result& stats)
{
    const size_t width = camera.width();
    std::vector<Hit> castBuffer;

    for (size_t y = rowBegin; y < rowEnd; ++y)
    {
        for (size_t x = 0; x < width; ++x)
        {
            const PixelCoords coord{x, y};
            // Deterministic pixel-center primary ray. Origin AND direction come
            // from the camera projection (orthographic varies the origin across
            // the image plane; perspective shares cameraPos).
            const Ray ray = camera.generatePrimaryRay(coord);

            // Closest emissive patch the pixel ray hits.
            double bestT = std::numeric_limits<double>::infinity();
            const EmissivePatch* bestPatch = nullptr;
            for (const auto& patch : patches)
            {
                std::optional<double> t = intersectPatch(patch, ray);
                if (t && *t < bestT)
                {
                    bestT = *t;
                    bestPatch = &patch;
                }
            }
            if (!bestPatch)
            {
                continue;
            }

            // Occlusion: a Volume nearer than the emitter hides the fixture.
            //
            // The emitter at y=300 is coplanar with the Cornell ceiling mesh, so
            // a naive `occluder < bestT` z-fights: per-pixel floating-point noise
            // makes the coincident ceiling resolve nearer than the emitter ~42%
            // of the time, dropping those panel pixels to black (salt-and-pepper
            // speckle). Require the occluder to be nearer by more than a small
            // RELATIVE margin of the emitter distance, so coincident/coplanar
            // geometry (hit at t ~= bestT) is not treated as an occluder. Genuine
            // occluders (walls, the blocker sphere) sit far inside bestT and still
            // block. Tied to bestT, the threshold scales with scene geometry.
            const double occluder = nearestOccluder(objects, ray, castBuffer, animation);
            if (occluder < bestT * (1.0 - kOcclusionCoincidenceMargin))
            {
                continue;
            }

            // The emitter shows its view-independent surface radiance L = M/pi.
            // Written straight into the pixel (physical luminance, as the splat /
            // mirror gather write), so the tonemap maps it through the camera
            // exposure unchanged.
            buffer.addColor(coord, bestPatch->radiance);

            ++stats.pixelsEmissive;
            const double lum = luminanceOf(bestPatch->radiance);
            stats.sumRadiance += lum;
            if (lum > stats.maxRadiance)
            {
                stats.maxRadiance = lum;
            }
        }
    }
}

}  // namespace

Result run(const std::vector<std::shared_ptr<Object>>& objects,
           const std::shared_ptr<Camera>& camera,
           const AnimationQuery* animation,
           size_t workerCount,
           Buffer& buffer)
{
    Result result;
    if (!camera)
    {
        return result;
    }
    const size_t width = camera->width();
    const size_t height = camera->height();
    if (width == 0 || height == 0)
    {
        return result;
    }

    const std::vector<EmissivePatch> patches = collectPatches(objects);
    if (patches.empty())
    {
        return result;
    }

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
            gatherRows(rowBegin, rowEnd, objects, patches, animation, *camera,
                       buffer, perThread[t]);
        });
    }
    for (auto& thread : pool)
    {
        thread.join();
    }

    for (const auto& s : perThread)
    {
        result.pixelsEmissive += s.pixelsEmissive;
        result.maxRadiance = std::max(result.maxRadiance, s.maxRadiance);
        result.sumRadiance += s.sumRadiance;
    }
    return result;
}

}  // namespace EmissiveGather
