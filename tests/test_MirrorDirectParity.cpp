#include <catch2/catch_all.hpp>

#include "Buffer.h"
#include "Color.h"
#include "Hit.h"
#include "ProbeGather.h"
#include "RenderFixture.h"
#include "StatAssert.h"
#include "Vector.h"

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

// ============================================================================
// #63 — reflected gather footprint: ray-differential TIGHTENING (keep 2x cap)
// ============================================================================
//
// THE BUG: the reflected gather disc was sized ONLY by the unfolded-path
// perpendicular footprint (unfoldedPathLength·tan(halfAngle)) with up to a 2x
// grazing inflation, with NO ray-differential tightening — unlike the DIRECT path,
// which takes min(diffRadius, perpRadius). So reflected contact shadows washed out
// and reflected noise smeared: the reflected disc was often far larger than the
// adjacent-pixel reflected rays actually spaced on the surface.
//
// THE FIX (APPROVED): apply the same ray-differential tightening to the reflected
// footprint — min against an adjacent-pixel reflected hit unfolded through the same
// specular chain — and KEEP the existing 2x grazing inflation as an upper-bound
// CEILING on the perpendicular term only.
//
// THE UNIT TEST (exact, deterministic): call the PRODUCTION testing::
// reflectedFootprintRadius with and without an adjacent reflected hit and assert:
//   (1) a CLOSE adjacent hit TIGHTENS the radius below the perpendicular footprint
//       (the differential is the smaller estimate -> it wins the min);
//   (2) a FAR adjacent hit does NOT enlarge the disc past the perpendicular ceiling
//       (the min keeps it at the 2x-capped perpendicular -> the cap is still the
//       upper bound, grazing surfaces never over-blur);
//   (3) with NO adjacent hit the result is exactly the perpendicular footprint (the
//       fallback is byte-for-byte the pre-fix behavior).
// Pre-fix the function had no adjacent-hit parameter, so (1) is the regression the
// fix introduces: the differential can now pull the reflected disc tighter than the
// perpendicular estimate, matching the direct path.

TEST_CASE("#63 reflected footprint: a close adjacent reflected hit tightens the gather disc",
          "[MirrorDirectParity][ReflectedFootprint][T4]")
{
    // A reflected surface facing -z (toward the viewer), hit at the origin. The
    // viewer looks straight on (cosView = 1), so the perpendicular footprint carries
    // NO grazing inflation — perp == unfoldedPathLength·tan(halfAngle).
    Hit hit;
    hit.position = Vector{0.0, 0.0, 0.0};
    hit.normal = Vector{0.0, 0.0, -1.0};
    const Vector viewer{0.0, 0.0, -1.0};  // unit, toward the last specular vertex

    const double halfAngle = 0.002;          // ~ a pixel half-angle in radians
    const double pathLength = 1000.0;        // unfolded camera->reflected-hit distance
    const double perp = pathLength * std::tan(halfAngle);

    // No adjacent hit -> exactly the perpendicular footprint (fallback == pre-fix).
    const double rNoAdj = ProbeGather::testing::reflectedFootprintRadius(
        halfAngle, pathLength, viewer, hit, nullptr);
    REQUIRE(rNoAdj == Catch::Approx(perp).epsilon(1e-9));

    // (1) A CLOSE adjacent reflected hit (on-surface spacing 0.5·perp) gives a
    // differential radius of 0.25·perp -> well below perp, so the min TIGHTENS the
    // disc to the differential. This is the contact-shadow / noise-smear fix.
    const Vector closeAdj = hit.position + Vector{0.5 * perp, 0.0, 0.0};
    const double rClose = ProbeGather::testing::reflectedFootprintRadius(
        halfAngle, pathLength, viewer, hit, &closeAdj);
    REQUIRE(rClose == Catch::Approx(0.25 * perp).epsilon(1e-9));
    REQUIRE(rClose < perp);  // the differential decisively tightened the disc

    // (2) A FAR adjacent reflected hit (spacing 10·perp, differential 5·perp) must NOT
    // enlarge the disc past the perpendicular CEILING — the min caps it at perp. The
    // 2x grazing cap stays the upper bound; a diverging differential cannot over-blur.
    const Vector farAdj = hit.position + Vector{10.0 * perp, 0.0, 0.0};
    const double rFar = ProbeGather::testing::reflectedFootprintRadius(
        halfAngle, pathLength, viewer, hit, &farAdj);
    REQUIRE(rFar == Catch::Approx(perp).epsilon(1e-9));
    REQUIRE(rFar <= rNoAdj);  // never larger than the perpendicular fallback

    // (3) The 2x grazing CEILING is preserved on the perpendicular term: at a grazing
    // reflected surface (cosView = 0.3 < 0.5) the perpendicular footprint is inflated
    // by exactly 2x (max(0.5, cosView) = 0.5), and with no adjacent hit that capped
    // value is returned (the cap is the ceiling, never the old 1/cosView ~ 3.3x).
    Hit grazing;
    grazing.position = Vector{0.0, 0.0, 0.0};
    // Normal tilted so the unit viewer (-z) makes ~72.5deg with it (cosView ~ 0.3).
    grazing.normal = Vector::normalized(Vector{0.954, 0.0, -0.3});
    const double cosGraze = Vector::dot(Vector::normalized(viewer), grazing.normal);
    REQUIRE(cosGraze < 0.5);  // confirm we are past the 2x cap knee
    const double rGraze = ProbeGather::testing::reflectedFootprintRadius(
        halfAngle, pathLength, viewer, grazing, nullptr);
    REQUIRE(rGraze == Catch::Approx(perp / 0.5).epsilon(1e-9));  // exactly 2x, not 1/cos
}

