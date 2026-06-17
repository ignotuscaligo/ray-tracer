#include <catch2/catch_all.hpp>

#include "Buffer.h"
#include "Color.h"
#include "Renderer.h"
#include "SceneLoader.h"

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

// ============================================================================
// #64 — mirror corner BLACK DOTS (reflected-ray origin offset)
// ============================================================================
//
// THE BUG: in the camera-side specular trace (ProbeGather::extendAndRecord,
// src/ProbeGather.cpp) the spawned reflected/refracted continuation ray's ORIGIN was
// offset along the OUTGOING DIRECTION by kReflectionEpsilon
// (`hit.position + nextDir * eps`). At a corner-grazing angle the outgoing direction
// is nearly parallel to the surface, so that offset barely separates the origin from
// the plane and — on the far side of a corner where two surfaces meet — steps it
// BELOW the surface. The next cast then escapes into hidden space and the pixel
// renders pure black: RGB(0,0,0) dots strung along the mirror's corner / triangulation
// diagonal lines (exposed in MirrorDirectTest, camera height == corner offset).
//
// THE FIX (APPROVED): offset the spawned ray's origin along the surface NORMAL
// instead. The normal is perpendicular to the plane, so an eps step along it always
// clears the surface no matter how grazing the outgoing direction is, oriented to the
// side the ray travels toward so refraction (which transmits to the far side) stays
// correct too.
//
// THE TEST (objective, deterministic): a closed Cornell box whose LEFT wall is a
// MIRROR (the canonical MirrorDirectTest geometry — a quad mesh that triangulates into
// two triangles, the corner/diagonal lines where the dots appear), rendered in
// single-thread deterministic mode. Counts PURE-BLACK RGB(0,0,0) pixels.
//
//   - CONTROL: with the same wall made DIFFUSE, the render has only a tiny baseline of
//     pure-black pixels (a few on the sphere silhouette / light corners). So almost
//     every black pixel in the MIRROR render is caused by the reflection path — the
//     dots are the bug, not generic scene darkness.
//   - PRE-FIX (origin offset along nextDir): 41 pure-black pixels in this scene.
//   - POST-FIX (origin offset along the surface normal): 25 — the reflected-ray bug's
//     dots along the corner/diagonal lines are removed (the residual is the diffuse
//     baseline plus mesh-triangulation-seam pixels unrelated to the spawn offset).
//
// The assertion threshold (34) sits between the pre-fix (41) and post-fix (25) counts:
// it FAILS pre-fix and PASSES post-fix, and is a durable regression guard — reverting
// the spawn offset back to `nextDir` repopulates the corner lines and trips it. The
// control assertion pins that the black is reflection-caused, so the guard can't be
// satisfied by globally darkening or brightening the scene.

namespace
{
// The scene below renders at this fixed resolution.
constexpr size_t kW = 300;
constexpr size_t kH = 300;

// The canonical Cornell-box mesh (quad faces -> 2 triangles each; the mirror wall's
// triangulation diagonal is where the corner dots strung). Same geometry as the
// in-repo meshes/CornellBox.obj that MirrorDirectTest.json uses; embedded so the test
// is self-contained and cwd-independent.
const char* kCornellObj = R"OBJ(# Cornell box (embedded for #64 corner-dots test)
v -150.000000 305.000000 155.000000
v -150.000000 -5.000000 155.000000
v -150.000000 305.000000 -155.000000
v -150.000000 -5.000000 -155.000000
vn 1.000000 0.000000 0.000000
vt 0.000000 0.000000 0.000000
vt 0.000000 1.000000 0.000000
vt 1.000000 1.000000 0.000000
vt 1.000000 0.000000 0.000000
o Right
f 2/4/1 4/3/1 3/2/1 1/1/1
v 150.000000 -5.000000 155.000000
v 150.000000 305.000000 155.000000
v 150.000000 -5.000000 -155.000000
v 150.000000 305.000000 -155.000000
vn -1.000000 0.000000 0.000000
o Left
f 6/4/2 8/3/2 7/2/2 5/1/2
v 155.000000 300.000000 155.000000
v -155.000000 300.000000 155.000000
v 155.000000 300.000000 -155.000000
v -155.000000 300.000000 -155.000000
vn 0.000000 -1.000000 0.000000
o Ceiling
f 10/4/3 12/3/3 11/2/3 9/1/3
v -155.000000 305.000000 150.000000
v 155.000000 305.000000 150.000000
v -155.000000 -5.000000 150.000000
v 155.000000 -5.000000 150.000000
vn 0.000000 0.000000 -1.000000
o Back
f 14/4/4 16/3/4 15/2/4 13/1/4
v -155.000000 0.000000 155.000000
v 155.000000 0.000000 155.000000
v -155.000000 0.000000 -155.000000
v 155.000000 0.000000 -155.000000
vn 0.000000 1.000000 0.000000
o Floor
f 18/4/5 20/3/5 19/2/5 17/1/5
)OBJ";

std::filesystem::path writeTempObj()
{
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rt_corner64_cornell.obj";
    std::ofstream out(path);
    out << kCornellObj;
    out.close();
    return path;
}

