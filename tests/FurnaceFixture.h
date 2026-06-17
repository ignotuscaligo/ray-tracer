#pragma once

#include "BounceStore.h"
#include "Color.h"

#include <cmath>
#include <string>

// ============================================================================
// FurnaceFixture.h — sealed-cube ("furnace") scene builder + energy oracle
// ============================================================================
//
// A DELIBERATELY TRUNCATED FURNACE. A classic white-furnace test asserts perfect
// energy conservation in a sealed albedo-1 box. THIS renderer does NOT conserve
// energy that way ON PURPOSE: termination is a deterministic decay + a hard bounce
// CAP, NOT Russian roulette (DESIGN §2). So in a sealed box the dropped residual
// tail beyond the bounce cap makes the box read DARKER than a true furnace — an
// ACCEPTED, documented tradeoff. A strict white-furnace assertion (total deposited
// == Phi/(1-rho)) contradicts DESIGN §2 and MUST NOT be "fixed in."
//
// What IS exact here is the TRUNCATED geometric series. With a Lambertian albedo
// rho, cosine-weighted sampling makes the per-bounce throughput EXACTLY rho with
// ZERO variance (f*cos/pdf = albedo identically — DESIGN §1). A photon emitted with
// magnitude Phi/N therefore deposits, at bounce depth b, exactly (Phi/N)*rho^b, and
// the deterministic bounce cap B stops it after depositing at b = 0..B (B+1
// deposits). Summed over all N photons, the TOTAL deposited power is the exact
// closed form:
//
//     E_total = Phi * (1 - rho^(B+1)) / (1 - rho)            [rho != 1]
//     E_total = Phi * (B + 1)                                 [rho == 1]
//
// This single number is the FurnaceLedger oracle (T1). It pins DESIGN invariants
// #3 (additive gather, no gather-side 1/N), #4 (relative absorption), #5 (single
// stochastic sample), #7/#9/#10 (deterministic decay, bounce cap, absolute floor):
// any reintroduction of Russian roulette (survivor boost), a gather-side 1/N, or
// mode-sampling (which would change the per-bounce throughput off exactly rho)
// moves the sum off this oracle.
//
// The fixture uses an OmniLight (point source) at the box center, NOT an AreaLight:
// a point source deposits NO emitter patches into the store, so the BounceStore
// sum is purely the transported diffuse deposits the oracle predicts.

