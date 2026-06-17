#include <catch2/catch_all.hpp>

#include "Buffer.h"
#include "Camera.h"
#include "RenderFixture.h"
#include "StatAssert.h"
#include "Vector.h"

#include <cmath>
#include <string>

// ============================================================================
// T5 InverseSquareFalloff — the first analytic PIXEL-VALUE oracle
// ============================================================================
//
// An OmniLight at height H over a flat diffuse floor. The irradiance at a floor point
// p is E(p) = I * cos(theta) / d^2 (the inverse-square law with the cosine of the
// incidence angle), and a Lambertian floor's outgoing radiance is L(p) = (albedo/pi)
// * E(p). So for two floor points the RENDERED luminance RATIO is purely geometric:
//
//   L(p1) / L(p2) == (cos1 / d1^2) / (cos2 / d2^2)
//
// The RATIO cancels I, the albedo, the exposure, the tonemap, and the gather's
// 4/pi parity constant — leaving a parameter-free analytic oracle on actual pixel
// values. d = distance light->point, cos = (H / d) for a horizontal floor (the angle
// between the light direction and the floor normal +Y).
//
// bounceThreshold 1 + black surroundings keep the floor lit by DIRECT light only, so
// the deposited irradiance is exactly the analytic E(p). Single-thread deterministic.

namespace
{
constexpr double kLightHeight = 200.0;  // OmniLight Y above the floor (floor at y=0)

std::string falloffScene()
{
    return R"JSON({
  "$materials": { "Floor": { "$type": "Diffuse", "$color": [0.7, 0.7, 0.7] } },
  "$workerConfiguration": { "$workerCount": 1, "$fetchSize": 50000, "$photonQueueSize": 60000000 },
  "$renderConfiguration": {
    "$width": 200, "$height": 200, "$photonsPerLight": 20000000,
    "$bounceThreshold": 1, "$terminationThreshold": 0.001,
    "$deterministic": true, "$seed": 7, "$bounceStoreCapacity": 120000000
  },
  "$scene": {
    "Camera": { "$type": "Camera", "$verticalFieldOfView": 70.0,
      "$position": [0.0, 600.0, 0.0],
      "$rotation": { "$type": "PitchYawRollDegrees", "$value": [90.0, 0.0, 0.0] } },
    "Light": { "$type": "OmniLight", "$position": [0.0, 200.0, 0.0],
      "$color": [1.0, 1.0, 1.0], "$brightness": 200000 },
    "Floor": { "$type": "Quad", "$material": "Floor",
      "$origin": [-400, 0, -400], "$edgeU": [800, 0, 0], "$edgeV": [0, 0, 800] }
  }
})JSON";
}

// Mean luminance over a small window centered on a buffer pixel.
double windowMean(const Buffer& buf, size_t w, size_t h, const PixelCoords& c, size_t r)
{
    const size_t x0 = (c.x > r) ? c.x - r : 0;
    const size_t y0 = (c.y > r) ? c.y - r : 0;
    const size_t x1 = std::min(w - 1, c.x + r);
    const size_t y1 = std::min(h - 1, c.y + r);
    return rt_test::regionMean(buf, x0, y0, x1, y1);
}

// Analytic relative irradiance E(p) = cos(theta)/d^2 for a floor point at lateral
// offset `radial` from directly under the light (light at height H above the point
// straight below it). d = sqrt(radial^2 + H^2), cos = H/d.
double relIrradiance(double radial)
{
    const double d2 = radial * radial + kLightHeight * kLightHeight;
    const double d = std::sqrt(d2);
    const double cosTheta = kLightHeight / d;
    return cosTheta / d2;
}
}  // namespace

TEST_CASE("T5 InverseSquareFalloff: floor luminance ratio matches cos/d^2",
          "[InverseSquareFalloff][T5]")
{
    rt_test::RenderScene scene{falloffScene()};
    const Buffer& buf = scene.buffer();
    const size_t w = scene.width();
    const size_t h = scene.height();
    REQUIRE(scene.meanLuminance() > 0.0);
    REQUIRE_FALSE(scene.result.cameras.empty());
    const auto& cam = scene.result.cameras.front().camera;

    // Floor points: directly under the light (radial 0) and three lateral offsets.
    // Camera looks straight down, so these project cleanly to distinct pixels. Use
    // points along +X so they spread across the frame.
    struct Sample { double radial; PixelCoords px; double measured; };
    const double radials[] = {0.0, 80.0, 160.0, 240.0};

    std::vector<Sample> samples;
    for (double radial : radials)
    {
        const Vector world{radial, 0.0, 0.0};
        const auto px = cam->coordForPoint(world);
        REQUIRE(px.has_value());
        const double m = windowMean(buf, w, h, *px, 4);
        INFO("radial=" << radial << " px=(" << px->x << "," << px->y << ") L=" << m);
        REQUIRE(m > 0.0);
        samples.push_back({radial, *px, m});
    }

    // Compare each off-center point's luminance ratio (vs the center point) to the
    // analytic cos/d^2 ratio. The ratio cancels all constants.
    const double E0 = relIrradiance(0.0);
    const double L0 = samples[0].measured;
    for (size_t i = 1; i < samples.size(); ++i)
    {
        const double measuredRatio = samples[i].measured / L0;
        const double analyticRatio = relIrradiance(samples[i].radial) / E0;
        INFO("radial=" << samples[i].radial << " measuredRatio=" << measuredRatio
             << " analyticRatio=" << analyticRatio);
        // 8% band: density-estimate noise + the finite window vs a point oracle. A
        // missing cosine or a 1/d (not 1/d^2) falloff would miss by far more.
        REQUIRE(measuredRatio == Catch::Approx(analyticRatio).epsilon(0.08));
    }
}
