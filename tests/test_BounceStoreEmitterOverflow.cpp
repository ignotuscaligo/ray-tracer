#include <catch2/catch_all.hpp>

#include "Buffer.h"
#include "Color.h"
#include "RenderFixture.h"
#include "StatAssert.h"

#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

// ============================================================================
// Issue #62 — BounceStore overflow must NOT black out emitters / fixtures
// ============================================================================
//
// THE BUG: emitter fixture deposits (an area light's own surface radiance, stored
// as raw bounces so the unified gather renders the lit panel directly and in
// mirrors — DESIGN §6d) were appended to the BounceStore AFTER the photon pass. The
// store drops every append past its capacity ceiling (BounceStore::append: a
// lock-free fetch_add, slot >= capacity => record dropped). So on a scene whose
// photons fill the store to capacity, EVERY emitter deposit was dropped and the
// fixture rendered BLACK at high photon counts — a quietly wrong image.
//
// THE FIX (APPROVED): deposit emitter contributions FIRST, before the photon pass,
// so their slots are reserved before any photon can claim one. The photon pass then
// competes only for the REMAINING slots; the fixture's own radiance is always kept.
//
// THE TEST (objective, deterministic): render the SAME emitter scene twice in
// single-thread deterministic mode —
//   (a) BASELINE: a generous $bounceStoreCapacity (no overflow), and
//   (b) OVERFLOW: a tiny $bounceStoreCapacity that deterministically overflows
//       (bounceStoreDropped > 0).
// Then assert the emitter PANEL REGION of the overflow render is NOT black, and its
// luminance is within tolerance of the no-overflow baseline. Pre-fix the overflow
// render's panel reads ~0 (all emitter deposits dropped); post-fix it matches the
// baseline because the emitter slots are reserved before the photon flood.
//
// The panel is the ceiling area light, framed in the TOP band of the image by a
// camera tilted up. The scene is deliberately small + low-photon so a few-thousand-
// slot capacity is far below the photons the keep-test retains => guaranteed
// overflow, with a fixed $seed so "guaranteed" is reproducible.

namespace
{
// Camera tilts UP (+pitch) so the ceiling-mounted square area light fills the top
// band of the frame; the box walls fill the rest. Single-thread deterministic so
// both the baseline and overflow renders are bitwise-stable run to run, and the
// two differ ONLY in $bounceStoreCapacity.
std::string emitterScene(const char* bounceStoreCapacity)
{
    std::string json = R"JSON({
  "$materials": {
    "Wall": { "$type": "Diffuse", "$color": [0.6, 0.6, 0.6] }
  },
  "$workerConfiguration": { "$workerCount": 1, "$fetchSize": 50000, "$photonQueueSize": 30000000 },
  "$renderConfiguration": {
    "$width": 160, "$height": 160, "$photonsPerLight": 4000000,
    "$bounceThreshold": 2, "$terminationThreshold": 0.01,
    "$deterministic": true, "$seed": 7, "$bounceStoreCapacity": )JSON";
    json += bounceStoreCapacity;
    json += R"JSON(
  },
  "$scene": {
    "Camera": { "$type": "Camera", "$verticalFieldOfView": 70.0,
      "$position": [0.0, 150.0, -300.0],
      "$rotation": { "$type": "PitchYawRollDegrees", "$value": [-28.0, 0.0, 0.0] } },
    "Light": { "$type": "AreaLight", "$shape": "Square", "$size": 160,
      "$position": [0, 298, 80],
      "$rotation": { "$type": "PitchYawRollDegrees", "$value": [90, 0, 0] },
      "$color": [1.0, 1.0, 1.0], "$luminousFlux": 260000000 },
    "Box": {
      "$type": "Object",
      "Ceiling": { "$type": "Quad", "$material": "Wall",
        "$origin": [-150, 300, 300], "$edgeU": [300, 0, 0], "$edgeV": [0, 0, -300] },
      "Floor": { "$type": "Quad", "$material": "Wall",
        "$origin": [-150, 0, -150], "$edgeU": [300, 0, 0], "$edgeV": [0, 0, 300] },
      "Left": { "$type": "Quad", "$material": "Wall",
        "$origin": [-150, 0, 300], "$edgeU": [0, 300, 0], "$edgeV": [0, 0, -300] },
      "Right": { "$type": "Quad", "$material": "Wall",
        "$origin": [150, 0, -150], "$edgeU": [0, 300, 0], "$edgeV": [0, 0, 300] },
      "BackWall": { "$type": "Quad", "$material": "Wall",
        "$origin": [-150, 0, 300], "$edgeU": [300, 0, 0], "$edgeV": [0, 300, 0] }
    }
  }
})JSON";
    return json;
}

// Mean luminance of the emitter PANEL — located orientation-independently as the
// brightest `topFraction` of pixels in the frame. The ceiling area light is by far
// the brightest surface in this box (its fixture deposits saturate the panel), so
// the bright tail IS the panel, wherever the buffer's x/y orientation places it.
// Measured on the BASELINE buffer; the same pixel set is then sampled on the
// overflow buffer so we compare the SAME on-panel pixels in both renders.
struct PanelPixels
{
    std::vector<std::pair<size_t, size_t>> coords;
};

