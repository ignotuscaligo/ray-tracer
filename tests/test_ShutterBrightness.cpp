#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "AnimationQuery.h"
#include "Renderer.h"
#include "SceneLoader.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using Catch::Approx;

namespace
{

// A small, mesh-free Cornell-ish scene: an OmniLight above a diffuse sphere in
// front of a diffuse back wall (a big sphere). Renders fast at low res. Written
// to a temp file so we can load it through the real SceneLoader -> Renderer path.
const char* kStaticScene = R"JSON(
{
  "$materials": {
    "Matte": { "$type": "Diffuse", "$color": [0.8] }
  },
  "$workerConfiguration": {
    "$workerCount": 8,
    "$fetchSize": 50000,
    "$photonQueueSize": 4000000
  },
  "$renderConfiguration": {
    "$width": 48,
    "$height": 48,
    "$photonsPerLight": 2000000,
    "$startFrame": 0,
    "$endFrame": 0,
    "$bounceThreshold": 2,
    "$terminationThreshold": 0.01,
    "SHUTTER_PLACEHOLDER"
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
      "$position": [0.0, 80.0, -60.0],
      "$color": [1.0, 1.0, 1.0],
      "$brightness": 50000
    },
    "Sphere": {
      "$type": "SphereVolume",
      "$material": "Matte",
      "$center": [0.0, 0.0, 0.0],
      "$radius": 40.0
    },
    "BackWall": {
      "$type": "SphereVolume",
      "$material": "Matte",
      "$center": [0.0, 0.0, 400.0],
      "$radius": 300.0
    }
  }
}
)JSON";

std::string writeTempScene(double shutterTime)
{
    std::string json = kStaticScene;
    const std::string placeholder = "\"SHUTTER_PLACEHOLDER\"";
    std::string replacement;
    if (shutterTime > 0.0)
    {
        replacement = "\"$shutterTime\": " + std::to_string(shutterTime) +
                      ", \"$frameRate\": 24.0";
    }
    else
    {
        replacement = "\"$shutterTime\": 0.0";
    }
    const size_t pos = json.find(placeholder);
    json.replace(pos, placeholder.size(), replacement);

    static int counter = 0;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("rt_shutter_test_" + std::to_string(counter++) + ".json");
    std::ofstream out(path);
    out << json;
    out.close();
    return path.string();
}

double renderMeanLuminance(double shutterTime)
{
    const std::string path = writeTempScene(shutterTime);
    LoadedScene scene = SceneLoader::loadFromFile(path, /*logToStdout=*/false);
    std::remove(path.c_str());

    RenderResult result = Renderer::renderFrame(scene);
    REQUIRE_FALSE(result.cameras.empty());
    return result.cameras.front().meanLuminance;
}

}  // namespace

// [INVARIANT] Photometric correctness of the shutter: spreading N photons over a
// non-zero shutter interval must produce the SAME total light energy as an
// instantaneous (zero-shutter) render of the same STATIC scene. The shutter only
// re-tags each photon's TIME; emit() still produces the same count carrying the
// same per-photon flux (DESIGN.md §3), and no photon is dropped (times are sampled
// inside the exposure window so the splat gate always passes). On a static scene
// the animated transform never changes, so the image — and its mean luminance —
// must match within Monte-Carlo noise.
TEST_CASE("Static scene brightness is invariant to shutter time", "[shutter][brightness]")
{
    const double noShutter = renderMeanLuminance(0.0);
    const double withShutter = renderMeanLuminance(0.02);

    INFO("mean luminance: no-shutter=" << noShutter << " with-shutter=" << withShutter);
    REQUIRE(noShutter > 0.0);
    REQUIRE(withShutter > 0.0);

    // Both are unseeded Monte-Carlo renders, so they will not be bit-identical;
    // with 2M photons the mean converges and the relative difference is small.
    const double relDiff = std::abs(withShutter - noShutter) / noShutter;
    INFO("relative difference = " << relDiff);
    REQUIRE(relDiff < 0.03);
}

// Step 2 corroboration at the unit level: an animated transform genuinely differs
// at different times, so casting "at a time" hits a rotated pose. (The render-path
// blur proof lives in the rendered fan sequence.)
TEST_CASE("Keyframed rotation yields a different pose at a later time", "[shutter][animation]")
{
    KeyframedAnimationQuery query;
    KeyframedAnimationQuery::AnimatedObject fan;
    fan.hasRotation = true;
    fan.rotationAxis = Vector{0.0, 0.0, 1.0};
    fan.baseRotation = Quaternion();  // identity scene-load orientation.
    fan.rotationAngle.addKeyframe(0.0, 0.0);
    fan.rotationAngle.addKeyframe(1.0, 3.14159265358979);  // half turn over 1s.
    query.setObject("Fan", fan);

    const auto t0 = query.transformAt("Fan", 0.0f);
    const auto t1 = query.transformAt("Fan", 1.0f);
    REQUIRE(t0.has_value());
    REQUIRE(t1.has_value());

    // Rotating a +X point by ~0 vs ~pi about +Z: at t0 it stays near +X, at t1 it
    // flips to near -X. The poses must differ.
    const Vector probe{1.0, 0.0, 0.0};
    const Vector p0 = t0->rotation * probe;
    const Vector p1 = t1->rotation * probe;
    REQUIRE(p0.x == Approx(1.0).margin(1e-6));
    REQUIRE(p1.x == Approx(-1.0).margin(1e-6));

    // An object with no animation entry returns nullopt (falls back to static).
    REQUIRE_FALSE(query.transformAt("SomethingElse", 0.5f).has_value());
}
