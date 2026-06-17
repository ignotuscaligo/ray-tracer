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

// Gather temporal window: a deposit is gathered at a camera ray time `tCam` only if
// |deposit.time - tCam| <= halfWindow. The window is sized to the SHUTTER SPAN so:
//   - STATIC geometry keeps exact brightness parity: a static surface's deposits sit
//     at one world position regardless of their (shutter-spread) photon times, and a
//     camera ray at any time within the shutter is within `shutter` of every one of
//     them, so none is rejected (the time filter is inert on static geometry — the
//     pre-animation baseline is unchanged).
//   - MOVING geometry self-filters SPATIALLY: a deposit from a photon at a time far
//     from tCam was laid down where the object was THEN — a different world position
//     — so the radius search already excludes it. The temporal window is the
//     backstop against two distinct poses that happen to overlap within the gather
//     radius bleeding together (it bounds how far apart in time co-located deposits
//     may be gathered). A zero shutter collapses the window to ~0 (single instant).
// Sizing it to the full shutter (not a tight fraction) is the conservative choice
// that guarantees static parity; the blur itself comes from the per-camera-sample
// random shutter time integrating poses, not from a tight temporal cut.
constexpr float kEmitterTimeless = RawBounce::kTimelessDeposit;

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

// ===== Probe pass = single camera-side specular tracer =====

namespace
{

// Result of walking a camera ray through the delta chain to its first non-delta
// hit, accumulating throughput and path length along the way.
struct ExtendResult
{
    Hit hit;                       // the first non-delta (or emitter) surface reached
    Color throughput{1.0f, 1.0f, 1.0f};  // product of delta BSDF weights to reach it
    double unfoldedPathLength = 0.0;     // total camera-to-hit distance along the chain
    Vector finalDirection{};       // direction of the last segment (the ray that hit the
                                   //   non-delta surface); wo = -finalDirection
    bool traversedDelta = false;   // passed through at least one delta surface
    bool valid = false;            // false => chain escaped or exceeded the depth cap
};

// Extend a ray through delta surfaces to the first non-delta hit, ACCUMULATING the
// product of the delta BSDF weights (the specular throughput) and the unfolded path
// length. This is the irreducible camera-side specular trace (a delta vs a point
// camera is measure-zero — a mirror must be TRACED, not gathered; DESIGN §6b). It
// runs ONCE here in the probe pass; the gather does no extension.
ExtendResult extendAndRecord(const std::vector<std::shared_ptr<Object>>& objects,
                             const MaterialLibrary& materials,
                             const AnimationQuery* animation,
                             std::vector<Hit>& castBuffer,
                             RandomGenerator& generator,
                             Ray ray,
                             float time,
                             const std::vector<EmitterPatch>& patches)
{
    ExtendResult out;
    for (int depth = 0; depth < kMaxSpecularDepth; ++depth)
    {
        std::optional<Hit> hit = firstHit(objects, ray, castBuffer, time, animation, &patches);
        if (!hit)
        {
            return out;  // escaped: invalid
        }
        out.unfoldedPathLength += hit->distance;

        if (hit->material == kEmitterMaterial)
        {
            out.hit = *hit;  // emitter surface: a non-delta gatherable surface
            out.finalDirection = ray.direction;
            out.valid = true;
            return out;
        }
        std::shared_ptr<Material> material = materials.fetchByIndex(hit->material);
        if (!material)
        {
            return out;
        }
        if (!material->isDelta())
        {
            out.hit = *hit;  // reached the first non-delta surface
            out.finalDirection = ray.direction;
            out.valid = true;
            return out;
        }

        // Delta surface: follow the (deterministic for mirror, stochastic for
        // glass) outgoing direction, fold its BSDF weight into the throughput, and
        // continue. Folding the weight here is the fix for the old probe pass, which
        // DISCARDED s.weight and kept only the endpoint — the gather had to re-walk
        // the chain to recover it. Now the throughput rides the record.
        out.traversedDelta = true;
        const UnitVector hitNormal = UnitVector::alreadyNormalized(hit->normal);
        const BSDFSample s = material->sample(ray.direction, hitNormal, generator);
        if (!s.valid)
        {
            return out;
        }
        out.throughput = out.throughput * s.weight;
        const Vector nextDir = Vector::normalized(s.direction);
        ray = Ray{hit->position + nextDir * kReflectionEpsilon, nextDir};
    }
    return out;  // exceeded specular depth: invalid (hall of mirrors -> black)
}

}  // namespace

