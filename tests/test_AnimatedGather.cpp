#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "AnimationQuery.h"
#include "Image.h"
#include "Quaternion.h"
#include "Ray.h"
#include "Renderer.h"
#include "SceneLoader.h"
#include "SphereVolume.h"
#include "Vector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Catch::Approx;

// These two tests close the gap that hid the time-blind probe gather (Fable-5
// review item 1a): there was NO animated-geometry intersection-at-time test and NO
// gather-at-time test, so a hardcoded time=0 on the camera side passed CI while
// rendering frame-0 geometry in the default ($probeGather true) path.

// ===== Test 1: Volume::castRayAt resolves the animated pose AT THE RAY TIME =====
//
// A moving object must be intersected at the pose it holds at the ray's time, not
// its scene-load pose. This is the photon/camera-side primitive the gather relies
// on; if it were time-blind here, motion blur and per-frame visibility would both
// be impossible.
TEST_CASE("castRayAt intersects a moving sphere at its time-resolved pose",
          "[animation][intersection]")
{
    // Sphere of radius 10 loaded at the origin, translating +X at 100 units/sec.
    auto sphere = std::make_shared<SphereVolume>();
    sphere->name("Mover");
    sphere->center(Vector{0.0, 0.0, 0.0});
    sphere->radius(10.0);
    sphere->transform.position = Vector{0.0, 0.0, 0.0};

    TranslatingAnimationQuery anim("Mover",
                                   /*basePosition=*/Vector{0.0, 0.0, 0.0},
                                   /*baseRotation=*/Quaternion(),
                                   /*velocity=*/Vector{100.0, 0.0, 0.0});

    std::vector<Hit> castBuffer;

    // A ray down +Z aimed at x = 100 (where the sphere is at t = 1, NOT at t = 0).
    const Ray ray{Vector{100.0, 0.0, -50.0}, Vector{0.0, 0.0, 1.0}};

    // At t = 0 the sphere is still at the origin -> the x=100 ray MISSES it.
    const std::optional<Hit> atZero = sphere->castRayAt(ray, castBuffer, 0.0f, &anim);
    REQUIRE_FALSE(atZero.has_value());

    // At t = 1 the sphere has translated to x = 100 -> the ray HITS it, and the hit
    // point sits on the translated sphere (x within radius of 100, front face near
    // z = -10 of the moved center at z = 0).
    const std::optional<Hit> atOne = sphere->castRayAt(ray, castBuffer, 1.0f, &anim);
    REQUIRE(atOne.has_value());
    REQUIRE(atOne->position.x == Approx(100.0).margin(1e-6));
    REQUIRE(atOne->position.z == Approx(-10.0).margin(1e-6));  // front face at center.z - r

    // Sanity: at t = 0 a ray aimed at the ORIGIN still hits (the sphere is there).
    const Ray originRay{Vector{0.0, 0.0, -50.0}, Vector{0.0, 0.0, 1.0}};
    const std::optional<Hit> originHit = sphere->castRayAt(originRay, castBuffer, 0.0f, &anim);
    REQUIRE(originHit.has_value());
    REQUIRE(originHit->position.x == Approx(0.0).margin(1e-6));
}

namespace
{

// A minimal probe-mode scene: an OmniLight over a single bright diffuse sphere in
// front of a large diffuse back wall. The sphere is animated to TRANSLATE +X, so a
// later frame must render it shifted to the right of frame 0 — but only if the
// camera-side gather casts at the frame time (the bug: it cast at 0, pinning the
// sphere at its frame-0 screen position). Zero shutter so each frame is a crisp
// single-instant pose (no blur to confound the position check). Probe gather is the
// DEFAULT, which is exactly the path under test.
const char* kMovingScene = R"JSON(
{
  "$materials": {
    "Matte": { "$type": "Diffuse", "$color": [0.9] }
  },
  "$workerConfiguration": {
    "$workerCount": 8,
    "$fetchSize": 50000,
    "$photonQueueSize": 4000000
  },
  "$renderConfiguration": {
    "$width": 96,
    "$height": 96,
    "$photonsPerLight": 6000000,
    "$startFrame": 0,
    "$endFrame": 0,
    "$frameRate": 1.0,
    "$shutterTime": 0.0,
    "$bounceThreshold": 2,
    "$terminationThreshold": 0.01,
    "$probeGather": true
  },
  "$scene": {
    "Camera": {
      "$type": "Camera",
      "$verticalFieldOfView": 60.0,
      "$position": [0.0, 0.0, -200.0],
      "$rotation": { "$type": "PitchYawRollDegrees", "$value": [0.0, 0.0, 0.0] }
    },
    "Light": {
      "$type": "OmniLight",
      "$position": [0.0, 0.0, -130.0],
      "$color": [1.0, 1.0, 1.0],
      "$brightness": 300000
    },
    "Mover": {
      "$type": "SphereVolume",
      "$material": "Matte",
      "$center": [-35.0, 0.0, -30.0],
      "$radius": 26.0,
      "$animation": {
        "$position": [
          { "t": 0.0, "value": [-35.0, 0.0, -30.0] },
          { "t": 1.0, "value": [ 35.0, 0.0, -30.0] }
        ]
      }
    },
    "BackWall": {
      "$type": "SphereVolume",
      "$material": "Matte",
      "$center": [0.0, 0.0, 500.0],
      "$radius": 380.0
    }
  }
}
)JSON";