PanelPixels findPanel(const Buffer& buf, size_t w, size_t h, double topFraction)
{
    std::vector<std::tuple<double, size_t, size_t>> lums;
    lums.reserve(w * h);
    for (size_t y = 0; y < h; ++y)
    {
        for (size_t x = 0; x < w; ++x)
        {
            lums.emplace_back(rt_test::pixelLuminance(buf.fetchColor({x, y})), x, y);
        }
    }
    std::sort(lums.begin(), lums.end(),
              [](const auto& a, const auto& b) { return std::get<0>(a) > std::get<0>(b); });
    const size_t take = std::max<size_t>(
        1, static_cast<size_t>(static_cast<double>(lums.size()) * topFraction));
    PanelPixels p;
    p.coords.reserve(take);
    for (size_t i = 0; i < take; ++i)
    {
        p.coords.emplace_back(std::get<1>(lums[i]), std::get<2>(lums[i]));
    }
    return p;
}

double meanOverPixels(const Buffer& buf, const PanelPixels& panel)
{
    if (panel.coords.empty())
    {
        return 0.0;
    }
    double sum = 0.0;
    for (const auto& [x, y] : panel.coords)
    {
        sum += rt_test::pixelLuminance(buf.fetchColor({x, y}));
    }
    return sum / static_cast<double>(panel.coords.size());
}
}  // namespace

TEST_CASE("Issue #62: a BounceStore overflow does not black out the emitter fixture",
          "[BounceStore][emitter][overflow][regression]")
{
    // (a) BASELINE — generous capacity, no overflow.
    rt_test::RenderScene baseline{emitterScene("40000000")};
    const Buffer& baseBuf = baseline.buffer();
    const size_t w = baseline.width();
    const size_t h = baseline.height();

    REQUIRE(baseline.result.bounceStoreDropped == 0);   // no overflow in the baseline
    REQUIRE(baseline.result.emitterDepositsKept > 0);    // the fixture was deposited

    // Locate the panel on the BASELINE (the brightest 5% of pixels = the emitter
    // fixture), then measure the SAME pixels in both renders.
    const PanelPixels panel = findPanel(baseBuf, w, h, 0.05);
    const double basePanel = meanOverPixels(baseBuf, panel);
    INFO("baseline panel luminance = " << basePanel);
    REQUIRE(basePanel > 0.0);  // the panel is lit in the baseline (sanity)

    // (b) OVERFLOW — a capacity large enough to hold ALL emitter fixture deposits
    // (deposited FIRST, post-fix) but FAR below the ~1.35M total deposits the photon
    // pass produces, so the photon pass overflows while the fixture is fully kept.
    // This isolates the fix precisely: pre-fix the emitter deposits were appended
    // AFTER the photons, so the store was already full and they were ALL dropped
    // (panel -> black) even though it had room reserved-first for them.
    rt_test::RenderScene overflow{emitterScene("300000")};
    const Buffer& ovBuf = overflow.buffer();

    INFO("overflow dropped = " << overflow.result.bounceStoreDropped
         << " emitterDepositsKept = " << overflow.result.emitterDepositsKept);

    // The overflow MUST actually fire (otherwise the test proves nothing). The
    // photon pass alone produces ~1.35M kept deposits at 4M photons, far over 300K.
    REQUIRE(overflow.result.bounceStoreDropped > 0);

    // HEADLINE: under overflow ALL emitter deposits are STILL kept — exactly as many
    // as the no-overflow baseline kept, because they are reserved before the photon
    // flood. Pre-fix this was 0 (the photons filled the store, then every emitter
    // append was dropped). Equality (not just > 0) pins the "reserved first" property.
    REQUIRE(overflow.result.emitterDepositsKept == baseline.result.emitterDepositsKept);

    // HEADLINE: the panel region is NOT BLACK under overflow. Pre-fix it read ~0
    // (every emitter deposit dropped => the gather had nothing to collect on the
    // panel). A small absolute floor decisively separates "lit" from "black".
    const double ovPanel = meanOverPixels(ovBuf, panel);
    INFO("overflow panel luminance = " << ovPanel);
    REQUIRE(ovPanel > 0.10 * basePanel);

    // The emitter density estimate reproduces L = M/pi regardless of how many
    // deposits land in a gather disc (per-deposit power scales by 1/N — DESIGN §6d),
    // so the panel brightness is essentially capacity-independent: the overflow panel
    // matches the baseline within a loose tolerance (the only difference is which
    // PHOTON-bounce indirect deposits survived, a minor contribution on the saturated
    // panel). Pre-fix the ratio was ~0 (black); post-fix it is ~1.
    REQUIRE(ovPanel == Catch::Approx(basePanel).epsilon(0.30));
}
