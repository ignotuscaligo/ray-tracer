#include <catch2/catch_test_macros.hpp>

#include "Image.h"
#include "Renderer.h"
#include "SceneLoader.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

// ===== Regression: a surface reached only via a glass MINORITY Fresnel branch =====
//
// THE BUG this pins (the reason the camera-side specular trace was unified into the
// probe pass): under the OLD split probe/gather, the probe pass took ONE stochastic
// Fresnel pick per glass pixel while the gather took kCameraSamplesPerPixel (16).
// At near-normal incidence a dielectric's Fresnel reflectance is ~4% (R0 for ior
// 1.5), so REFLECTION is the MINORITY branch: a 1-sample probe almost never picks
// it. A surface visible only through that minority reflection therefore received no
// probes -> the photon-pass keep-test culled its deposits -> the 16-sample gather
// arrived at a surface with no deposits and the reflection read DARK / neutral.
//
// The fix makes the probe pass the SINGLE camera-side specular tracer that takes
// ALL 16 Fresnel branches, so every gather point IS a probe by construction and the
// 1-vs-16 mismatch cannot cull anything.
//
// SCENE: the camera sits INSIDE an enclosed white box looking +z at a glass sphere.
// Directly behind the sphere (the camera sees this through REFRACTION, the ~96%
// MAJORITY branch) is a near-black wall. Behind the CAMERA (the sphere's front-face
// REFLECTION, the ~4% MINORITY branch, sends this toward the eye) is a strongly RED
// wall. So at the sphere CENTER the only colored signal is the red wall reached via
// the minority reflection branch.
//
// PRE-FIX: that red wall is reached only by the 1-sample probe's rare reflection
// pick, so most of it is culled and the sphere center reads ~neutral (R ~ G ~ B):
// measured R/G ~ 1.09. POST-FIX: the reflection is fully probed and gathered, so the
// sphere center is clearly RED: measured R/G ~ 1.7. The assertion (R > 1.3*G) sits
// decisively between the two and is stable across probeSubSample (the ratio is
// coverage-by-construction; only the absolute brightness scales with subsampling).

namespace
{
std::string writeMinorityFresnelScene()
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rt_minority_fresnel_test.json";
    std::ofstream out(path);
    out << R"JSON({
    "$materials": {
        "Glass":  { "$type": "Glass", "$ior": 1.5 },
        "RedM":   { "$type": "Diffuse", "$color": [0.9, 0.04, 0.04] },
        "DarkM":  { "$type": "Diffuse", "$color": [0.02, 0.02, 0.02] },
        "WhiteM": { "$type": "Diffuse", "$color": [0.6, 0.6, 0.6] }
    },
    "$workerConfiguration": { "$workerCount": 8, "$fetchSize": 50000, "$photonQueueSize": 12000000 },
    "$renderConfiguration": {
        "$width": 96, "$height": 96, "$photonsPerLight": 12000000,
        "$startFrame": 0, "$endFrame": 0, "$renderPath": "renders",
        "$renderName": "minorityfresnel", "$bounceThreshold": 2, "$probeGather": true,
        "$bounceStoreCapacity": 160000000
    },
    "$scene": {
        "Camera": { "$type": "Camera", "$verticalFieldOfView": 45.0, "$position": [0.0, 150.0, -140.0],
            "$rotation": { "$type": "PitchYawRollDegrees", "$value": [0.0, 0.0, 0.0] } },
        "Light": { "$type": "AreaLight", "$shape": "Square", "$size": 120, "$position": [0, 298, 0],
            "$rotation": { "$type": "PitchYawRollDegrees", "$value": [90, 0, 0] },
            "$color": [1.0, 1.0, 1.0], "$luminousFlux": 125663706 },
        "Box": { "$type": "Object",
            "Ceiling": { "$type": "Quad", "$material": "WhiteM", "$origin": [-155, 300, 155], "$edgeU": [310, 0, 0], "$edgeV": [0, 0, -310] },
            "Floor":   { "$type": "Quad", "$material": "WhiteM", "$origin": [-155, 0, 155],   "$edgeU": [310, 0, 0], "$edgeV": [0, 0, -310] },
            "Left":    { "$type": "Quad", "$material": "WhiteM", "$origin": [150, -5, 155],   "$edgeU": [0, 310, 0], "$edgeV": [0, 0, -310] },
            "Right":   { "$type": "Quad", "$material": "WhiteM", "$origin": [-150, -5, 155],  "$edgeU": [0, 310, 0], "$edgeV": [0, 0, -310] },
            "BackDark":  { "$type": "Quad", "$material": "DarkM", "$origin": [-155, -5, 150], "$edgeU": [310, 0, 0], "$edgeV": [0, 310, 0] },
            "FrontRed":  { "$type": "Quad", "$material": "RedM",  "$origin": [155, -5, -150], "$edgeU": [-310, 0, 0], "$edgeV": [0, 310, 0] }
        },
        "GlassSphere": { "$type": "SphereVolume", "$material": "Glass", "$center": [0.0, 150.0, 30.0], "$radius": 55.0 }
    }
})JSON";
    out.close();
    return path.string();
}

// Mean of one channel over the central 1/8 box (the glass sphere center, where the
// only colored signal is the red wall reached via the minority reflection branch).
double centerMeanChannel(const Image& image, int channel)
{
    Image& m = const_cast<Image&>(image);
    const std::size_t w = image.width();
    const std::size_t h = image.height();
    const std::size_t x0 = w * 7 / 16;
    const std::size_t x1 = w * 9 / 16;
    const std::size_t y0 = h * 7 / 16;
    const std::size_t y1 = h * 9 / 16;
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t y = y0; y < y1; ++y)
    {
        for (std::size_t x = x0; x < x1; ++x)
        {
            sum += static_cast<double>(m.getPixel(x, y)[static_cast<std::size_t>(channel)]);
            ++n;
        }
    }
    return n ? sum / static_cast<double>(n) : 0.0;
}
}  // namespace

TEST_CASE("A surface seen only via the minority Fresnel branch of glass is gathered, not culled",
          "[ProbeGather][glass][fresnel][regression]")
{
    const std::string path = writeMinorityFresnelScene();
    LoadedScene scene = SceneLoader::loadFromFile(path, /*logToStdout=*/false);
    std::remove(path.c_str());

    RenderResult result = Renderer::renderFrame(scene);
    REQUIRE_FALSE(result.cameras.empty());
    REQUIRE(result.cameras.front().image);

    const Image& image = *result.cameras.front().image;
    const double r = centerMeanChannel(image, 0);
    const double g = centerMeanChannel(image, 1);
    const double b = centerMeanChannel(image, 2);

    INFO("sphere-center channels (16-bit): R=" << r << " G=" << g << " B=" << b
         << " R/G=" << (g > 0.0 ? r / g : 0.0));

    // The sphere center must be LIT (the gather found deposits at all — a fully
    // culled reflection would read ~0 here).
    REQUIRE(r > 1000.0);

    // HEADLINE: the reflected RED wall — reachable only through the ~4% minority
    // reflection branch — is clearly present, so RED dominates the neutral
    // refraction/backdrop. Pre-fix the minority branch was culled and the center
    // read ~neutral (R/G ~ 1.09); the unified probe-as-gather tracer keeps it (R/G
    // ~ 1.7). 1.3 sits decisively between, and the ratio is stable across
    // probeSubSample (coverage is by construction, not by sample luck).
    REQUIRE(g > 0.0);
    REQUIRE(r > 1.3 * g);
    REQUIRE(r > 1.3 * b);
}
