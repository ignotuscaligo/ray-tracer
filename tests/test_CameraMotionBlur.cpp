#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "AnimationQuery.h"
#include "Camera.h"
#include "Image.h"
#include "Quaternion.h"
#include "Ray.h"
#include "Renderer.h"
#include "SceneLoader.h"
#include "Vector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Catch::Approx;

// ===== Camera motion blur (DESIGN.md §9, camera-transform-at-time) =====
//
// A ray cast at shutter time t must originate from the camera's pose AT t. When the
// CAMERA itself moves/rotates fast across a non-zero shutter, the (even static)
// geometry it images must smear along the camera's motion — directional motion blur.
// The bug these tests guard against: the probe pass + gather threaded a per-sample
// shutter TIME into geometry intersection but generated the camera ray from the
// camera's STATIC scene-load pose, so a fast-moving camera did NOT blur (static
// geometry stayed sharp; only object motion smeared). See the §9 history note.

// ----- Unit: resolveEyeRotationAt evaluates the camera pose at the ray time -----
//
// This is the camera-side analogue of Volume::resolveTransformAt. A non-animated
// camera (or a null query) must return its scene-load pose for EVERY time (static
// parity); an animated camera must return the interpolated pose at the ray time.
TEST_CASE("Camera resolves an animated eye position at the ray time",
          "[camera][animation][motionblur]")
{
    Camera camera(64, 64, 90.0);
    camera.name("Cam");
    camera.transform.position = Vector{0.0, 0.0, -100.0};
    camera.transform.rotation = Quaternion();

    // Null animation -> static scene-load pose at any time (parity).
    {
        const Camera::EyeRotation at0 = camera.resolveEyeRotationAt(0.0f, nullptr);
        const Camera::EyeRotation at5 = camera.resolveEyeRotationAt(5.0f, nullptr);
        REQUIRE(at0.eye.x == Approx(0.0).margin(1e-9));
        REQUIRE(at5.eye.x == Approx(0.0).margin(1e-9));  // time-independent
        REQUIRE(at0.eye.z == Approx(-100.0).margin(1e-9));
    }

    // A query that has NO entry for this camera -> fall back to the static pose.
    {
        StaticAnimationQuery staticQuery;
        const Camera::EyeRotation at0 = camera.resolveEyeRotationAt(0.0f, &staticQuery);
        REQUIRE(at0.eye.x == Approx(0.0).margin(1e-9));
        REQUIRE(at0.eye.z == Approx(-100.0).margin(1e-9));
    }

    // An animated camera: translate +X at 80 units/sec. resolveEyeRotationAt must
    // return the translated eye, and generatePrimaryRayAt must originate the ray
    // there (the static generatePrimaryRay must NOT — it ignores time).
    {
        TranslatingAnimationQuery anim("Cam",
                                       /*basePosition=*/Vector{0.0, 0.0, -100.0},
                                       /*baseRotation=*/Quaternion(),
                                       /*velocity=*/Vector{80.0, 0.0, 0.0});

        const Camera::EyeRotation at0 = camera.resolveEyeRotationAt(0.0f, &anim);
        const Camera::EyeRotation at1 = camera.resolveEyeRotationAt(1.0f, &anim);
        REQUIRE(at0.eye.x == Approx(0.0).margin(1e-6));
        REQUIRE(at1.eye.x == Approx(80.0).margin(1e-6));

        const PixelCoords center{32, 32};
        const Ray rayStatic = camera.generatePrimaryRay(center);
        const Ray rayAt1 = camera.generatePrimaryRayAt(center, 1.0f, &anim);
        // The time-aware ray originates from the moved eye; the static ray does not.
        REQUIRE(rayStatic.origin.x == Approx(0.0).margin(1e-6));
        REQUIRE(rayAt1.origin.x == Approx(80.0).margin(1e-6));
        // ... and the time-aware ray at t=0 matches the static ray (parity).
        const Ray rayAt0 = camera.generatePrimaryRayAt(center, 0.0f, &anim);
        REQUIRE(rayAt0.origin.x == Approx(rayStatic.origin.x).margin(1e-9));
    }
}

