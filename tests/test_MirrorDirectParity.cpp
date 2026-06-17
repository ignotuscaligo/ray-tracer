#include <catch2/catch_all.hpp>

#include "Buffer.h"
#include "Color.h"
#include "RenderFixture.h"
#include "StatAssert.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>
#include <vector>

// ============================================================================
// T4 MirrorDirectParity — the Phase-2a headline "mirror == direct" invariant
// ============================================================================
//
// DESIGN [INVARIANT] (§0 headline): "A reflection in a mirror is the scene gathered
// the SAME way as the direct view ... a flat mirror shows the scene at the SAME
// fidelity as pointing the camera at it directly — not blurrier/blockier." Until now
// this was pinned only by eyeballing MirrorDirectTest.json's PNG. This converts it to
// a hard number, using a COLOR-RATIO oracle that is robust to the exact reflection
// location and per-pixel footprint (the same robustness trick test_MinorityFresnel
// uses): chromaticity, not absolute pixel placement.
//
// SCENE: a Cornell box whose LEFT wall is a COLOR-TINTED mirror (albedo
// (0.9, 0.45, 0.225) — a warm 1 : 1/2 : 1/4 tint). A WHITE flat diffuse patch on the
// dark back wall is seen DIRECTLY (white) and REFLECTED in the tinted mirror. Because
// the reflection is the direct view multiplied by the ONE specular BSDF weight (the
// mirror albedo), the reflected patch's CHROMATICITY must equal the mirror's tint:
//   reflected G/R == mirror G/R (= 0.5),  reflected B/R == mirror B/R (= 0.25),
// while the direct patch is neutral (G/R == B/R == 1). This is the mirror==direct
// invariant stated as a hard per-channel ratio, independent of where the reflection
// lands or how big its footprint is.
//
// A separate, lower-fidelity reflection path (a density-grid lookup, a neutral
// fallback, a cos(view)/footprint distortion) would either neutralize the tint
// (ratios drift toward 1) or kill the reflection (no colored signal). Either way the
// chromaticity assertion fails.
//
// Runs in SINGLE-THREAD DETERMINISTIC mode so the result is stable run to run.

namespace
{
// Mirror tint: R:G:B = 1 : 0.5 : 0.25.
constexpr double kTintGtoR = 0.5;
constexpr double kTintBtoR = 0.25;

std::string parityScene()
{
    return R"JSON({
  "$materials": {
    "Mirror": { "$type": "Mirror", "$color": [0.9, 0.45, 0.225] },
    "Wall": { "$type": "Diffuse", "$color": [0.03, 0.03, 0.03] },
    "Patch": { "$type": "Diffuse", "$color": [0.95, 0.95, 0.95] }
  },
  "$workerConfiguration": { "$workerCount": 1, "$fetchSize": 50000, "$photonQueueSize": 30000000 },
  "$renderConfiguration": {
    "$width": 200, "$height": 160, "$photonsPerLight": 4000000,
    "$bounceThreshold": 2, "$terminationThreshold": 0.01,
    "$deterministic": true, "$seed": 9, "$bounceStoreCapacity": 40000000
  },
  "$scene": {
    "Camera": { "$type": "Camera", "$verticalFieldOfView": 65.0,
      "$position": [110.0, 150.0, -340.0],
      "$rotation": { "$type": "PitchYawRollDegrees", "$value": [0.0, -16.0, 0.0] } },
    "Light": { "$type": "AreaLight", "$shape": "Square", "$size": 140,
      "$position": [-40, 298, 120],
      "$rotation": { "$type": "PitchYawRollDegrees", "$value": [90, 0, 0] },
      "$color": [1.0, 1.0, 1.0], "$luminousFlux": 260000000 },
    "Box": {
      "$type": "Object",
      "LeftMirror": { "$type": "Quad", "$material": "Mirror",
        "$origin": [-150, 0, 300], "$edgeU": [0, 300, 0], "$edgeV": [0, 0, -300] },
      "Right": { "$type": "Quad", "$material": "Wall",
        "$origin": [150, 0, -150], "$edgeU": [0, 300, 0], "$edgeV": [0, 0, 300] },
      "Ceiling": { "$type": "Quad", "$material": "Wall",
        "$origin": [-150, 300, 300], "$edgeU": [300, 0, 0], "$edgeV": [0, 0, -300] },
      "Floor": { "$type": "Quad", "$material": "Wall",
        "$origin": [-150, 0, -150], "$edgeU": [300, 0, 0], "$edgeV": [0, 0, 300] },
      "BackWall": { "$type": "Quad", "$material": "Wall",
        "$origin": [-150, 0, 300], "$edgeU": [300, 0, 0], "$edgeV": [0, 300, 0] },
      "BackPatch": { "$type": "Quad", "$material": "Patch",
        "$origin": [40, 110, 299], "$edgeU": [70, 0, 0], "$edgeV": [0, 70, 0] }
    }
  }
})JSON";
}

