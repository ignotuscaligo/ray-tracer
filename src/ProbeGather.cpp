#include "ProbeGather.h"

#include "DielectricMaterial.h"
#include "EmitterPatch.h"
#include "Hit.h"
#include "Material.h"
#include "RandomGenerator.h"
#include "Ray.h"
#include "UnitVector.h"
#include "Utility.h"
#include "Vector.h"
#include "Volume.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <thread>

namespace ProbeGather
{

namespace
{

constexpr double kSelfHitThreshold = std::numeric_limits<double>::epsilon();
constexpr int kMaxSpecularDepth = 8;
constexpr double kReflectionEpsilon = 1e-3;

// Sentinel material index marking a Hit on an emitter (AreaLight) surface. The
// emitter is not a scene Volume / has no MaterialLibrary entry, so a hit on its
// patch carries this index instead. The gather treats an emitter hit as a
// non-delta surface with an IDENTITY BRDF (f = 1): summing the emitter's own
// radiance deposits over the footprint and dividing by area reproduces its
// surface radiance L = M/pi, exactly like any other gathered surface — so the
// fixture renders directly AND in mirrors with no special-case pass.
constexpr std::size_t kEmitterMaterial = std::numeric_limits<std::size_t>::max();

// Closest emitter patch the ray strikes (front face, within bounds). Fills `hit`
// with the patch hit and returns its distance; +inf if no patch is struck. The
// returned hit's material is kEmitterMaterial.
double firstEmitterHit(const std::vector<EmitterPatch>& patches,
                       const Ray& ray,
                       Hit& hit)
{
    double bestT = std::numeric_limits<double>::infinity();
    const EmitterPatch* best = nullptr;
    for (const auto& patch : patches)
    {
        const std::optional<double> t = intersectEmitterPatch(patch, ray);
        if (t && *t > kSelfHitThreshold && *t < bestT)
        {
            bestT = *t;
            best = &patch;
        }
    }
    if (!best)
    {
        return std::numeric_limits<double>::infinity();
    }
    hit.position = ray.origin + ray.direction * bestT;
    hit.normal = best->normal;
    hit.distance = bestT;
    hit.material = kEmitterMaterial;
    return bestT;
}

// Camera samples per pixel for STOCHASTIC delta surfaces (glass): each makes one
// independent Fresnel reflect/refract pick; averaging removes the per-pixel noise
// the stochastic choice introduces. Mirrors are deterministic (single sample).
constexpr int kCameraSamplesPerPixel = 16;

// Closest visible surface along a ray (mirrors the photon pass first-hit). Emitter
// patches (if supplied) are intersected alongside scene Volumes, so a camera or
// specular ray that lands on a light fixture returns an emitter Hit and gathers
// the fixture's radiance like any other surface.
std::optional<Hit> firstHit(const std::vector<std::shared_ptr<Object>>& objects,
                            const Ray& ray,
                            std::vector<Hit>& castBuffer,
                            float time,
                            const AnimationQuery* animation,
                            const std::vector<EmitterPatch>* patches = nullptr)
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
    if (patches && !patches->empty())
    {
        Hit emitterHit;
        const double t = firstEmitterHit(*patches, ray, emitterHit);
        if (std::isfinite(t) && (!closest || t < closest->distance))
        {
            closest = emitterHit;
        }
    }
    return closest;
}

}  // namespace

// ===== Probe pass =====