namespace
{

// A fully STATIC scene with a single hard VERTICAL color edge: a red half-space
// (left) meets a green half-space (right) on a FAR wall, lit by an omni light. The
// camera translates in +X across the shutter. A static render has a crisp vertical
// red/green seam (a steep red-channel falloff at the boundary); a moving-camera
// render smears that boundary horizontally (the falloff widens / flattens). The wall
// is placed FAR back so a lateral camera translation slides the image (smearing the
// seam) with only mild parallax — the seam stays a near-vertical line near frame
// center, so the row-averaged red profile cleanly isolates the camera-motion smear.
//
// {MOVING} is replaced with the camera $animation block (moving variant) or removed
// (static variant); {SHUTTER}/{SAMPLES}/{OFFSET} are substituted per variant.
const char* kEdgeSceneTemplate = R"JSON(
{
  "$materials": {
    "RedM":   { "$type": "Diffuse", "$color": [0.9, 0.0, 0.0] },
    "GreenM": { "$type": "Diffuse", "$color": [0.0, 0.9, 0.0] }
  },
  "$workerConfiguration": {
    "$workerCount": 8,
    "$fetchSize": 50000,
    "$photonQueueSize": 4000000
  },
  "$renderConfiguration": {
    "$width": 96,
    "$height": 96,
    "$photonsPerLight": 3000000,
    "$startFrame": 0,
    "$endFrame": 0,
    "$frameRate": 1.0,
    "$shutterTime": {SHUTTER},
    "$cameraTimeSamples": {SAMPLES},
    "$frameOffset": {OFFSET},
    "$bounceThreshold": 1,
    "$terminationThreshold": 0.01,
    "$bounceStoreCapacity": 60000000,
    "$probeGather": true
  },
  "$scene": {
    "Camera": {
      "$type": "Camera",
      "$verticalFieldOfView": 45.0,
      "$position": [0.0, 0.0, -200.0],
      "$rotation": { "$type": "PitchYawRollDegrees", "$value": [0.0, 0.0, 0.0] }{MOVING}
    },
    "Light": {
      "$type": "OmniLight",
      "$position": [0.0, 0.0, 400.0],
      "$color": [1.0, 1.0, 1.0],
      "$brightness": 6000000
    },
    "RedHalf": {
      "$type": "SphereVolume",
      "$material": "RedM",
      "$center": [-2400.0, 0.0, 800.0],
      "$radius": 2400.0
    },
    "GreenHalf": {
      "$type": "SphereVolume",
      "$material": "GreenM",
      "$center": [2400.0, 0.0, 800.0],
      "$radius": 2400.0
    }
  }
}
)JSON";

// The camera $animation block: translate from x=-40 (t=0) to x=+40 (t=1). Over the
// full shutter [0,1) this slides the eye 80 world units laterally. Against the far
// wall (z ~ 800) that is a several-pixel image slide — enough to smear the vertical
// seam, with only mild parallax (the wall is ~5x farther than the translation).
const char* kCameraAnimation = R"JSON(,
      "$animation": {
        "$position": [
          { "t": 0.0, "value": [-40.0, 0.0, -200.0] },
          { "t": 1.0, "value": [ 40.0, 0.0, -200.0] }
        ]
      })JSON";

std::string substitute(std::string s, const std::string& key, const std::string& value)
{
    const std::size_t pos = s.find(key);
    REQUIRE(pos != std::string::npos);
    return s.replace(pos, key.size(), value);
}

