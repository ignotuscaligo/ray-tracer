#include <catch2/catch_all.hpp>

#include "Buffer.h"
#include "RenderFixture.h"
#include "StatAssert.h"

#include <cmath>
#include <string>

// ============================================================================
// T8 VarianceConvergence — Monte-Carlo noise falls as 1/sqrt(N)
// ============================================================================
//
// The only consistency check on the estimator's variance: quadrupling the photon
// count must HALVE the noise (noise ~ 1/sqrt(N), so RMS(N)/RMS(4N) ~ 2). This catches
// an estimator that is biased-but-low-variance (would not converge) or one whose
// variance does not scale with N (a broken sampling/normalization).
//
// METHOD: the per-pixel noise at a given N is estimated by the RMS difference between
// TWO independent-seed renders at that N (each an unbiased sample of the same image,
// so their difference is pure noise, std ~ sqrt(2) * per-render noise). Compute that
// RMS at N and at 4N; the ratio must be ~2, within a CI (the RMS-of-RMS itself has
// sampling spread, so a single pair gives a noisy ratio — we use a wide band centered
// on 2 that still excludes "no convergence" (ratio ~1) and "wrong scaling").

namespace
{
std::string scene(size_t photonsPerLight, std::uint32_t seed)
{
    std::string s = R"JSON({
  "$materials": { "Matte": { "$type": "Diffuse", "$color": [0.7] } },
  "$workerConfiguration": { "$workerCount": 1, "$fetchSize": 50000, "$photonQueueSize": 60000000 },
  "$renderConfiguration": {
    "$width": 40, "$height": 40, "$photonsPerLight": )JSON";
    s += std::to_string(photonsPerLight);
    s += R"JSON(,
    "$bounceThreshold": 2, "$terminationThreshold": 0.01,
    "$deterministic": true, "$seed": )JSON";
    s += std::to_string(seed);
    s += R"JSON(, "$bounceStoreCapacity": 120000000
  },
  "$scene": {
    "Camera": { "$type": "Camera", "$verticalFieldOfView": 60.0,
      "$position": [0.0, 0.0, -200.0],
      "$rotation": { "$type": "PitchYawRollDegrees", "$value": [0.0, 0.0, 0.0] } },
    "Light": { "$type": "OmniLight", "$position": [0.0, 80.0, -60.0],
      "$color": [1.0, 1.0, 1.0], "$brightness": 50000 },
    "Sphere": { "$type": "SphereVolume", "$material": "Matte",
      "$center": [0.0, 0.0, 0.0], "$radius": 40.0 },
    "BackWall": { "$type": "SphereVolume", "$material": "Matte",
      "$center": [0.0, 0.0, 400.0], "$radius": 300.0 }
  }
})JSON";
    return s;
}

// RMS difference between two independent-seed renders at photon count N — a proxy for
// the per-render noise level at N (up to a sqrt(2) constant that cancels in the ratio).
double noiseAt(size_t photonsPerLight, std::uint32_t seedA, std::uint32_t seedB)
{
    rt_test::RenderScene a{scene(photonsPerLight, seedA)};
    rt_test::RenderScene b{scene(photonsPerLight, seedB)};
    return rt_test::rmse(a.buffer(), b.buffer(), a.width(), a.height());
}
}  // namespace

TEST_CASE("T8 VarianceConvergence: 4x photons halves the RMS noise (1/sqrt(N))",
          "[VarianceConvergence][T8]")
{
    const size_t N = 600000;
    const double noiseN = noiseAt(N, 101, 202);
    const double noise4N = noiseAt(4 * N, 303, 404);

    REQUIRE(noiseN > 0.0);
    REQUIRE(noise4N > 0.0);

    const double ratio = noiseN / noise4N;
    INFO("noise(N)=" << noiseN << " noise(4N)=" << noise4N << " ratio=" << ratio);
    // Expected ratio 2 (sqrt(4)). A single pair of RMS estimates is itself noisy, so a
    // wide band [1.5, 2.7] around 2 — it decisively excludes "no convergence" (ratio
    // ~1, a non-converging or biased estimator) and "wrong scaling" while tolerating
    // the RMS-of-RMS sampling spread.
    REQUIRE(ratio > 1.5);
    REQUIRE(ratio < 2.7);
}