namespace
{

// Extend a ray through delta surfaces to the first non-delta hit. Returns that
// hit (and reports whether any delta surface was traversed). nullopt if the chain
// escapes the scene or exceeds the specular depth cap.
std::optional<Hit> extendToNonDelta(const std::vector<std::shared_ptr<Object>>& objects,
                                    const MaterialLibrary& materials,
                                    const AnimationQuery* animation,
                                    std::vector<Hit>& castBuffer,
                                    RandomGenerator& generator,
                                    Ray ray,
                                    const std::vector<EmitterPatch>& patches,
                                    bool& outTraversedDelta)
{
    outTraversedDelta = false;
    for (int depth = 0; depth < kMaxSpecularDepth; ++depth)
    {
        std::optional<Hit> hit = firstHit(objects, ray, castBuffer, 0.0f, animation, &patches);
        if (!hit)
        {
            return std::nullopt;
        }
        if (hit->material == kEmitterMaterial)
        {
            return hit;  // emitter surface: a non-delta gatherable surface
        }
        std::shared_ptr<Material> material = materials.fetchByIndex(hit->material);
        if (!material)
        {
            return std::nullopt;
        }
        if (!material->isDelta())
        {
            return hit;  // reached the first non-delta surface
        }

        // Delta surface: follow the (deterministic for mirror, stochastic for
        // glass) outgoing direction and continue.
        outTraversedDelta = true;
        const UnitVector hitNormal = UnitVector::alreadyNormalized(hit->normal);
        const BSDFSample s = material->sample(ray.direction, hitNormal, generator);
        if (!s.valid)
        {
            return std::nullopt;
        }
        const Vector nextDir = Vector::normalized(s.direction);
        ray = Ray{hit->position + nextDir * kReflectionEpsilon, nextDir};
    }
    return std::nullopt;  // exceeded specular depth
}

void collectProbeRows(size_t rowBegin,
                      size_t rowEnd,
                      const std::vector<std::shared_ptr<Object>>& objects,
                      const Camera& camera,
                      const MaterialLibrary& materials,
                      const AnimationQuery* animation,
                      const std::vector<EmitterPatch>& patches,
                      size_t subSample,
                      ProbeResult& out)
{
    const size_t width = camera.width();
    std::vector<Hit> castBuffer;
    RandomGenerator generator;

    for (size_t y = rowBegin; y < rowEnd; y += subSample)
    {
        for (size_t x = 0; x < width; x += subSample)
        {
            const PixelCoords coord{x, y};
            const Ray ray = camera.generatePrimaryRay(coord);
            ++out.cameraRays;

            bool traversedDelta = false;
            std::optional<Hit> hit = extendToNonDelta(
                objects, materials, animation, castBuffer, generator, ray, patches,
                traversedDelta);
            if (traversedDelta)
            {
                ++out.deltaExtensions;
            }
            if (!hit)
            {
                ++out.misses;
                continue;
            }
            out.probes.push_back(hit->position);
        }
    }
}

}  // namespace

ProbeResult collectProbes(const std::vector<std::shared_ptr<Object>>& objects,
                          const Camera& camera,
                          const MaterialLibrary& materials,
                          const AnimationQuery* animation,
                          size_t subSample)
{
    ProbeResult result;
    const size_t width = camera.width();
    const size_t height = camera.height();
    if (width == 0 || height == 0)
    {
        return result;
    }
    const size_t stride = std::max<size_t>(1, subSample);

    const std::vector<EmitterPatch> patches = collectEmitterPatches(objects);

    // The probe pass is camera-ray-scale and cheap; run it single-threaded so the
    // probe list (and its diagnostics) accumulate without cross-thread merging.
    // For very large frames this can be parallelized like the gather; not needed
    // at current resolutions.
    collectProbeRows(0, height, objects, camera, materials, animation, patches, stride,
                     result);
    return result;
}

// ===== Emitter deposits =====