// The gather-geometry helpers (pixelFootprintRadius / reflectedFootprintRadius) and
// the density estimate (gatherRadiance) are declared in ProbeGather.h's `testing`
// namespace and DEFINED below (hoisted out of the anonymous namespace so unit tests
// call the production code directly). The probe pass + gather call sites use those
// same definitions — no behavior change.

ProbeResult collectGatherPoints(const std::vector<std::shared_ptr<Object>>& objects,
                                const Camera& camera,
                                const MaterialLibrary& materials,
                                const AnimationQuery* animation,
                                float frameTime,
                                float shutterTime,
                                int cameraSamples,
                                size_t subSample,
                                long long seed)
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

    const double pixelHalfAngle =
        0.5 * Utility::radians(camera.verticalFieldOfView()) / static_cast<double>(height);

    const float shutterSpan = std::max(0.0f, shutterTime);
    const bool motionActive = (shutterSpan > 0.0f) && (cameraSamples > 1);
    // DOF is active only for a RealLens camera with a NON-ZERO effective aperture.
    // A RealLens camera whose effective aperture is 0 is geometrically a pinhole
    // (every aperture sample collapses to the eye, the ray reduces to the perspective
    // pinhole ray), so it MUST take the same single-sample, pixel-center, no-generator
    // path as a perspective camera — that is the "DOF off == pinhole, byte-for-byte"
    // invariant. Gating on the effective aperture (not just the projection enum) is
    // what makes a zero-aperture reallens camera a true pinhole control.
    const bool dofActive = (camera.projection() == Camera::Projection::RealLens) &&
                           (camera.effectiveApertureRadius() > 0.0);

    // The camera-side specular trace is camera-ray-scale and cheap; run it
    // single-threaded so the record list (and its diagnostics) accumulate without
    // cross-thread merging. For very large frames this could be parallelized like
    // the gather; not needed at current resolutions.
    std::vector<Hit> castBuffer;
    // Deterministic test mode seeds the camera-side sampling RNG; production passes
    // seed < 0 -> the random_device-seeded default ctor.
    RandomGenerator generator = (seed >= 0)
        ? RandomGenerator(static_cast<std::uint32_t>(seed))
        : RandomGenerator();

    for (size_t y = 0; y < height; y += stride)
    {
        for (size_t x = 0; x < width; x += stride)
        {
            const PixelCoords coord{x, y};

            // This pixel's camera samples mirror the former gather loop exactly so the
            // records are an unbiased camera-side estimate: DOF aperture samples
            // (RealLens) and/or random shutter-time samples (finite shutter); each
            // draws a sample time and generates its ray at that time (camera pose
            // resolved at the time — §9e). A dielectric first hit then fans into
            // kCameraSamplesPerPixel stochastic Fresnel picks (unless DOF already
            // multisamples); a mirror is one deterministic extension.
            const int motionSamples = motionActive ? cameraSamples : 1;
            const int dofSamples = dofActive ? kCameraSamplesPerPixel : 1;
            const int primarySamples = std::max(dofSamples, motionSamples);

            // Stage the surviving records for THIS pixel so the sampleWeight (1/N
            // over the pixel's surviving samples) can be filled once N is known.
            const size_t pixelRecordBegin = result.points.size();

            for (int primary = 0; primary < primarySamples; ++primary)
            {
                const float sampleTime =
                    motionActive
                        ? frameTime + static_cast<float>(generator.value(shutterSpan))
                        : frameTime;

                const Ray ray = dofActive
                    ? camera.generatePrimaryRayAt(coord, sampleTime, animation, &generator)
                    : camera.generatePrimaryRayAt(coord, sampleTime, animation);
                ++result.cameraRays;

                std::optional<Hit> firstSurface =
                    firstHit(objects, ray, castBuffer, sampleTime, animation, &patches);
                if (!firstSurface)
                {
                    ++result.misses;
                    continue;
                }

                const bool firstIsEmitter = (firstSurface->material == kEmitterMaterial);
                std::shared_ptr<Material> firstMat =
                    firstIsEmitter ? nullptr
                                   : materials.fetchByIndex(firstSurface->material);
                if (!firstIsEmitter && !firstMat)
                {
                    continue;
                }

                // DIRECT non-delta first hit: emit a depth-0 record with identity
                // throughput and the ray-differential footprint (distortion- and
                // foreshortening-correct).
                if (firstIsEmitter || !firstMat->isDelta())
                {
                    GatherPoint gp;
                    gp.pixel = coord;
                    gp.position = firstSurface->position;
                    gp.normal = firstSurface->normal;
                    gp.viewDir = Vector::normalized(ray.origin - firstSurface->position);
                    gp.materialIndex = firstSurface->material;
                    gp.specularThroughput = Color{1.0f, 1.0f, 1.0f};
                    gp.unfoldedPathLength = firstSurface->distance;
                    gp.footprintRadius = testing::pixelFootprintRadius(
                        camera, animation, pixelHalfAngle, coord, *firstSurface,
                        firstSurface->distance, sampleTime);
                    gp.sampleTime = sampleTime;
                    gp.sampleWeight = 1.0f;  // filled below
                    result.points.push_back(gp);
                    continue;
                }

                // DELTA first hit: extend through the specular chain. Glass is
                // stochastic so fan into extra Fresnel picks; a mirror is
                // deterministic (single pick).
                const bool stochasticDelta =
                    (dynamic_cast<DielectricMaterial*>(firstMat.get()) != nullptr);
                const int extensionSamples =
                    (stochasticDelta && !dofActive) ? kCameraSamplesPerPixel : 1;

                for (int ext = 0; ext < extensionSamples; ++ext)
                {
                    const ExtendResult chain = extendAndRecord(
                        objects, materials, animation, castBuffer, generator, ray,
                        sampleTime, patches);
                    if (chain.traversedDelta)
                    {
                        ++result.deltaExtensions;
                    }
                    if (!chain.valid)
                    {
                        ++result.misses;
                        continue;
                    }
                    // wo at a reflected surface is back along the final segment
                    // toward the last specular vertex (= -finalDirection): the same
                    // viewer the old shade() evaluated the reflected BRDF toward.
                    const Vector reflectedView = -chain.finalDirection;
                    GatherPoint gp;
                    gp.pixel = coord;
                    gp.position = chain.hit.position;
                    gp.normal = chain.hit.normal;
                    gp.viewDir = Vector::normalized(reflectedView);
                    gp.materialIndex = chain.hit.material;
                    gp.specularThroughput = chain.throughput;
                    gp.unfoldedPathLength = chain.unfoldedPathLength;
                    gp.footprintRadius = testing::reflectedFootprintRadius(
                        pixelHalfAngle, chain.unfoldedPathLength, reflectedView,
                        chain.hit);
                    gp.sampleTime = sampleTime;
                    gp.sampleWeight = 1.0f;  // filled below
                    result.points.push_back(gp);
                }
            }

            // sampleWeight = 1 / (surviving samples for this pixel): the average over
            // the pixel's DOF/shutter/Fresnel samples. A pixel whose every sample
            // missed contributes no records (and no weight).
            const size_t survived = result.points.size() - pixelRecordBegin;
            if (survived > 0)
            {
                const float w = 1.0f / static_cast<float>(survived);
                for (size_t i = pixelRecordBegin; i < result.points.size(); ++i)
                {
                    result.points[i].sampleWeight = w;
                }
            }
        }
    }
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