namespace rt_test
{

struct FurnaceParams
{
    double albedo = 0.5;        // wall reflectance rho (white walls, all channels)
    double half = 100.0;        // half-edge of the cube (walls at +/- half)
    double brightnessCd = 100.0;  // OmniLight luminous INTENSITY (candela)
    size_t photonsPerLight = 30000;
    size_t bounceThreshold = 4;  // the hard bounce cap B
    size_t width = 48;
    size_t height = 48;
    std::uint32_t seed = 1234;
    // The probe keep-test culls a non-delta bounce farther than
    // (probeKeepRadiusScale * sceneDepthFootprint) from EVERY probe. A camera inside
    // the box cannot see all 6 walls, so a deposit on a wall behind/beside the camera
    // would normally be culled and the energy LEDGER would be incomplete. We blow the
    // keep radius far past the box diagonal so the keep-test retains EVERYTHING (every
    // bounce is within keep range of some probe); tests assert bounceCulled == 0 to
    // confirm the ledger saw every deposit.
    double probeKeepRadiusScale = 100000.0;
    // BounceStore capacity is min(this, probeCount * 256). N*(B+1) deposits must fit,
    // so keep photonsPerLight*(B+1) below probeCount*256 (probeCount = width*height).
    size_t bounceStoreCapacity = 40000000;
};

// Total luminous flux Phi of the furnace's isotropic point light: Phi = I * 4*pi.
inline double furnaceFlux(const FurnaceParams& p)
{
    return p.brightnessCd * 4.0 * M_PI;
}

// The exact truncated-geometric-series energy oracle (see header comment):
//   E_total = Phi * (1 - rho^(B+1)) / (1 - rho)     (or Phi*(B+1) at rho == 1).
inline double furnaceEnergyOracle(const FurnaceParams& p)
{
    const double phi = furnaceFlux(p);
    const double rho = p.albedo;
    const int terms = static_cast<int>(p.bounceThreshold) + 1;  // deposits at b=0..B
    if (std::abs(rho - 1.0) < 1e-12)
    {
        return phi * static_cast<double>(terms);
    }
    return phi * (1.0 - std::pow(rho, terms)) / (1.0 - rho);
}

// Sum of a single channel of every stored deposit's power — the measured ledger.
// For a white furnace all channels are equal, so any channel equals E_total. The
// store must have drained (post-render) before this is read.
inline double sumDepositedPower(const BounceStore& store)
{
    double sum = 0.0;
    const std::size_t n = store.size();
    for (std::size_t i = 0; i < n; ++i)
    {
        sum += static_cast<double>(store[i].power.red);
    }
    return sum;
}

// Build a sealed-cube ("furnace") scene JSON: 6 inward-facing diffuse quads at
// +/- half on each axis, a white OmniLight at the center, and a camera inside
// looking at the +z wall (so the probe pass covers the interior surface). All in
// SINGLE-THREAD DETERMINISTIC MODE so the deposit set — and thus the ledger — is
// bitwise-reproducible. termination floor is 0 so ONLY the bounce cap terminates
// (the truncated-series premise: no decay-floor kills mid-series).
inline std::string buildFurnaceScene(const FurnaceParams& p)
{
    auto num = [](double v) { return std::to_string(v); };
    const std::string h = num(p.half);
    const std::string nh = num(-p.half);
    const std::string edge = num(2.0 * p.half);
    const std::string nedge = num(-2.0 * p.half);
    const std::string albedo = num(p.albedo);

    // Quad winding: $edgeU x $edgeV is the front-face normal direction. We want
    // every wall's front face pointing INWARD (toward the box interior / the
    // photons), so each pair is wound to face the center.
    std::string s = "{\n";
    s += "  \"$materials\": {\n";
    s += "    \"Wall\": { \"$type\": \"Diffuse\", \"$color\": [" + albedo + ", " +
         albedo + ", " + albedo + "] }\n";
    s += "  },\n";
    s += "  \"$workerConfiguration\": { \"$workerCount\": 1, \"$fetchSize\": 50000, "
         "\"$photonQueueSize\": " + num(static_cast<double>(p.photonsPerLight) + 100000.0) + " },\n";
    s += "  \"$renderConfiguration\": {\n";
    s += "    \"$width\": " + std::to_string(p.width) + ",\n";
    s += "    \"$height\": " + std::to_string(p.height) + ",\n";
    s += "    \"$photonsPerLight\": " + std::to_string(p.photonsPerLight) + ",\n";
    s += "    \"$bounceThreshold\": " + std::to_string(p.bounceThreshold) + ",\n";
    s += "    \"$terminationThreshold\": 0.0,\n";
    s += "    \"$probeKeepRadiusScale\": " + num(p.probeKeepRadiusScale) + ",\n";
    s += "    \"$bounceStoreCapacity\": " + std::to_string(p.bounceStoreCapacity) + ",\n";
    s += "    \"$deterministic\": true,\n";
    s += "    \"$seed\": " + std::to_string(p.seed) + "\n";
    s += "  },\n";
    s += "  \"$scene\": {\n";
    s += "    \"Camera\": { \"$type\": \"Camera\", \"$verticalFieldOfView\": 90.0, "
         "\"$position\": [0.0, 0.0, 0.0], \"$rotation\": { \"$type\": "
         "\"PitchYawRollDegrees\", \"$value\": [0.0, 0.0, 0.0] } },\n";
    s += "    \"Light\": { \"$type\": \"OmniLight\", \"$position\": [0.0, 0.0, 0.0], "
         "\"$color\": [1.0, 1.0, 1.0], \"$brightness\": " + num(p.brightnessCd) + " },\n";
    s += "    \"Box\": {\n";
    s += "      \"$type\": \"Object\",\n";
    // -z wall (front face toward +z): origin at (-h,-h,-h), U=+x, V=+y
    s += "      \"BackZ\": { \"$type\": \"Quad\", \"$material\": \"Wall\", "
         "\"$origin\": [" + nh + ", " + nh + ", " + nh + "], \"$edgeU\": [" + edge +
         ", 0, 0], \"$edgeV\": [0, " + edge + ", 0] },\n";
    // +z wall (front face toward -z): origin at (+h,-h,+h), U=-x, V=+y
    s += "      \"FrontZ\": { \"$type\": \"Quad\", \"$material\": \"Wall\", "
         "\"$origin\": [" + h + ", " + nh + ", " + h + "], \"$edgeU\": [" + nedge +
         ", 0, 0], \"$edgeV\": [0, " + edge + ", 0] },\n";
    // -x wall (front face toward +x): origin at (-h,-h,+h), U=+z? choose so normal +x
    s += "      \"LeftX\": { \"$type\": \"Quad\", \"$material\": \"Wall\", "
         "\"$origin\": [" + nh + ", " + nh + ", " + h + "], \"$edgeU\": [0, 0, " +
         nedge + "], \"$edgeV\": [0, " + edge + ", 0] },\n";
    // +x wall (front face toward -x)
    s += "      \"RightX\": { \"$type\": \"Quad\", \"$material\": \"Wall\", "
         "\"$origin\": [" + h + ", " + nh + ", " + nh + "], \"$edgeU\": [0, 0, " +
         edge + "], \"$edgeV\": [0, " + edge + ", 0] },\n";
    // -y wall (floor, front face toward +y)
    s += "      \"FloorY\": { \"$type\": \"Quad\", \"$material\": \"Wall\", "
         "\"$origin\": [" + nh + ", " + nh + ", " + nh + "], \"$edgeU\": [0, 0, " +
         edge + "], \"$edgeV\": [" + edge + ", 0, 0] },\n";
    // +y wall (ceiling, front face toward -y)
    s += "      \"CeilY\": { \"$type\": \"Quad\", \"$material\": \"Wall\", "
         "\"$origin\": [" + nh + ", " + h + ", " + nh + "], \"$edgeU\": [" + edge +
         ", 0, 0], \"$edgeV\": [0, 0, " + edge + "] }\n";
    s += "    }\n";
    s += "  }\n";
    s += "}\n";
    return s;
}

}  // namespace rt_test