EmitterDepositResult depositEmitters(const std::vector<std::shared_ptr<Object>>& objects,
                                     const ProbeIndex& probeIndex,
                                     double depositSpacing,
                                     BounceStore& store)
{
    EmitterDepositResult result;
    const std::vector<EmitterPatch> patches = collectEmitterPatches(objects);
    result.patches = patches.size();
    if (patches.empty())
    {
        return result;
    }

    const double spacing = std::max(depositSpacing, 1e-6);

    for (const EmitterPatch& patch : patches)
    {
        // Tile the patch with a uniform grid of deposit points in its (u, v)
        // in-plane parameterization. A square spans [-halfWidth, +halfWidth] ×
        // [-halfHeight, +halfHeight]; a disc is bounded by its radius and points
        // outside it are skipped.
        double extentU = patch.isDisc ? patch.radius : patch.halfWidth;
        double extentV = patch.isDisc ? patch.radius : patch.halfHeight;
        if (extentU <= 0.0 || extentV <= 0.0)
        {
            continue;
        }

        // Cell-centered samples so each deposit represents an equal sub-area.
        const int countU = std::max(1, static_cast<int>(std::ceil(2.0 * extentU / spacing)));
        const int countV = std::max(1, static_cast<int>(std::ceil(2.0 * extentV / spacing)));
        const double stepU = (2.0 * extentU) / countU;
        const double stepV = (2.0 * extentV) / countV;

        const double area = patch.isDisc
                                ? (Utility::pi * patch.radius * patch.radius)
                                : (2.0 * patch.halfWidth) * (2.0 * patch.halfHeight);

        // Count the deposits that actually fall on the patch (a disc drops the
        // corner cells), so per-deposit power = radiance * pi * area / (4 N) makes
        // the density estimate reproduce L = M/pi regardless of N. (Derivation: the
        // gather sums power over a disc of area pi r^2, multiplies by 4/pi, divides
        // by pi r^2, with identity BRDF; a uniform areal power density rho yields
        // L = (4/pi) rho, so rho = radiance * pi / 4, i.e. total patch power =
        // radiance * pi * area / 4 spread over N deposits.)
        std::vector<Vector> samples;
        samples.reserve(static_cast<size_t>(countU) * static_cast<size_t>(countV));
        for (int iu = 0; iu < countU; ++iu)
        {
            const double u = -extentU + (iu + 0.5) * stepU;
            for (int iv = 0; iv < countV; ++iv)
            {
                const double v = -extentV + (iv + 0.5) * stepV;
                if (patch.isDisc && (u * u + v * v) > (patch.radius * patch.radius))
                {
                    continue;
                }
                samples.push_back(patch.center + patch.right * u + patch.up * v);
            }
        }
        if (samples.empty())
        {
            continue;
        }
        result.generated += samples.size();

        const double perDepositScale =
            (Utility::pi * area) / (4.0 * static_cast<double>(samples.size()));
        const Color perDepositPower = patch.radiance * static_cast<float>(perDepositScale);

        // incoming direction is irrelevant for an emitter (identity BRDF), but a
        // valid unit vector keeps the record well-formed; store the patch normal.
        for (const Vector& position : samples)
        {
            if (!probeIndex.anyWithinKeepRadius(position))
            {
                continue;  // no camera path lands here; the deposit can't be gathered
            }
            const RawBounce record{position, patch.normal, patch.normal, perDepositPower};
            if (store.append(record))
            {
                ++result.kept;
            }
        }
    }
    return result;
}

// ===== Unified gather =====

namespace
{

struct Context
{
    const std::vector<std::shared_ptr<Object>>& objects;
    const BounceStore& store;
    const MaterialLibrary& materials;
    const AnimationQuery* animation;
    std::vector<EmitterPatch> patches;
    double pixelHalfAngle;
    double minGatherRadius;
};

// Density estimate of the radiance leaving a non-delta surface point toward the
// viewer: sum BRDF(incoming, wo) * power over the raw bounces within the gather
// footprint, divided by the gather AREA. The photon-mapping density estimate
//   L_o = (1/ΔA) Σ_p f(wi_p, wo) Φ_p
// has NO cos(theta_view) term — the photons already carry the incoming geometry
// (cos_theta_i folded into the deposit), the BRDF f handles the view direction,
// and ΔA is the surface area the gather covers.
//
// `footprintRadius` is the world-space radius of the gather disc ON the surface.
// It is supplied by the caller as a RAY DIFFERENTIAL: the spacing on the surface
// between this pixel's hit and an adjacent pixel's hit. Using the real per-pixel
// surface footprint (rather than a constant angular footprint) is load-bearing
// for brightness parity with the retired splat AND for matching it across the
// frame: a rectilinear camera's pixels subtend different solid angles toward the
// edges and project onto tilted surfaces with foreshortening, so a CONSTANT
// half-angle footprint over/under-counts at frame edges and on grazing surfaces
// (measured: side walls +20%, corners +40% vs the splat). The differential
// footprint is exactly the surface area one pixel covers, so gathering over it
// and dividing by it reproduces the splat's "energy per pixel" by construction —
// the cos(theta)/projection/edge factors all fall out automatically. The disc is
// gathered with a radius covering the pixel footprint (so adjacent discs tile the
// surface, losing no energy) and the normalization divides by that SAME area, so
// the estimate stays unbiased.
Color gatherRadiance(const Context& ctx,
                     const Hit& hit,
                     const std::shared_ptr<Material>& material,
                     const Vector& viewer,
                     double footprintRadius,
                     size_t& outDeposits)
{
    outDeposits = 0;

    // An emitter hit has no MaterialLibrary entry: it is gathered with an IDENTITY
    // BRDF (f = 1) over its own radiance deposits, reproducing its view-independent
    // surface radiance L = M/pi. Any other hit uses its material's BRDF.
    const bool isEmitter = (hit.material == kEmitterMaterial);

    const Vector toViewer = viewer - hit.position;
    const double toViewerMag = toViewer.magnitude();
    if (toViewerMag <= kSelfHitThreshold)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }
    const Vector wo = toViewer / toViewerMag;

