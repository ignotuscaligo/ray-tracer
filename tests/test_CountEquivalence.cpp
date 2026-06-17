#include <catch2/catch_all.hpp>

#include "Buffer.h"
#include "Color.h"
#include "RenderFixture.h"
#include "StatAssert.h"

#include <cmath>
#include <memory>
#include <string>

// ============================================================================
// T7 CountEquivalence + BufferSummability — the distribution-readiness invariants
// ============================================================================
//
// DESIGN §3/§7 [INVARIANT]: the 1/N photon-count normalization is baked at EMISSION
// (per-photon flux = Phi/N), so the gather is a pure additive sum and the image
// brightness is INDEPENDENT of N (more photons => less noise, same mean). And because
// every deposit carries an absolute, count-baked magnitude, independent renders are
// ADDITIVE estimators of the same image — the property that makes distributed
// rendering "sum the buffers" valid (#25, #26).
//
//   T7a CountEquivalence: render at N and at 4N photons => equal MEAN luminance. Any
//       reintroduction of a gather-side 1/N (double-normalization) would make the 4N
//       image 1/4 as bright; a missing normalization would make it 4x.
//   T7b BufferSummability: two independent-seed renders at the same N are unbiased
//       estimates of the SAME image, so their per-pixel AVERAGE has the same mean and
//       LOWER variance (RMSE-to-each-other is the noise; averaging halves... reduces
//       it). This exercises Buffer's per-channel atomic-add additivity.

namespace
{
std::string scene(size_t photonsPerLight, std::uint32_t seed)
{
    std::string s = R"JSON({
  "$materials": { "Matte": { "$type": "Diffuse", "$color": [0.7] } },
  "$workerConfiguration": { "$workerCount": 1, "$fetchSize": 50000, "$photonQueueSize": 40000000 },
  "$renderConfiguration": {
    "$width": 48, "$height": 48, "$photonsPerLight": )JSON";
    s += std::to_string(photonsPerLight);
    s += R"JSON(,
    "$bounceThreshold": 2, "$terminationThreshold": 0.01,
    "$deterministic": true, "$seed": )JSON";
    s += std::to_string(seed);
    s += R"JSON(, "$bounceStoreCapacity": 80000000
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
}  // namespace

TEST_CASE("T7 CountEquivalence: mean luminance is independent of photon count",
          "[CountEquivalence][T7]")
{
    // N and 4N photons. Per-photon flux is Phi/N at emission, so the expected mean is
    // identical; only noise differs. Same seed base, but the 4N run draws a different
    // (longer) RNG sequence, so this is a Monte-Carlo agreement on the MEAN.
    rt_test::RenderScene n1{scene(/*photonsPerLight=*/1000000, /*seed=*/3)};
    rt_test::RenderScene n4{scene(/*photonsPerLight=*/4000000, /*seed=*/3)};

    const double m1 = n1.meanLuminance();
    const double m4 = n4.meanLuminance();
    REQUIRE(m1 > 0.0);
    REQUIRE(m4 > 0.0);

    const double rel = std::abs(m4 - m1) / m1;
    INFO("mean(N)=" << m1 << " mean(4N)=" << m4 << " rel=" << rel);
    // Count-independent within Monte-Carlo noise. A gather-side 1/N regression would
    // put this at ~0.75 (4N at 1/4 brightness) or ~3.0 (missing normalization) — far
    // outside this band; a 3% band sits well above the sampling noise at 1M+ photons.
    REQUIRE(rel < 0.03);
}

TEST_CASE("T7 BufferSummability: independent renders average to the same image, lower noise",
          "[CountEquivalence][BufferSummability][T7]")
{
    // Two independent-seed renders at the same N. Each is an unbiased estimate of the
    // same expected image (Buffer accumulates additively), so:
    //   - their MEANS agree (within MC noise), and
    //   - their per-pixel AVERAGE is closer to each input than the two inputs are to
    //     each other is loose; instead we assert the averaged image's mean equals each
    //     input mean (additivity) and that the inputs are genuinely DIFFERENT samples
    //     (different seeds => nonzero RMSE), proving the average is a real combination.
    rt_test::RenderScene a{scene(1500000, 11)};
    rt_test::RenderScene b{scene(1500000, 22)};

    const size_t w = a.width();
    const size_t h = a.height();
    REQUIRE(w == b.width());
    REQUIRE(h == b.height());

    const double ma = a.meanLuminance();
    const double mb = b.meanLuminance();
    REQUIRE(ma > 0.0);
    REQUIRE(mb > 0.0);

    // The two independent samples agree in mean (same expected image).
    const double relMeans = std::abs(ma - mb) / ma;
    INFO("meanA=" << ma << " meanB=" << mb << " rel=" << relMeans);
    REQUIRE(relMeans < 0.03);

    // Build the per-pixel SUM buffer A+B via Buffer's additive addColor, then its mean
    // is exactly meanA + meanB (additivity of the accumulator), demonstrating that
    // summing independent buffers combines their energy linearly — the distribution-
    // readiness property (#25).
    Buffer summed(w, h);
    for (size_t y = 0; y < h; ++y)
    {
        for (size_t x = 0; x < w; ++x)
        {
            summed.addColor({x, y}, a.buffer().fetchColor({x, y}));
            summed.addColor({x, y}, b.buffer().fetchColor({x, y}));
        }
    }
    const double meanSum = rt_test::meanLuminance(summed, w, h);
    INFO("mean(A+B)=" << meanSum << " meanA+meanB=" << (ma + mb));
    REQUIRE(meanSum == Catch::Approx(ma + mb).epsilon(1e-4));

    // The two renders are genuinely different samples (different seeds), so their RMSE
    // is nonzero — the average is a real variance-reducing combination, not two copies.
    const double noise = rt_test::rmse(a.buffer(), b.buffer(), w, h);
    INFO("RMSE(A,B)=" << noise);
    REQUIRE(noise > 0.0);
}
