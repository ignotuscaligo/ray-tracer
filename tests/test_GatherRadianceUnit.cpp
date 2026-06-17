#include <catch2/catch_all.hpp>

#include "BounceStore.h"
#include "Color.h"
#include "Hit.h"
#include "LambertianMaterial.h"
#include "MaterialLibrary.h"
#include "ProbeGather.h"
#include "Utility.h"
#include "Vector.h"

#include <cmath>
#include <memory>

// ============================================================================
// T6 GatherRadianceUnit — the real density estimate, called directly
// ============================================================================
//
// ProbeGather::testing::gatherRadiance (hoisted out of the .cpp anonymous namespace
// this stage) IS the per-record density estimate the production gather runs. This
// test appends hand-built RawBounces to a real BounceStore, builds its index, and
// calls the real gatherRadiance — so the normalization, the no-cos(view) rule, the
// normal-agreement leak suppression, and the temporal window are exercised in
// PRODUCTION code, not re-derived in the test body (the self-consistent-but-wrong
// trap the review flagged: test_ProbeGather's old normal-agreement case never called
// the gather).
//
// Closed-form oracle for the estimate (DESIGN §6f):
//   L_o = (4/pi) * (1/(pi r^2)) * sum_p f(wi_p, wo) * Phi_p ,  no cos(theta_view).
// For a Lambertian f = albedo/pi (both directions above the surface), so N identical
// deposits of power Phi at the gather point give
//   L = (4/pi) * (1/(pi r^2)) * N * (albedo/pi) * Phi   (per channel).

using Catch::Approx;

namespace
{
const double kPi = Utility::pi;

std::shared_ptr<MaterialLibrary> oneLambertian(const Color& albedo, size_t& outIndex)
{
    auto lib = std::make_shared<MaterialLibrary>();
    lib->add(std::make_shared<LambertianMaterial>("diffuse", albedo));
    outIndex = lib->indexForName("diffuse");
    return lib;
}

// A non-delta Hit on a +Z-facing surface at the origin.
Hit upZHit(size_t materialIndex)
{
    Hit h;
    h.position = Vector{0.0, 0.0, 0.0};
    h.normal = Vector{0.0, 0.0, 1.0};
    h.material = materialIndex;
    h.distance = 1.0;
    return h;
}
}  // namespace

TEST_CASE("T6 GatherRadianceUnit: density estimate matches the closed form", "[GatherRadianceUnit][T6]")
{
    const Color albedo{0.8f, 0.6f, 0.4f};
    size_t matIndex = 0;
    auto materials = oneLambertian(albedo, matIndex);

    // N deposits co-located at the gather point, each arriving from straight above
    // (incoming travels -Z into the surface => wi = +Z, above the surface) carrying
    // power Phi. All share the surface normal +Z (normal agreement satisfied).
    const int N = 50;
    const float phi = 3.0f;
    const double footprint = 2.0;
    BounceStore store(256);
    for (int i = 0; i < N; ++i)
    {
        const Vector pos{0.0, 0.0, 0.0};
        const Vector incoming{0.0, 0.0, -1.0};  // photon travels down into the surface
        const Vector normal{0.0, 0.0, 1.0};
        store.append(RawBounce{pos, incoming, normal, RawBounce::kTimelessDeposit,
                               Color{phi, phi, phi}});
    }
    store.buildIndex(footprint);

    const Hit hit = upZHit(matIndex);
    const Vector wo = Vector::normalized(Vector{0.0, 0.0, 1.0});  // viewer straight on
    std::size_t deposits = 0;
    const Color L = ProbeGather::testing::gatherRadiance(
        store, *materials, hit, materials->fetchByIndex(matIndex), wo, footprint,
        /*minGatherRadius=*/0.0, /*rayTime=*/0.0f, /*timeHalfWindow=*/0.0f, deposits);

    REQUIRE(deposits == static_cast<std::size_t>(N));

    // L = (4/pi) * (1/(pi r^2)) * N * (albedo/pi) * Phi, per channel.
    const double r = footprint;
    const double scale = (4.0 / kPi) * (1.0 / (kPi * r * r));
    const double expR = scale * N * (albedo.red / kPi) * phi;
    const double expG = scale * N * (albedo.green / kPi) * phi;
    const double expB = scale * N * (albedo.blue / kPi) * phi;
    INFO("L=(" << L.red << "," << L.green << "," << L.blue << ")");
    REQUIRE(L.red == Approx(expR).epsilon(1e-5));
    REQUIRE(L.green == Approx(expG).epsilon(1e-5));
    REQUIRE(L.blue == Approx(expB).epsilon(1e-5));
}

TEST_CASE("T6 GatherRadianceUnit: NO cos(theta_view) term in the estimate", "[GatherRadianceUnit][T6]")
{
    // DESIGN §6f [INVARIANT]: the density estimate divides by the on-surface gather
    // AREA only, with NO cos(theta_view). For a Lambertian (view-independent f) the
    // radiance toward a straight-on viewer and toward a grazing viewer must therefore
    // be IDENTICAL — a cos(view) factor would dim the grazing view.
    const Color albedo{0.7f, 0.7f, 0.7f};
    size_t matIndex = 0;
    auto materials = oneLambertian(albedo, matIndex);

    const double footprint = 1.5;
    BounceStore store(64);
    for (int i = 0; i < 20; ++i)
    {
        store.append(RawBounce{Vector{0, 0, 0}, Vector{0, 0, -1}, Vector{0, 0, 1},
                               RawBounce::kTimelessDeposit, Color{2.0f, 2.0f, 2.0f}});
    }
    store.buildIndex(footprint);
    const Hit hit = upZHit(matIndex);

    std::size_t d1 = 0, d2 = 0;
    const Vector woStraight = Vector::normalized(Vector{0.0, 0.0, 1.0});
    const Vector woGrazing = Vector::normalized(Vector{0.95, 0.0, 0.05});  // near-tangent
    const Color Lstraight = ProbeGather::testing::gatherRadiance(
        store, *materials, hit, materials->fetchByIndex(matIndex), woStraight,
        footprint, 0.0, 0.0f, 0.0f, d1);
    const Color Lgrazing = ProbeGather::testing::gatherRadiance(
        store, *materials, hit, materials->fetchByIndex(matIndex), woGrazing,
        footprint, 0.0, 0.0f, 0.0f, d2);

    REQUIRE(d1 == d2);
    INFO("straight=" << Lstraight.red << " grazing=" << Lgrazing.red);
    // Identical (Lambertian f is view-independent and there is no cos(view) divide).
    REQUIRE(Lgrazing.red == Approx(Lstraight.red).epsilon(1e-5));
}