std::string writeMovingScene()
{
    static int counter = 0;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("rt_anim_gather_" + std::to_string(counter++) + ".json");
    std::ofstream out(path);
    out << kMovingScene;
    out.close();
    return path.string();
}

// Mean x (column) of the brightest pixels in the rendered image — the horizontal
// centroid of the lit sphere. A sphere translated +X must move this centroid to a
// different column.
double brightColumnCentroid(Image& image)
{
    const size_t w = image.width();
    const size_t h = image.height();

    auto luminance = [&](size_t x, size_t y) {
        const Pixel& p = image.getPixel(x, y);
        return 0.2126 * static_cast<double>(p.red) +
               0.7152 * static_cast<double>(p.green) +
               0.0722 * static_cast<double>(p.blue);
    };

    // Find the peak luminance, then take the centroid of pixels within 60% of it
    // (the bright sphere) ignoring the dim back wall.
    double peak = 0.0;
    for (size_t y = 0; y < h; ++y)
    {
        for (size_t x = 0; x < w; ++x)
        {
            peak = std::max(peak, luminance(x, y));
        }
    }
    REQUIRE(peak > 0.0);

    // Luminance-weighted column centroid over the lit pixels (those above 40% of
    // peak — the sphere; the distant wall is near-black). Weighting by luminance
    // (rather than a binary count) is robust to the peak varying between the two
    // unseeded Monte-Carlo renders.
    const double thresh = 0.4 * peak;
    double sumX = 0.0;
    double weight = 0.0;
    for (size_t y = 0; y < h; ++y)
    {
        for (size_t x = 0; x < w; ++x)
        {
            const double lum = luminance(x, y);
            if (lum >= thresh)
            {
                sumX += static_cast<double>(x) * lum;
                weight += lum;
            }
        }
    }
    REQUIRE(weight > 0.0);
    return sumX / weight;
}

double renderBrightCentroid(double frameTimeSeconds)
{
    const std::string path = writeMovingScene();
    LoadedScene scene = SceneLoader::loadFromFile(path, /*logToStdout=*/false);
    std::remove(path.c_str());

    // renderFrame reads settings.frameTime directly (main.cpp's per-frame loop is
    // what normally derives it from frame/frameRate); set it here to sample the
    // animated pose at the requested instant. Zero shutter -> a crisp single pose.
    scene.settings.frameTime = frameTimeSeconds;

    RenderResult result = Renderer::renderFrame(scene);
    REQUIRE_FALSE(result.cameras.empty());
    REQUIRE(result.cameras.front().image);
    return brightColumnCentroid(*result.cameras.front().image);
}

}  // namespace

// ===== Test 2: the DEFAULT probe gather renders an object at its time pose =====
//
// A sphere animated to translate from x=-50 (t=0) to x=+50 (t=1) must render
// shifted to the RIGHT at the later frame. With the camera-side gather hardcoded to
// time=0 (the bug), both frames would put the sphere at the same screen column and
// this test fails. The camera looks down -Z (right-handed: +X world maps to one
// screen side consistently), so the only requirement is that the centroid MOVES by
// a meaningful amount between the two time samples — and that frame 0 is NOT at the
// translated (frame-1) position.
TEST_CASE("Probe gather renders a translated-at-t=1 object at its moved position",
          "[animation][gather][ProbeGather]")
{
    const double centroidT0 = renderBrightCentroid(0.0);  // sphere at x = -50
    const double centroidT1 = renderBrightCentroid(1.0);  // sphere at x = +50

    INFO("bright-sphere column centroid: t0=" << centroidT0 << " t1=" << centroidT1);

    // The sphere moved 70 world units in +X; the rendered centroid shifts by ~38
    // columns on the 96-wide frame (measured stable across unseeded runs). 20 px is a
    // robust floor far above the centroid noise and far below the expected shift, so
    // a regression to a time-blind camera ray (time=0, zero shift) fails hard.
    REQUIRE(std::abs(centroidT1 - centroidT0) > 20.0);
}