    if (Vector::dot(wo, hit.normal) <= 0.0)
    {
        return Color{0.0f, 0.0f, 0.0f};  // surface faces away from the viewer
    }

    const double r = Utility::flooredSplatRadius(footprintRadius, ctx.minGatherRadius);
    if (r <= 0.0)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }

    const std::vector<std::size_t> neighbors = ctx.store.radiusSearch(hit.position, r);
    if (neighbors.empty())
    {
        return Color{0.0f, 0.0f, 0.0f};
    }

    // Leak suppression by NORMAL AGREEMENT (not a hard tangent-plane distance cut).
    // The radius search returns every deposit inside a Euclidean SPHERE of radius
    // r, which near a corner/edge also catches deposits on an ADJACENT
    // perpendicular surface (a wall gather pulling in floor/ceiling deposits) — the
    // classic photon-map boundary bias / light leak. The old fix rejected any
    // deposit whose perpendicular distance from the hit's tangent plane exceeded a
    // small fraction of r; on a CURVED surface that over-rejected legitimate
    // deposits near the silhouette (their positions bow off the local tangent
    // plane), darkening the silhouette into a black rim.
    //
    // Instead, keep a deposit when its SURFACE NORMAL agrees with the gather
    // point's normal: dot(depositNormal, hitNormal) >= kNormalAgree. A perpendicular
    // adjacent wall has dot ~= 0 (rejected — leak still stopped); a smoothly curved
    // same-surface neighborhood stays within a moderate cone of the hit normal
    // (kept — no rim). A generous tangent-plane band (a multiple of r) is retained
    // only as a coarse backstop against a far co-normal surface (e.g. a parallel
    // facing wall across a thin gap) sneaking into the sphere; it is loose enough
    // never to clip a curved same-surface neighborhood.
    const UnitVector hitNormal = UnitVector::alreadyNormalized(hit.normal);
    constexpr double kNormalAgree = 0.5;   // cos 60°: same-surface vs perpendicular
    const double planeBand = 2.0 * r;      // loose backstop only
    Color sum{0.0f, 0.0f, 0.0f};
    size_t kept = 0;
    for (const std::size_t index : neighbors)
    {
        const RawBounce& record = ctx.store[index];

        if (isEmitter)
        {
            // Emitter deposits carry the patch normal; only gather deposits whose
            // normal matches THIS emitter face (rejects a second fixture's deposits
            // sneaking into the sphere). f = 1.
            const Vector dn = record.normal();
            if (Vector::dot(dn, hit.normal) < kNormalAgree)
            {
                continue;
            }
            sum += record.power;
            ++kept;
            continue;
        }

        const Vector dn = record.normal();
        const double dnLen = dn.magnitude();
        if (dnLen > kSelfHitThreshold)
        {
            if (Vector::dot(dn / dnLen, hit.normal) < kNormalAgree)
            {
                continue;  // deposit is on a differently-oriented (adjacent) surface
            }
        }
        const double planeDist =
            std::abs(Vector::dot(record.position() - hit.position, hit.normal));
        if (planeDist > planeBand)
        {
            continue;  // far co-normal surface across a gap; coarse backstop
        }
        const Vector wi = -record.incoming();  // direction the bounce photon came from
        const Color f = material->evaluate(wi, wo, hitNormal);
        sum += f * record.power;
        ++kept;
    }
    outDeposits = kept;
    if (kept == 0)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }

    // Divide by the gather disc area. The disc radius r is HALF the on-surface
    // pixel spacing (ray differential), so the disc is INSCRIBED in the pixel's
    // surface square (area = spacing²): the disc covers π r² = π/4 · spacing² of
    // it. The retired splat binned the WHOLE pixel square and normalized by π r²
    // — i.e. it is 4/π brighter than a pure density estimate over the inscribed
    // disc. The splat is the parity target (the shipped image), so multiply by
    // 4/π to reproduce its energy-per-pixel. The ray-differential footprint makes
    // this factor a single SCALE constant rather than a geometry-dependent one,
    // which is why the per-region spread collapses with it (vs a constant-angle
    // footprint, which left frame-edge variation).
    const double kSplatParity = 4.0 / Utility::pi;
    const double area = Utility::pi * r * r;
    const float scale = static_cast<float>(kSplatParity / area);
    return sum * scale;
}