TEST_CASE("T6 GatherRadianceUnit: normal-agreement rejects a perpendicular deposit (leak suppression)",
          "[GatherRadianceUnit][T6]")
{
    const Color albedo{0.9f, 0.9f, 0.9f};
    size_t matIndex = 0;
    auto materials = oneLambertian(albedo, matIndex);
    const double footprint = 2.0;

    // Same-surface deposits (normal +Z) PLUS adjacent-wall deposits (normal +X, e.g. a
    // corner leak). Only the same-surface ones must be summed.
    BounceStore store(64);
    const int same = 12;
    const int perp = 12;
    for (int i = 0; i < same; ++i)
    {
        store.append(RawBounce{Vector{0, 0, 0}, Vector{0, 0, -1}, Vector{0, 0, 1},
                               RawBounce::kTimelessDeposit, Color{1.0f, 1.0f, 1.0f}});
    }
    for (int i = 0; i < perp; ++i)
    {
        // Perpendicular surface deposit (normal +X) co-located within the radius.
        store.append(RawBounce{Vector{0.1, 0, 0}, Vector{-1, 0, 0}, Vector{1, 0, 0},
                               RawBounce::kTimelessDeposit, Color{1.0f, 1.0f, 1.0f}});
    }
    store.buildIndex(footprint);

    const Hit hit = upZHit(matIndex);
    std::size_t kept = 0;
    ProbeGather::testing::gatherRadiance(store, *materials, hit,
                                         materials->fetchByIndex(matIndex),
                                         Vector{0, 0, 1}, footprint, 0.0, 0.0f, 0.0f, kept);
    // Only the same-normal deposits survive the normal-agreement test.
    REQUIRE(kept == static_cast<std::size_t>(same));
}

TEST_CASE("T6 GatherRadianceUnit: temporal window keeps timeless + in-window, drops far-time",
          "[GatherRadianceUnit][T6]")
{
    const Color albedo{0.9f, 0.9f, 0.9f};
    size_t matIndex = 0;
    auto materials = oneLambertian(albedo, matIndex);
    const double footprint = 2.0;
    const Hit hit = upZHit(matIndex);

    BounceStore store(64);
    // A timeless (emitter-style) deposit — always kept.
    store.append(RawBounce{Vector{0, 0, 0}, Vector{0, 0, -1}, Vector{0, 0, 1},
                           RawBounce::kTimelessDeposit, Color{1, 1, 1}});
    // A deposit at time 0.0 — within the window of a rayTime 0.0.
    store.append(RawBounce{Vector{0, 0, 0}, Vector{0, 0, -1}, Vector{0, 0, 1},
                           0.0f, Color{1, 1, 1}});
    // A deposit at time 5.0 — outside a small window of rayTime 0.0.
    store.append(RawBounce{Vector{0, 0, 0}, Vector{0, 0, -1}, Vector{0, 0, 1},
                           5.0f, Color{1, 1, 1}});
    store.buildIndex(footprint);

    // Tight window (0.5): the t=5 deposit is dropped; timeless + t=0 are kept => 2.
    std::size_t kept = 0;
    ProbeGather::testing::gatherRadiance(store, *materials, hit,
                                         materials->fetchByIndex(matIndex),
                                         Vector{0, 0, 1}, footprint, 0.0,
                                         /*rayTime=*/0.0f, /*timeHalfWindow=*/0.5f, kept);
    REQUIRE(kept == 2);

    // Wide window (10): all three kept.
    std::size_t keptWide = 0;
    ProbeGather::testing::gatherRadiance(store, *materials, hit,
                                         materials->fetchByIndex(matIndex),
                                         Vector{0, 0, 1}, footprint, 0.0, 0.0f, 10.0f,
                                         keptWide);
    REQUIRE(keptWide == 3);
}

TEST_CASE("T6 GatherRadianceUnit: viewer below the surface gathers nothing", "[GatherRadianceUnit][T6]")
{
    const Color albedo{0.9f, 0.9f, 0.9f};
    size_t matIndex = 0;
    auto materials = oneLambertian(albedo, matIndex);
    const double footprint = 2.0;
    BounceStore store(16);
    store.append(RawBounce{Vector{0, 0, 0}, Vector{0, 0, -1}, Vector{0, 0, 1},
                           RawBounce::kTimelessDeposit, Color{1, 1, 1}});
    store.buildIndex(footprint);
    const Hit hit = upZHit(matIndex);

    std::size_t deposits = 0;
    const Color L = ProbeGather::testing::gatherRadiance(
        store, *materials, hit, materials->fetchByIndex(matIndex),
        Vector{0.0, 0.0, -1.0}, footprint, 0.0, 0.0f, 0.0f, deposits);
    REQUIRE(L.red == 0.0f);
    REQUIRE(L.green == 0.0f);
    REQUIRE(L.blue == 0.0f);
}