// Sum the per-channel color over the brightest `topFraction` of pixels in a buffer
// rectangle (the patch / its reflection dominate the bright tail over the dark walls).
// Returns the channel-summed Color so chromaticity ratios can be taken.
Color brightRegionColor(const Buffer& buf, size_t x0, size_t y0, size_t x1, size_t y1,
                        double topFraction)
{
    std::vector<std::tuple<double, size_t, size_t>> lums;
    for (size_t y = y0; y <= y1; ++y)
    {
        for (size_t x = x0; x <= x1; ++x)
        {
            lums.emplace_back(rt_test::pixelLuminance(buf.fetchColor({x, y})), x, y);
        }
    }
    std::sort(lums.begin(), lums.end(),
              [](const auto& a, const auto& b) { return std::get<0>(a) > std::get<0>(b); });
    const size_t take = std::max<size_t>(
        1, static_cast<size_t>(static_cast<double>(lums.size()) * topFraction));

    double r = 0, g = 0, b = 0;
    for (size_t i = 0; i < take; ++i)
    {
        const Color c = buf.fetchColor({std::get<1>(lums[i]), std::get<2>(lums[i])});
        r += c.red;
        g += c.green;
        b += c.blue;
    }
    return Color{static_cast<float>(r), static_cast<float>(g), static_cast<float>(b)};
}
}  // namespace

TEST_CASE("T4 MirrorDirectParity: the reflection carries the mirror's tint (mirror==direct)",
          "[MirrorDirectParity][T4]")
{
    rt_test::RenderScene scene{parityScene()};
    const Buffer& buf = scene.buffer();
    const size_t w = scene.width();
    const size_t h = scene.height();
    REQUIRE(scene.meanLuminance() > 0.0);

    // Exclude the bright area-light band: restrict to the vertical middle where both
    // patches live (the patches are mid-height; the light + its reflection are higher).
    const size_t yLo = h * 35 / 100;
    const size_t yHi = h * 62 / 100;
    const size_t midX = w / 2;

    // The direct white patch and its tinted reflection are on opposite sides of frame
    // center. One half holds each; the float buffer's x-flip vs the PNG only decides
    // WHICH half, not the chromaticity we measure.
    const Color leftCol = brightRegionColor(buf, 0, yLo, midX - 1, yHi, 0.03);
    const Color rightCol = brightRegionColor(buf, midX, yLo, w - 1, yHi, 0.03);

    // The reflected copy is the tinted one: its B/R is well below 1 (the mirror's 0.25
    // tint). The direct (white) copy has B/R ~ 1. Identify them by that.
    const double leftBtoR = leftCol.blue / std::max(1e-6f, leftCol.red);
    const double rightBtoR = rightCol.blue / std::max(1e-6f, rightCol.red);
    INFO("left B/R=" << leftBtoR << " right B/R=" << rightBtoR);

    const Color& reflected = (leftBtoR < rightBtoR) ? leftCol : rightCol;
    const Color& direct = (leftBtoR < rightBtoR) ? rightCol : leftCol;

    const double dGtoR = direct.green / std::max(1e-6f, direct.red);
    const double dBtoR = direct.blue / std::max(1e-6f, direct.red);
    const double rGtoR = reflected.green / std::max(1e-6f, reflected.red);
    const double rBtoR = reflected.blue / std::max(1e-6f, reflected.red);

    INFO("direct chroma G/R=" << dGtoR << " B/R=" << dBtoR);
    INFO("reflected chroma G/R=" << rGtoR << " B/R=" << rBtoR);

    // The DIRECT white patch is near-neutral (a white patch under a white light stays
    // close to 1:1:1; the dark walls bounce only a little color).
    REQUIRE(dGtoR == Catch::Approx(1.0).margin(0.15));
    REQUIRE(dBtoR == Catch::Approx(1.0).margin(0.15));

    // The REFLECTED patch carries the mirror's tint: G/R == 0.5, B/R == 0.25 (the
    // mirror == direct invariant — the reflection is the white direct view times the
    // mirror's per-channel albedo). A neutral / low-fidelity reflection path would
    // leave these near 1.0.
    REQUIRE(rGtoR == Catch::Approx(kTintGtoR).margin(0.12));
    REQUIRE(rBtoR == Catch::Approx(kTintBtoR).margin(0.10));

    // And decisively distinct from the direct view's chromaticity.
    REQUIRE(rBtoR < 0.6 * dBtoR);
}