std::string writeScene(bool moving)
{
    std::string scene = kEdgeSceneTemplate;
    if (moving)
    {
        scene = substitute(scene, "{MOVING}", kCameraAnimation);
        scene = substitute(scene, "{SHUTTER}", "1.0");
        scene = substitute(scene, "{SAMPLES}", "32");
        scene = substitute(scene, "{OFFSET}", "0.0");
    }
    else
    {
        // Static reference: no camera animation, zero shutter, rendered at the
        // SHUTTER MIDPOINT (frameOffset 0.5 -> the moving camera's x=0 centroid
        // pose) so it is the sharp same-viewpoint counterpart of the blurred run.
        scene = substitute(scene, "{MOVING}", "");
        scene = substitute(scene, "{SHUTTER}", "0.0");
        scene = substitute(scene, "{SAMPLES}", "1");
        scene = substitute(scene, "{OFFSET}", "0.5");
    }

    static int counter = 0;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("rt_cammb_" + std::to_string(counter++) + ".json");
    std::ofstream out(path);
    out << scene;
    out.close();
    return path.string();
}

// Steepness of the red/green SEAM: build the row-AVERAGED red-channel column profile
// (averaging over the central rows beats down per-pixel Monte-Carlo noise), then take
// the maximum absolute column-to-column step in it. The seam is where red falls from
// its plateau to ~0; a SHARP image makes that a steep one/two-column drop (large max
// step), while horizontal camera motion blur spreads the drop over many columns
// (small max step). Row-averaging + isolating the single steepest step makes this a
// clean, direction-specific blur metric robust to the unseeded render noise.
double seamSteepness(Image& image)
{
    const size_t w = image.width();
    const size_t h = image.height();
    const size_t y0 = h / 4;
    const size_t y1 = (3 * h) / 4;
    const double rows = static_cast<double>(y1 - y0);

    std::vector<double> redProfile(w, 0.0);
    for (size_t x = 0; x < w; ++x)
    {
        double sum = 0.0;
        for (size_t y = y0; y < y1; ++y)
        {
            sum += static_cast<double>(image.getPixel(x, y).red);
        }
        redProfile[x] = sum / rows;
    }

    double maxStep = 0.0;
    for (size_t x = 1; x < w; ++x)
    {
        maxStep = std::max(maxStep, std::abs(redProfile[x] - redProfile[x - 1]));
    }
    return maxStep;
}

double renderSeamSteepness(bool moving)
{
    const std::string path = writeScene(moving);
    LoadedScene scene = SceneLoader::loadFromFile(path, /*logToStdout=*/false);
    std::remove(path.c_str());

    // renderFrame reads settings.frameTime directly (main.cpp normally derives it
    // from frame/frameRate + frameOffset). Mirror that: frameTime = frameOffset.
    scene.settings.frameTime = scene.settings.frameOffset;

    RenderResult result = Renderer::renderFrame(scene);
    REQUIRE_FALSE(result.cameras.empty());
    REQUIRE(result.cameras.front().image);
    return seamSteepness(*result.cameras.front().image);
}

}  // namespace

// ===== Regression: a fast-translating camera blurs static geometry =====
//
// Same static scene, same viewpoint centroid, two renders: a moving camera over a
// full shutter vs a static camera at the shutter-midpoint pose. The moving render's
// vertical red/green seam must be measurably SOFTER (lower horizontal edge energy).
// With the pre-fix time-blind camera ray-gen both renders would be the identical
// sharp static-pose image and the ratio would be ~1.0 — this test fails hard then.
TEST_CASE("A fast-translating camera motion-blurs static geometry",
          "[camera][animation][motionblur][ProbeGather]")
{
    const double sharpSeam = renderSeamSteepness(/*moving=*/false);
    const double blurredSeam = renderSeamSteepness(/*moving=*/true);

    INFO("seam steepness (max red column step): static(sharp)=" << sharpSeam
         << " moving(blurred)=" << blurredSeam
         << " ratio=" << (blurredSeam / sharpSeam));

    // The static seam is a steep one/two-column red falloff; the moving camera
    // spreads it across many columns, dropping the max step. Require the moving
    // render's seam to be at least 30% shallower than the static one — a margin far
    // above run-to-run Monte-Carlo noise yet failing decisively if the camera
    // ray-gen ever regresses to a static (time-blind) pose (the two seams would then
    // be the identical sharp profile, ratio ~1.0).
    REQUIRE(sharpSeam > 0.0);
    REQUIRE(blurredSeam < 0.7 * sharpSeam);
}