// World-space surface footprint radius of a pixel via a RAY DIFFERENTIAL: cast
// the ray for an adjacent pixel, intersect the SAME surface plane (the hit's
// tangent plane), and return half the on-surface distance between the two hits.
// This is the exact per-pixel footprint — distortion- and foreshortening-correct
// — used as the gather radius. Falls back to the constant-angle estimate if the
// differential ray is degenerate (parallel to the plane / behind the camera).
double pixelFootprintRadius(const Context& ctx,
                            const Camera& camera,
                            const PixelCoords& coord,
                            const Hit& hit,
                            double fallbackDistance)
{
    const Vector n = hit.normal;
    // Adjacent pixel: +1 in x where possible, else -1 (frame's right edge).
    const size_t nx = (coord.x + 1 < camera.width()) ? coord.x + 1 : (coord.x - 1);
    const PixelCoords adj{nx, coord.y};
    const Ray adjRay = camera.generatePrimaryRay(adj);

    // Perpendicular (constant-angle) footprint at this depth.
    const double perpRadius = fallbackDistance * std::tan(ctx.pixelHalfAngle);

    const double denom = Vector::dot(adjRay.direction, n);
    if (std::abs(denom) > kSelfHitThreshold)
    {
        const double t = Vector::dot(hit.position - adjRay.origin, n) / denom;
        if (t > kSelfHitThreshold)
        {
            const Vector adjHit = adjRay.origin + adjRay.direction * t;
            const double diffRadius = 0.5 * (adjHit - hit.position).magnitude();
            // The gather radius is the SMALLER of the ray-differential spacing and
            // the perpendicular footprint. Why the min:
            //   - At a GRAZING surface (a wall's far end seen near the frame edge)
            //     the differential spacing DIVERGES (adjacent hits land far apart),
            //     which over-blurs and washes out the real lighting GRADIENT along
            //     the wall (measured: the wall's bright-toward-the-corner falloff
            //     flattened to near-uniform). The smaller perpendicular footprint
            //     keeps the gather tight and the gradient sharp there.
            //   - Where the differential is SMALLER than the perpendicular (e.g.
            //     rectilinear edge compression), it correctly tightens the disc.
            // Taking the min keeps the disc no larger than either estimate, so the
            // gather stays sharp; the noise cost of a small disc is the accepted
            // tradeoff (more photons clean it, linear cost).
            if (diffRadius > 0.0)
            {
                return std::min(diffRadius, perpRadius);
            }
        }
    }
    // Fallback: constant angular footprint at the hit depth.
    return perpRadius;
}