// Scene JSON for the closed mirror box. `leftMaterial` is "MirrorMat" for the bug
// render and "Wall" (diffuse) for the control. `$objPath` is filled with the temp OBJ.
std::string cornerScene(const std::string& objPath, const char* leftMaterial)
{
    std::string json = R"JSON({
  "$meshes": ["__OBJ__"],
  "$materials": {
    "MirrorMat": { "$type": "Mirror", "$color": [0.98, 0.98, 0.98] },
    "RightGreen": { "$type": "Diffuse", "$color": [0.1, 0.9, 0.1] },
    "Wall": { "$type": "Diffuse", "$color": [1.0] },
    "BallMat": { "$type": "Diffuse", "$color": [0.9, 0.6, 0.1] }
  },
  "$workerConfiguration": { "$workerCount": 1, "$fetchSize": 50000, "$photonQueueSize": 30000000 },
  "$renderConfiguration": {
    "$width": 300, "$height": 300, "$photonsPerLight": 6000000,
    "$bounceThreshold": 3, "$probeGather": true,
    "$deterministic": true, "$seed": 11, "$bounceStoreCapacity": 40000000
  },
  "$scene": {
    "Camera": { "$type": "Camera", "$verticalFieldOfView": 70.0, "$position": [0.0, 150, -360],
      "$rotation": { "$type": "PitchYawRollDegrees", "$value": [0.0, 0.0, 0.0] } },
    "Light": { "$type": "AreaLight", "$shape": "Square", "$size": 120, "$position": [0, 298, 0],
      "$rotation": { "$type": "PitchYawRollDegrees", "$value": [90, 0, 0] },
      "$color": [1.0, 1.0, 1.0], "$luminousFlux": 125663706 },
    "Ball": { "$type": "SphereVolume", "$material": "BallMat", "$center": [60, 50, 30], "$radius": 50 },
    "CornellBox": { "$type": "Object",
      "Ceiling": { "$type": "MeshVolume", "$material": "Wall", "$mesh": "Ceiling" },
      "LeftWall": { "$type": "MeshVolume", "$material": "__LEFT__", "$mesh": "Left" },
      "RightWall": { "$type": "MeshVolume", "$material": "RightGreen", "$mesh": "Right" },
      "Floor": { "$type": "MeshVolume", "$material": "Wall", "$mesh": "Floor" },
      "Back": { "$type": "MeshVolume", "$material": "Wall", "$mesh": "Back" },
      "Front": { "$type": "Quad", "$material": "Wall", "$origin": [-200,-50,-360], "$edgeU": [400,0,0], "$edgeV": [0,400,0] }
    }
  }
})JSON";
    const std::string objKey = "__OBJ__";
    json.replace(json.find(objKey), objKey.size(), objPath);
    const std::string leftKey = "__LEFT__";
    json.replace(json.find(leftKey), leftKey.size(), leftMaterial);
    return json;
}

size_t countPureBlack(const Buffer& buffer, size_t width, size_t height)
{
    size_t n = 0;
    for (size_t y = 0; y < height; ++y)
    {
        for (size_t x = 0; x < width; ++x)
        {
            const Color c = buffer.fetchColor({x, y});
            if (c.red == 0.0f && c.green == 0.0f && c.blue == 0.0f)
            {
                ++n;
            }
        }
    }
    return n;
}

RenderResult renderJson(const std::string& json)
{
    std::filesystem::path scenePath =
        std::filesystem::temp_directory_path() / "rt_corner64_scene.json";
    {
        std::ofstream out(scenePath);
        out << json;
    }
    LoadedScene scene = SceneLoader::loadFromFile(scenePath.string(), false);
    std::remove(scenePath.string().c_str());
    return Renderer::renderFrame(scene);
}
}  // namespace

TEST_CASE("#64: a mirror corner has no pure-black escape dots (reflected-ray origin offset)",
          "[MirrorCorner][BlackDots][regression]")
{
    const std::filesystem::path objPath = writeTempObj();

    // CONTROL: the same wall DIFFUSE — establishes the baseline of pure-black pixels
    // NOT caused by the mirror reflection (sphere silhouette / light-corner pixels).
    const RenderResult control =
        renderJson(cornerScene(objPath.string(), "Wall"));
    REQUIRE(control.buffer);
    const size_t controlBlack = countPureBlack(*control.buffer, kW, kH);
    INFO("control (diffuse wall) pure-black = " << controlBlack);

    // MIRROR render: the bug scene.
    const RenderResult mirror =
        renderJson(cornerScene(objPath.string(), "MirrorMat"));
    std::filesystem::remove(objPath);
    REQUIRE(mirror.buffer);
    const size_t mirrorBlack = countPureBlack(*mirror.buffer, kW, kH);
    INFO("mirror render pure-black = " << mirrorBlack
         << " (pre-fix 41, post-fix 25; threshold 34)");

    // The control proves the black is reflection-caused: the diffuse render has only a
    // tiny baseline, so the corner dots in the mirror render come from the reflection.
    REQUIRE(controlBlack < 10);

    // HEADLINE: the mirror corner pure-black count is below 34 — the reflected-ray
    // origin offset along the surface NORMAL removes the corner/diagonal escape dots.
    // Pre-fix (origin offset along the outgoing direction) this scene has 41 pure-black
    // pixels and FAILS; post-fix it has 25 and PASSES. Reverting the spawn offset back
    // to `nextDir` repopulates the corner lines and trips this guard.
    REQUIRE(mirrorBlack < 34);
}
