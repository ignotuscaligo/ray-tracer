#include <catch2/catch_test_macros.hpp>

#include "Image.h"
#include "Renderer.h"
#include "SceneLoader.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

// ===== Multi-camera probe union (review 1b) =====
//
// In probe mode the photon-pass KEEP-TEST retains a non-delta bounce only if a
// probe lies within the keep-radius; bounces far from every probe are discarded
// as un-gatherable. The bug: probes were collected for the PRIMARY camera only,
// yet the gather runs for EVERY camera. So a SECONDARY camera viewing geometry
// the primary cannot see gathers from a store whose deposits there were culled —
// its image goes dark/black with no warning.
//
// This scene puts two cameras at the same point facing OPPOSITE directions: the
// forward camera (index 0, the primary) sees a RED sphere at +z; the backward
// camera (index 1) sees a BLUE sphere at -z that the primary never images. With
// the primary-only bug the backward camera's blue sphere is black (its deposits
// were culled); with the all-camera probe UNION both spheres are lit. The test
// asserts BOTH cameras have a bright, correctly-colored center region.

namespace
{
std::string writeMultiCameraScene()
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rt_multicam_probe_test.json";
    std::ofstream out(path);
    out << R"JSON({
    "$materials": {
        "RedDiffuse": { "$type": "Diffuse", "$color": [1.0, 0.0, 0.0] },
        "BlueDiffuse": { "$type": "Diffuse", "$color": [0.0, 0.0, 1.0] }
    },
    "$workerConfiguration": { "$workerCount": 4, "$fetchSize": 2000 },
    "$renderConfiguration": {
        "$width": 64, "$height": 64, "$photonsPerLight": 3000000,
        "$startFrame": 0, "$endFrame": 0, "$renderPath": "renders",
        "$renderName": "multicamprobe", "$bounceThreshold": 2, "$probeGather": true
    },
    "$scene": {
        "CameraForward": { "$type": "Camera", "$outputName": "forward", "$width": 64, "$height": 64,
            "$verticalFieldOfView": 60.0, "$position": [0.0, 150.0, 0.0],
            "$rotation": { "$type": "PitchYawRollDegrees", "$value": [0.0, 0.0, 0.0] } },
        "CameraBackward": { "$type": "Camera", "$outputName": "backward", "$width": 64, "$height": 64,
            "$verticalFieldOfView": 60.0, "$position": [0.0, 150.0, 0.0],
            "$rotation": { "$type": "PitchYawRollDegrees", "$value": [0.0, 180.0, 0.0] } },
        "Light": { "$type": "OmniLight", "$position": [0.0, 150.0, 0.0], "$color": [1.0, 1.0, 1.0], "$intensityCandela": 20000000 },
        "RedSphere": { "$type": "SphereVolume", "$material": "RedDiffuse", "$center": [0.0, 150.0, 300.0], "$radius": 80.0 },
        "BlueSphere": { "$type": "SphereVolume", "$material": "BlueDiffuse", "$center": [0.0, 150.0, -300.0], "$radius": 80.0 }
    }
})JSON";
    out.close();
    return path.string();
}

// Mean of one channel over the central 1/3 box of the image (where the sphere
// projects). channel: 0=R, 1=G, 2=B.
double centerMeanChannel(const Image& image, int channel)
{
    Image& m = const_cast<Image&>(image);
    const std::size_t w = image.width();
    const std::size_t h = image.height();
    const std::size_t x0 = w / 3;
    const std::size_t x1 = 2 * w / 3;
    const std::size_t y0 = h / 3;
    const std::size_t y1 = 2 * h / 3;
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
} // namespace

TEST_CASE("Probe gather lights every camera, not just the primary", "[MultiCamera][ProbeGather]")
{
    const std::string path = writeMultiCameraScene();
    LoadedScene scene = SceneLoader::loadFromFile(path, /*logToStdout=*/false);
    std::remove(path.c_str());

    RenderResult result = Renderer::renderFrame(scene);

    REQUIRE(result.cameras.size() == 2);
    REQUIRE(result.cameras[0].image);
    REQUIRE(result.cameras[1].image);

    // The two cameras face opposite directions, so each images exactly ONE of the
    // two single-colored spheres (red at +z, blue at -z). The renderer's forward
    // convention determines which camera sees which; we don't hard-code it. What
    // matters for the bug: BOTH cameras must be lit, with DIFFERENT dominant
    // colors. With the primary-only probe bug the SECONDARY camera's sphere is
    // black (its deposits were culled); the probe union lights both.
    const double r0 = centerMeanChannel(*result.cameras[0].image, 0);
    const double b0 = centerMeanChannel(*result.cameras[0].image, 2);
    const double r1 = centerMeanChannel(*result.cameras[1].image, 0);
    const double b1 = centerMeanChannel(*result.cameras[1].image, 2);

    INFO("cam0 R=" << r0 << " B=" << b0 << " | cam1 R=" << r1 << " B=" << b1);

    // Each camera's center is brightly lit in its sphere's color (16-bit channels:
    // a lit sphere reads in the thousands; a culled/black region is ~0). The
    // dominant channel differs between the two cameras (red vs blue).
    const double dominant0 = std::max(r0, b0);
    const double dominant1 = std::max(r1, b1);

    // HEADLINE: NEITHER camera is black. The primary-only bug makes the secondary
    // camera's sphere ~0 here; the probe union keeps both lit.
    REQUIRE(dominant0 > 2000.0);
    REQUIRE(dominant1 > 2000.0);

    // Each camera sees a single strongly-colored sphere (its dominant channel far
    // exceeds the other), and the two cameras see DIFFERENT colors (one red-led,
    // one blue-led) — confirming each gathered its OWN viewpoint's geometry, not
    // a shared/wrong probe set.
    const bool cam0Red = r0 > b0;
    const bool cam1Red = r1 > b1;
    REQUIRE(cam0Red != cam1Red);  // opposite dominant colors
}