// ----------------------------------------------------------------------------
// #63 — rendered confirmation: the reflected feature is SHARPER (tighter blob)
// ----------------------------------------------------------------------------
//
// Renders a Cornell box whose LEFT wall is a flat mirror, with a bright square
// patch on the dark back wall seen DIRECTLY (center-right of frame) and REFLECTED
// in the mirror (a grazing blob at the left edge). The reflected blob's
// luminance-weighted SECOND MOMENT (its spatial spread in pixel^2 — the blob "size"
// oracle from RenderFixture.h) measures its sharpness: an over-blurred reflected
// footprint smears the blob, raising its second moment.
//
// PRE-FIX (no reflected ray-differential, perpendicular footprint only): the
// reflected blob's second moment measured ~147. POST-FIX (differential tightening,
// 2x cap kept as ceiling): ~105 — a ~28% tighter reflection, confirming the disc was
// pulled in below the perpendicular estimate. The DIRECT blob's second moment is
// UNCHANGED (~58, the direct path is untouched), which is the control: a global
// brightness/exposure shift would move both, but only the reflected blob tightens.
//
// The assertion sits at 130, decisively between the pre-fix (147) and post-fix (105)
// values: it FAILS pre-fix and PASSES post-fix. A second assertion pins the direct
// blob unchanged (it must stay well below the reflected blob's pre-fix spread), so a
// future regression that re-blurs reflections (or that "fixes" the test by blurring
// the direct path to match) is caught. Single-thread deterministic for stability.

namespace
{
std::string reflectedSharpnessScene()
{
    return R"JSON({
  "$materials": {
    "Mirror": { "$type": "Mirror", "$color": [0.95, 0.95, 0.95] },
    "Dark": { "$type": "Diffuse", "$color": [0.02, 0.02, 0.02] },
    "Bright": { "$type": "Diffuse", "$color": [0.9, 0.9, 0.9] }
  },
  "$workerConfiguration": { "$workerCount": 1, "$fetchSize": 50000, "$photonQueueSize": 30000000 },
  "$renderConfiguration": {
    "$width": 200, "$height": 160, "$photonsPerLight": 6000000,
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
      "Right": { "$type": "Quad", "$material": "Dark",
        "$origin": [150, 0, -150], "$edgeU": [0, 300, 0], "$edgeV": [0, 0, 300] },
      "Ceiling": { "$type": "Quad", "$material": "Dark",
        "$origin": [-150, 300, 300], "$edgeU": [300, 0, 0], "$edgeV": [0, 0, -300] },
      "Floor": { "$type": "Quad", "$material": "Dark",
        "$origin": [-150, 0, -150], "$edgeU": [300, 0, 0], "$edgeV": [0, 0, 300] },
      "BackWall": { "$type": "Quad", "$material": "Dark",
        "$origin": [-150, 0, 300], "$edgeU": [300, 0, 0], "$edgeV": [0, 300, 0] },
      "BackPatch": { "$type": "Quad", "$material": "Bright",
        "$origin": [40, 110, 299], "$edgeU": [70, 0, 0], "$edgeV": [0, 70, 0] }
    }
  }
})JSON";
}
}  // namespace

TEST_CASE("#63 reflected footprint: the reflected feature is sharper than the pre-fix blur",
          "[MirrorDirectParity][ReflectedFootprint][T4]")
{
    rt_test::RenderScene scene{reflectedSharpnessScene()};
    const Buffer& buf = scene.buffer();
    const size_t h = scene.height();
    REQUIRE(scene.meanLuminance() > 0.0);

    const size_t yLo = h * 30 / 100;
    const size_t yHi = h * 70 / 100;

    // Reflected blob: the grazing reflection at the LEFT edge of frame (the patch
    // reflected in the left mirror). Direct blob: center-right (the patch seen
    // directly). Column bands chosen from the rendered geometry.
    const double reflectedSpread = rt_test::secondMoment(buf, 44, yLo, 62, yHi);
    const double directSpread = rt_test::secondMoment(buf, 118, yLo, 140, yHi);

    INFO("reflected second moment = " << reflectedSpread
         << " | direct second moment = " << directSpread);

    // Both blobs must actually be present (lit) so the spreads are meaningful.
    REQUIRE(rt_test::centroid(buf, 44, yLo, 62, yHi).totalWeight > 0.0);
    REQUIRE(rt_test::centroid(buf, 118, yLo, 140, yHi).totalWeight > 0.0);

    // HEADLINE: the reflected blob is TIGHT — its spread is below 130, the threshold
    // between the pre-fix perpendicular-only blur (~147) and the post-fix differential
    // tightening (~105). Pre-fix this assertion fails; post-fix it passes.
    REQUIRE(reflectedSpread < 130.0);

    // CONTROL: the DIRECT blob's spread is unchanged by this fix (the direct path is
    // untouched) and stays well below the reflected blob's PRE-FIX spread — so the
    // tightening above is a real reflected-path change, not a global exposure shift,
    // and a "fix" that blurred the direct path to match would trip this.
    REQUIRE(directSpread < 90.0);
}