// ===== Unified gather = PURE COLLECTION =====

namespace
{

// Everything the pure-collection gather needs that is NOT carried per-record: the
// store to density-estimate, the materials for the BRDF, the radius floor, and the
// temporal window. No objects, no camera, no animation — the gather casts no rays.
struct Context
{
    const BounceStore& store;
    const MaterialLibrary& materials;
    double minGatherRadius;
    float timeHalfWindow;  // gather temporal window half-width (shutter-sized)
};

}  // namespace

// Density estimate of the radiance leaving a non-delta surface point toward the
// viewer: sum BRDF(incoming, wo) * power over the raw bounces within the gather
// footprint, divided by the gather AREA. The photon-mapping density estimate
//   L_o = (1/ΔA) Σ_p f(wi_p, wo) Φ_p
// has NO cos(theta_view) term — the photons already carry the incoming geometry
// (cos_theta_i folded into the deposit), the BRDF f handles the view direction,
// and ΔA is the surface area the gather covers.
//
// `wo` is the unit direction from the hit toward the viewer (precomputed by the
// probe pass and carried on the record): the camera eye for a direct hit, the last
// specular vertex for a reflected one. `footprintRadius` is the world-space radius
// of the gather disc ON the surface — a RAY DIFFERENTIAL (the spacing on the
// surface between this pixel's hit and an adjacent pixel's hit) for a direct hit,
// the unfolded-path perpendicular footprint for a reflected hit, also precomputed
// by the probe pass. Using the real per-pixel surface footprint (rather than a
// constant angular footprint) is load-bearing for brightness parity with the
// retired splat AND for matching it across the frame: a rectilinear camera's pixels
// subtend different solid angles toward the edges and project onto tilted surfaces
// with foreshortening, so a CONSTANT half-angle footprint over/under-counts at frame
// edges and on grazing surfaces (measured: side walls +20%, corners +40% vs the
// splat). The differential footprint is exactly the surface area one pixel covers,
// so gathering over it and dividing by it reproduces the splat's "energy per pixel"
// by construction — the cos(theta)/projection/edge factors all fall out
// automatically. The disc is gathered with a radius covering the pixel footprint (so
// adjacent discs tile the surface, losing no energy) and the normalization divides
// by that SAME area, so the estimate stays unbiased.
Color testing::gatherRadiance(const BounceStore& store,
                              const MaterialLibrary& materials,
                              const Hit& hit,
                              const std::shared_ptr<Material>& material,
                              const Vector& wo,
                              double footprintRadius,
                              double minGatherRadius,
                              float rayTime,
                              float timeHalfWindow,
                              std::size_t& outDeposits)
{
    outDeposits = 0;

    // An emitter hit has no MaterialLibrary entry: it is gathered with an IDENTITY
    // BRDF (f = 1) over its own radiance deposits, reproducing its view-independent
    // surface radiance L = M/pi. Any other hit uses its material's BRDF.
    const bool isEmitter = (hit.material == kEmitterMaterial);
    (void)materials;  // BRDF arrives via `material`; kept in the signature for symmetry.

    if (Vector::dot(wo, hit.normal) <= 0.0)
    {
        return Color{0.0f, 0.0f, 0.0f};  // surface faces away from the viewer
    }

    const double r = Utility::flooredSplatRadius(footprintRadius, minGatherRadius);
    if (r <= 0.0)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }

    const std::vector<std::size_t> neighbors = store.radiusSearch(hit.position, r);
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
        const RawBounce& record = store[index];

        // TEMPORAL WINDOW. Keep a deposit only if its photon time is within the
        // gather's half-window of this camera ray's time. A timeless deposit (an
        // emitter patch, time == +inf) always passes. On a STATIC surface every
        // co-located deposit is within the shutter-sized window of any camera ray
        // time, so none is dropped (exact baseline). On a MOVING surface a deposit
        // from a far-off time was laid down at a different position and is already
        // outside the spatial radius — this is the backstop for two poses that
        // overlap spatially within the gather radius.
        if (record.time != kEmitterTimeless &&
            std::abs(record.time - rayTime) > timeHalfWindow)
        {
            continue;
        }

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
// — used as the gather radius for a DIRECT (depth-0) hit. Falls back to the
// constant-angle estimate if the differential ray is degenerate (parallel to the
// plane / behind the camera). Computed in the PROBE PASS (the adjacent-pixel ray it
// needs is a camera ray, available there), then stored on the record so the gather
// does no geometry. The only ray here is the adjacent-pixel CAMERA ray; it is
// intersected analytically against the hit's tangent PLANE, not cast into the scene.
double testing::pixelFootprintRadius(const Camera& camera,
                                     const AnimationQuery* animation,
                                     double pixelHalfAngle,
                                     const PixelCoords& coord,
                                     const Hit& hit,
                                     double fallbackDistance,
                                     float rayTime)
{
    const Vector n = hit.normal;
    // Adjacent pixel: +1 in x where possible, else -1 (frame's right edge). When the
    // frame is 1px wide both branches would underflow, so clamp to the same pixel
    // (degenerate differential -> perpendicular fallback below).
    const size_t nx = (coord.x + 1 < camera.width()) ? coord.x + 1
                      : (coord.x > 0)                 ? coord.x - 1
                                                      : coord.x;
    const PixelCoords adj{nx, coord.y};
    // Generate the differential ray at the SAME sample time as the primary ray, so
    // an animated camera's footprint is measured from its pose at `rayTime` (matches
    // the primary ray's origin/orientation). Static camera => same pose as before.
    const Ray adjRay = camera.generatePrimaryRayAt(adj, rayTime, animation);

    // Perpendicular (constant-angle) footprint at this depth.
    const double perpRadius = fallbackDistance * std::tan(pixelHalfAngle);

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

// World-space footprint radius for a REFLECTED (specularly-extended) hit. A mirror
// is an UNFOLDED straight path, so the perpendicular footprint at the reflected
// surface is the pixel half-angle projected over the TOTAL unfolded path length —
// the same quantity the direct path caps its gather radius at, keeping reflections
// as crisp as the direct view. [INVARIANT, §6f] It is NOT inflated by 1/cos(view):
// an earlier `/min(1, max(0.15, cosView))` blew the disc up to ~6.7x at a grazing
// reflected surface (reflected ceiling/floor blurrier than direct); the
// foreshortening enlargement is capped at 2x (`max(0.5, cosView)`). Brightness
// parity does NOT depend on this (the density estimate divides by the same area it
// gathers, so r trades sharpness for noise, not energy). `viewer` is the unit
// direction from the hit toward the last specular vertex (= the record's viewDir).
double testing::reflectedFootprintRadius(double pixelHalfAngle,
                                         double unfoldedPathLength,
                                         const Vector& viewer,
                                         const Hit& hit)
{
    const double viewerMag = viewer.magnitude();
    double footprint = unfoldedPathLength * std::tan(pixelHalfAngle);
    if (viewerMag > kSelfHitThreshold)
    {
        const double cosView = Vector::dot(viewer / viewerMag, hit.normal);
        if (cosView > 0.0)
        {
            footprint /= std::min(1.0, std::max(0.5, cosView));
        }
    }
    return footprint;
}

namespace
{

// PURE COLLECTION over a contiguous slice of this camera's GatherPoint records. For
// each record: rebuild its Hit, fetch its material, density-estimate the retained
// raw bounces at its position over its precomputed footprint, multiply by its
// specular throughput and 1/N sample weight, and ADD into its pixel. `buffer`
// accumulation (atomic) does the per-pixel averaging, so records sharing a pixel may
// be split across threads. No ray casting, no extension — the trace already happened
// in the probe pass.
void gatherRecords(const std::vector<GatherPoint>& points,
                   size_t begin,
                   size_t end,
                   const Context& ctx,
                   Buffer& buffer,
                   Result& stats)
{
    for (size_t i = begin; i < end; ++i)
    {
        const GatherPoint& gp = points[i];

        Hit hit;
        hit.position = gp.position;
        hit.normal = gp.normal;
        hit.material = gp.materialIndex;
        hit.distance = gp.unfoldedPathLength;

        const bool isEmitter = (gp.materialIndex == kEmitterMaterial);
        std::shared_ptr<Material> material =
            isEmitter ? nullptr : ctx.materials.fetchByIndex(gp.materialIndex);
        if (!isEmitter && !material)
        {
            continue;
        }

        std::size_t deposits = 0;
        const Color radiance = testing::gatherRadiance(
            ctx.store, ctx.materials, hit, material, gp.viewDir, gp.footprintRadius,
            ctx.minGatherRadius, gp.sampleTime, ctx.timeHalfWindow, deposits);
        const Color contribution =
            gp.specularThroughput * radiance * gp.sampleWeight;

        stats.depositsAccum += deposits;
        if (contribution.red == 0.0f && contribution.green == 0.0f &&
            contribution.blue == 0.0f)
        {
            continue;
        }
        buffer.addColor(gp.pixel, contribution);

        // Diagnostics (the per-pixel notions are approximated at record granularity:
        // each surviving record is a gathered sample; peak/sum track the per-record
        // contribution, which sums to the per-pixel value via the buffer).
        ++stats.pixelsGathered;
        const double peak = std::max({static_cast<double>(contribution.red),
                                      static_cast<double>(contribution.green),
                                      static_cast<double>(contribution.blue)});
        if (peak > stats.maxRadiance)
        {
            stats.maxRadiance = peak;
        }
        stats.sumRadiance += 0.2126 * contribution.red + 0.7152 * contribution.green +
                             0.0722 * contribution.blue;
    }
}

}  // namespace

Result run(const std::shared_ptr<Camera>& camera,
           const std::vector<GatherPoint>& points,
           const BounceStore& store,
           const MaterialLibrary& materials,
           size_t workerCount,
           double minGatherRadius,
           Buffer& buffer,
           float shutterTime)
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

    // Gather temporal half-window = the full shutter span (see kEmitterTimeless note
    // above): wide enough to never reject a static surface's shutter-spread deposits
    // (exact brightness parity), with moving surfaces self-filtering spatially. A
    // zero shutter => 0 window => only same-instant deposits, which on a static scene
    // is every deposit (all stamped at frameTime).
    const float shutterSpan = std::max(0.0f, shutterTime);

    const Context ctx{store, materials, minGatherRadius, shutterSpan};

    result.pixelsHit = points.size();  // every record reached a non-delta surface

    if (points.empty())
    {
        return result;
    }

    const size_t threads = std::max<size_t>(1, workerCount);
    const size_t effectiveThreads = std::min(threads, points.size());

    std::vector<Result> perThread(effectiveThreads);
    std::vector<std::thread> pool;
    pool.reserve(effectiveThreads);

    const size_t perThreadCount = (points.size() + effectiveThreads - 1) / effectiveThreads;

    for (size_t t = 0; t < effectiveThreads; ++t)
    {
        const size_t begin = t * perThreadCount;
        const size_t end = std::min(points.size(), begin + perThreadCount);
        if (begin >= end)
        {
            break;
        }
        pool.emplace_back([&, begin, end, t]() {
            gatherRecords(points, begin, end, ctx, buffer, perThread[t]);
        });
    }
    for (auto& thread : pool)
    {
        thread.join();
    }

    for (const auto& s : perThread)
    {
        result.pixelsGathered += s.pixelsGathered;
        result.maxRadiance = std::max(result.maxRadiance, s.maxRadiance);
        result.sumRadiance += s.sumRadiance;
        result.depositsAccum += s.depositsAccum;
    }
    return result;
}

testing::ExtendResult testing::extendToNonDelta(
    const std::vector<std::shared_ptr<Object>>& objects,
    const MaterialLibrary& materials,
    const AnimationQuery* animation,
    RandomGenerator& generator,
    const Ray& ray,
    float time)
{
    const std::vector<EmitterPatch> patches = collectEmitterPatches(objects);
    std::vector<Hit> castBuffer;
    // The anon-namespace ExtendResult lives at ProbeGather scope; qualify it so it is
    // not shadowed by testing::ExtendResult inside this testing-namespace function.
    const ProbeGather::ExtendResult chain = extendAndRecord(
        objects, materials, animation, castBuffer, generator, ray, time, patches);

    testing::ExtendResult out;
    out.hit = chain.hit;
    out.throughput = chain.throughput;
    out.unfoldedPathLength = chain.unfoldedPathLength;
    out.finalDirection = chain.finalDirection;
    out.traversedDelta = chain.traversedDelta;
    out.valid = chain.valid;
    return out;
}

}  // namespace ProbeGather