// Follow an already-extended (reflected/refracted) ray: at a delta surface extend
// and recurse; at a non-delta surface gather the raw bounces. `depth` bounds
// specular recursion; `pathLength` is the accumulated camera-to-here distance
// along the specular chain, used to size the reflected gather footprint. Returns
// the radiance the ray brings back toward the original camera.
Color shade(const Context& ctx,
            const Vector& origin,
            const Vector& direction,
            int depth,
            double pathLength,
            std::vector<Hit>& castBuffer,
            RandomGenerator& generator,
            size_t& outDeposits)
{
    if (depth >= kMaxSpecularDepth)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }

    const Ray ray{origin, direction};
    std::optional<Hit> hit =
        firstHit(ctx.objects, ray, castBuffer, 0.0f, ctx.animation, &ctx.patches);
    if (!hit)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }

    const bool isEmitter = (hit->material == kEmitterMaterial);
    std::shared_ptr<Material> material =
        isEmitter ? nullptr : ctx.materials.fetchByIndex(hit->material);
    if (!isEmitter && !material)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }

    const double segment = hit->distance;

    if (!isEmitter && material->isDelta())
    {
        const UnitVector hitNormal = UnitVector::alreadyNormalized(hit->normal);
        const BSDFSample s = material->sample(direction, hitNormal, generator);
        if (!s.valid)
        {
            return Color{0.0f, 0.0f, 0.0f};
        }
        const Vector nextDir = Vector::normalized(s.direction);
        const Vector nextOrigin = hit->position + nextDir * kReflectionEpsilon;
        const Color child = shade(ctx, nextOrigin, nextDir, depth + 1,
                                  pathLength + segment, castBuffer, generator, outDeposits);
        return s.weight * child;
    }

    // Non-delta surface reached through a specular chain: gather the raw bounces.
    // The viewer for the BRDF is the last specular vertex (origin) — the correct
    // outgoing direction to evaluate the reflected surface's BRDF toward.
    //
    // Footprint: a mirror is an UNFOLDED straight path, so the perpendicular
    // footprint at the reflected surface is the pixel half-angle projected over the
    // TOTAL path length — exactly the direct-view perpendicular footprint at that
    // unfolded depth. This is the same quantity the DIRECT path caps the gather
    // radius at (pixelFootprintRadius's perpRadius = depth*tan(halfAngle)), so it
    // keeps reflections as crisp as the direct view.
    //
    // The previous code then DIVIDED this by cos(view) (clamped to 0.15), inflating
    // the disc up to ~6.7x at a grazing reflected surface — which is what made the
    // reflected ceiling/floor blurrier than the direct view (Bug 3). The direct
    // path applies NO such cos(view) inflation (it relies on the perpendicular /
    // ray-differential footprint), and brightness parity does NOT depend on it: the
    // density estimate divides by the SAME area it gathers over, so changing r only
    // trades sharpness against noise, not energy. The foreshortening enlargement is
    // therefore capped tightly (at most 2x, matching a moderate grazing angle)
    // rather than the old 6.7x, keeping the reflected footprint near the direct
    // perpendicular footprint.
    const Vector toViewer = origin - hit->position;
    const double toViewerMag = toViewer.magnitude();
    double footprint = (pathLength + segment) * std::tan(ctx.pixelHalfAngle);
    if (toViewerMag > kSelfHitThreshold)
    {
        const double cosView =
            Vector::dot(toViewer / toViewerMag, hit->normal);
        if (cosView > 0.0)
        {
            footprint /= std::min(1.0, std::max(0.5, cosView));
        }
    }
    return gatherRadiance(ctx, *hit, material, origin, footprint, outDeposits);
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

            const bool dofActive = (camera.projection() == Camera::Projection::RealLens);
            const int primarySamples = dofActive ? kCameraSamplesPerPixel : 1;

            Color pixelAccum{0.0f, 0.0f, 0.0f};
            int validSamples = 0;
            bool anyHit = false;
            bool firstSurfaceDelta = false;
            size_t depositsThisPixel = 0;

            for (int primary = 0; primary < primarySamples; ++primary)
            {
                const Ray ray = dofActive
                    ? camera.generatePrimaryRay(coord, &generator)
                    : camera.generatePrimaryRay(coord);

                std::optional<Hit> hit =
                    firstHit(ctx.objects, ray, castBuffer, 0.0f, ctx.animation, &ctx.patches);
                if (!hit)
                {
                    ++stats.pixelsMiss;
                    continue;
                }
                anyHit = true;

                const bool isEmitter = (hit->material == kEmitterMaterial);
                std::shared_ptr<Material> material =
                    isEmitter ? nullptr : ctx.materials.fetchByIndex(hit->material);
                if (!isEmitter && !material)
                {
                    continue;
                }

                if (isEmitter || !material->isDelta())
                {
                    // Direct non-delta pixel: gather at extension depth 0. The
                    // viewer is the camera eye (ray origin); the footprint is the
                    // exact per-pixel surface area via a ray differential against
                    // the adjacent pixel (distortion- and foreshortening-correct).
                    const double footprint =
                        pixelFootprintRadius(ctx, camera, coord, *hit, hit->distance);
                    size_t deposits = 0;
                    const Color radiance =
                        gatherRadiance(ctx, *hit, material, ray.origin, footprint, deposits);
                    pixelAccum += radiance;
                    depositsThisPixel += deposits;
                    ++validSamples;
                    continue;
                }

                // Delta first surface: glass is stochastic so average extra
                // extension samples; a mirror is deterministic (single sample).
                firstSurfaceDelta = true;
                const bool stochasticDelta =
                    (dynamic_cast<DielectricMaterial*>(material.get()) != nullptr);
                const int extensionSamples =
                    (stochasticDelta && !dofActive) ? kCameraSamplesPerPixel : 1;

                const UnitVector hitNormal = UnitVector::alreadyNormalized(hit->normal);
                Color accumulated{0.0f, 0.0f, 0.0f};
                int extValid = 0;
                for (int sample = 0; sample < extensionSamples; ++sample)
                {
                    const BSDFSample s = material->sample(ray.direction, hitNormal, generator);
                    if (!s.valid)
                    {
                        continue;
                    }
                    const Vector nextDir = Vector::normalized(s.direction);
                    const Vector nextOrigin = hit->position + nextDir * kReflectionEpsilon;
                    size_t deposits = 0;
                    accumulated += s.weight * shade(ctx, nextOrigin, nextDir, /*depth=*/1,
                                                    /*pathLength=*/hit->distance,
                                                    castBuffer, generator, deposits);
                    depositsThisPixel += deposits;
                    ++extValid;
                }
                if (extValid > 0)
                {
                    pixelAccum += accumulated * (1.0f / static_cast<float>(extValid));
                    ++validSamples;
                }
            }

            if (anyHit)
            {
                ++stats.pixelsHit;
                if (firstSurfaceDelta)
                {
                    ++stats.pixelsDelta;
                }
            }

            if (validSamples == 0)
            {
                continue;
            }

            const Color pixel = pixelAccum * (1.0f / static_cast<float>(validSamples));
            if (pixel.red == 0.0f && pixel.green == 0.0f && pixel.blue == 0.0f)
            {
                continue;
            }

            buffer.addColor(coord, pixel);
            ++stats.pixelsGathered;
            stats.depositsAccum += depositsThisPixel;
            const double peak = std::max({static_cast<double>(pixel.red),
                                          static_cast<double>(pixel.green),
                                          static_cast<double>(pixel.blue)});
            if (peak > stats.maxRadiance)
            {
                stats.maxRadiance = peak;
            }
            stats.sumRadiance +=
                0.2126 * pixel.red + 0.7152 * pixel.green + 0.0722 * pixel.blue;
        }
    }
}

}  // namespace

Result run(const std::vector<std::shared_ptr<Object>>& objects,
           const std::shared_ptr<Camera>& camera,
           const BounceStore& store,
           const MaterialLibrary& materials,
           const AnimationQuery* animation,
           size_t workerCount,
           double minGatherRadius,
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

    const double pixelHalfAngle =
        0.5 * Utility::radians(camera->verticalFieldOfView()) / static_cast<double>(height);

    const Context ctx{objects,        store,          materials,       animation,
                      collectEmitterPatches(objects), pixelHalfAngle, minGatherRadius};

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
        result.pixelsHit += s.pixelsHit;
        result.pixelsGathered += s.pixelsGathered;
        result.pixelsDelta += s.pixelsDelta;
        result.pixelsMiss += s.pixelsMiss;
        result.maxRadiance = std::max(result.maxRadiance, s.maxRadiance);
        result.sumRadiance += s.sumRadiance;
        result.depositsAccum += s.depositsAccum;
    }
    return result;
}

}  // namespace ProbeGather
