#include <catch2/catch_all.hpp>

#include "FurnaceFixture.h"
#include "RenderFixture.h"
#include "StatAssert.h"

#include <string>

// ============================================================================
// Determinism + ThreadEquivalence (T8 sibling + the deterministic-mode guarantee)
// ============================================================================
//
// Two distinct claims about the RNG/threading infrastructure this stage added:
//
//   1. SINGLE-THREAD DETERMINISTIC MODE is BITWISE-reproducible. Two independent
//      renderFrame calls on a $deterministic scene produce byte-for-byte identical
//      float buffers. This is the owner's single-thread decision: one worker + a
//      seeded RNG + a single-threaded gather make the whole frame one fixed
//      sequence of RNG draws and buffer adds. (Multi-thread atomic-float adds are
//      non-associative, so bitwise determinism is UNATTAINABLE there — which is why
//      the mode collapses to one worker.)
//
//   2. THREAD EQUIVALENCE: a 1-worker render and an N-worker render of the same
//      (non-deterministic, seeded) scene agree in MEAN LUMINANCE in expectation.
//      Bitwise equality is NOT expected (atomic-float non-associativity); the means
//      converge because the photon set and per-photon energy are identical, only the
//      thread interleaving differs.

namespace
{
// A small diffuse scene rendered at a given worker count. Built mesh-free so it
// renders fast; an OmniLight over a diffuse sphere in front of a big back-wall
// sphere (the test_ShutterBrightness scene, parameterized).
std::string sceneWithWorkers(size_t workerCount, std::uint32_t seed)
{
    std::string s = R"JSON({
  "$materials": { "Matte": { "$type": "Diffuse", "$color": [0.7] } },
  "$workerConfiguration": { "$workerCount": )JSON";
    s += std::to_string(workerCount);
    s += R"JSON(, "$fetchSize": 20000, "$photonQueueSize": 4000000 },
  "$renderConfiguration": {
    "$width": 48, "$height": 48, "$photonsPerLight": 1500000,
    "$bounceThreshold": 2, "$terminationThreshold": 0.01,
    "$seed": )JSON";
    s += std::to_string(seed);
    s += R"JSON(
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

TEST_CASE("Determinism: single-thread deterministic mode is bitwise-reproducible",
          "[Determinism][T8]")
{
    rt_test::FurnaceParams p;
    p.photonsPerLight = 40000;
    p.bounceThreshold = 3;
    p.seed = 555;

    rt_test::RenderScene furnace{rt_test::buildFurnaceScene(p)};
    const RenderResult second = furnace.renderAgain();

    REQUIRE(furnace.result.buffer != nullptr);
    REQUIRE(second.buffer != nullptr);

    // The whole pre-tonemap float image must be byte-for-byte identical.
    const bool identical = rt_test::buffersBitwiseEqual(
        *furnace.result.buffer, *second.buffer, furnace.width(), furnace.height());
    REQUIRE(identical);

    // And the deposit ledger must match bitwise too (same RNG draw sequence => same
    // deposits in the same order).
    REQUIRE(furnace.result.bounceStore->size() == second.bounceStore->size());
    REQUIRE(rt_test::sumDepositedPower(*furnace.result.bounceStore) ==
            rt_test::sumDepositedPower(*second.bounceStore));
}

TEST_CASE("Determinism: a non-deterministic seeded render is NOT bitwise-reproducible "
          "across worker counts, but the mean agrees",
          "[Determinism][ThreadEquivalence][T8]")
{
    // 1 worker vs 8 workers, same seed. The means converge (same photon energy);
    // bitwise equality is not expected (atomic-float add is non-associative).
    rt_test::RenderScene one{sceneWithWorkers(1, 31337)};
    rt_test::RenderScene many{sceneWithWorkers(8, 31337)};

    const double m1 = one.meanLuminance();
    const double m8 = many.meanLuminance();
    REQUIRE(m1 > 0.0);
    REQUIRE(m8 > 0.0);

    // Mean luminance agrees within a small relative band. The two runs are NOT the
    // same photon set: with a seed set but deterministic==false the workers seed from
    // baseSeed + workerIndex, and 1 vs 8 workers partition the emission differently, so
    // the realized photon paths differ — this is a Monte-Carlo agreement (the EXPECTED
    // mean is identical; the realized means differ by sampling noise), not a bitwise
    // one. A 2% band sits well above that noise floor at 1.5M photons while still
    // failing any real per-thread bias (a dropped/double-counted partition would shift
    // the mean by tens of percent).
    const double rel = std::abs(m1 - m8) / m1;
    INFO("mean1=" << m1 << " mean8=" << m8 << " rel=" << rel);
    REQUIRE(rel < 0.02);
}
