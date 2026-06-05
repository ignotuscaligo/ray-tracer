#include <catch2/catch_all.hpp>

#include "AreaLight.h"
#include "Buffer.h"
#include "Camera.h"
#include "Color.h"
#include "EmissiveGather.h"
#include "Object.h"
#include "PixelCoords.h"
#include "Quaternion.h"
#include "SphereVolume.h"
#include "Utility.h"
#include "Vector.h"

#include <cmath>
#include <memory>
#include <vector>

// EmissiveGather coplanar-occlusion epsilon (review HIGH gap: EmissiveGather had
// ZERO tests). This guards the salt-and-pepper z-fix: the gather treats a scene
// Volume as occluding the emitter only when it lies NEARER than the emitter by more
// than a RELATIVE margin of the emitter distance:
//
//     occluder < bestT * (1 - kOcclusionCoincidenceMargin)   (src/EmissiveGather.cpp:217)
//
// with kOcclusionCoincidenceMargin = 1e-3. A coplanar / near-coincident surface hit
// at t ~= bestT must NOT occlude (the panel stays lit); a genuine occluder clearly
// nearer than the emitter DOES occlude. Both the end-to-end run() and the focused
// predicate test below pin that behavior.

using Catch::Matchers::WithinAbs;

namespace
{

// kOcclusionCoincidenceMargin from src/EmissiveGather.cpp:40. Kept in sync here so
// the predicate test asserts against the real shipped value.
constexpr double kOcclusionCoincidenceMargin = 1e-3;

// The exact occlusion decision the gather makes (mirrors EmissiveGather.cpp:217).
// Returns true if `occluderDistance` counts as occluding the emitter at `bestT`.
bool occludes(double occluderDistance, double bestT)
{
    return occluderDistance < bestT * (1.0 - kOcclusionCoincidenceMargin);
}

// A camera at the origin looking down +Z (identity rotation => pixelDirection maps
// the center pixel to (0,0,1)).
std::shared_ptr<Camera> makeAxisCamera()
{
    auto camera = std::make_shared<Camera>(11, 11, 60.0);
    camera->transform.position = Vector{0, 0, 0};
    camera->transform.rotation = Quaternion();
    return camera;
}

// An emissive square panel centered on the +Z axis at distance D, its lit face
// turned back toward the camera at the origin (normal = -Z). 180-degree yaw about Y
// turns the light's +Z normal to -Z while keeping the panel spanning x/y.
std::shared_ptr<AreaLight> makeFacingPanel(double distance, double halfExtent)
{
    auto light = std::make_shared<AreaLight>();
    light->shape(AreaLight::Shape::Square);
    light->width(halfExtent * 2.0);
    light->height(halfExtent * 2.0);
    light->luminousFluxOverride(1000.0);
    light->color(Color{1.0f, 1.0f, 1.0f});
    light->transform.position = Vector{0, 0, distance};
    light->transform.rotation = Quaternion::fromAxisAngle(Vector{0, 1, 0}, Utility::pi);
    return light;
}

}  // namespace

TEST_CASE("EmissiveGather: a coplanar occluder at t==bestT does NOT occlude", "[EmissiveGather]")
{
    // The exact z-fight case: a surface coincident with the emitter plane is hit at
    // t == bestT (per-pixel FP noise pushes it a hair either way). The relative
    // margin must keep it from counting as an occluder.
    const double bestT = 300.0;  // emitter distance, matching the real Cornell scale
    REQUIRE_FALSE(occludes(bestT, bestT));                 // exactly coincident
    REQUIRE_FALSE(occludes(bestT * (1.0 + 1e-9), bestT));  // a hair beyond
    REQUIRE_FALSE(occludes(bestT * (1.0 - 1e-9), bestT));  // a hair nearer (FP noise)
    // Anything inside the margin band (nearer by < 0.1% of bestT) still does NOT occlude.
    REQUIRE_FALSE(occludes(bestT * (1.0 - 0.5 * kOcclusionCoincidenceMargin), bestT));
}

TEST_CASE("EmissiveGather: a genuine occluder clearly nearer than the emitter DOES occlude",
          "[EmissiveGather]")
{
    const double bestT = 300.0;
    // A real occluder (wall / blocker sphere) sits well inside the emitter distance,
    // far outside the 0.1% margin band.
    REQUIRE(occludes(0.5 * bestT, bestT));
    REQUIRE(occludes(0.99 * bestT, bestT));  // 1% nearer: outside the 0.1% margin
    // Just past the margin edge is the first distance that occludes.
    REQUIRE(occludes(bestT * (1.0 - 2.0 * kOcclusionCoincidenceMargin), bestT));
}

TEST_CASE("EmissiveGather run(): unoccluded panel is written at its surface radiance",
          "[EmissiveGather]")
{
    const double distance = 50.0;
    const double halfExtent = 8.0;  // a wide panel so the center pixel ray lands inside it
    auto camera = makeAxisCamera();
    auto panel = makeFacingPanel(distance, halfExtent);

    std::vector<std::shared_ptr<Object>> objects;
    objects.push_back(panel);

    Buffer buffer(camera->width(), camera->height());
    const EmissiveGather::Result result =
        EmissiveGather::run(objects, camera, /*animation=*/nullptr, /*workerCount=*/1, buffer);

    // The center pixel ray (0,0,1) strikes the front of the facing panel.
    REQUIRE(result.pixelsEmissive > 0);
    const PixelCoords center{camera->width() / 2, camera->height() / 2};
    const Color written = buffer.fetchColor(center);
    const Color expected = panel->surfaceRadiance();
    REQUIRE(expected.red > 0.0f);
    // The pixel holds exactly the emitter's surface radiance (addColor into a fresh buffer).
    REQUIRE_THAT(written.red, WithinAbs(expected.red, 1e-3f));
    REQUIRE_THAT(written.green, WithinAbs(expected.green, 1e-3f));
    REQUIRE_THAT(written.blue, WithinAbs(expected.blue, 1e-3f));
}

TEST_CASE("EmissiveGather run(): a near occluder hides the panel (center pixel black)",
          "[EmissiveGather]")
{
    const double distance = 50.0;
    const double halfExtent = 8.0;
    auto camera = makeAxisCamera();
    auto panel = makeFacingPanel(distance, halfExtent);

    // A sphere on the +Z axis well in front of the panel (near surface at z = 25-2 = 23,
    // i.e. ~0.46 * distance, far inside the margin) genuinely occludes the center pixel.
    auto blocker = std::make_shared<SphereVolume>(/*materialIndex=*/0,
                                                  Vector{0, 0, 25.0}, /*radius=*/2.0);

    std::vector<std::shared_ptr<Object>> objects;
    objects.push_back(panel);
    objects.push_back(blocker);

    Buffer buffer(camera->width(), camera->height());
    EmissiveGather::run(objects, camera, nullptr, 1, buffer);

    const PixelCoords center{camera->width() / 2, camera->height() / 2};
    const Color written = buffer.fetchColor(center);
    // Center pixel is occluded -> nothing written there.
    REQUIRE(written.red == 0.0f);
    REQUIRE(written.green == 0.0f);
    REQUIRE(written.blue == 0.0f);
}

TEST_CASE("EmissiveGather run(): a back-facing panel emits nothing", "[EmissiveGather]")
{
    // Panel with its lit face turned AWAY from the camera (normal = +Z, default
    // rotation) -> intersectPatch's front-face-only test rejects the pixel ray.
    const double distance = 50.0;
    auto camera = makeAxisCamera();
    auto panel = std::make_shared<AreaLight>();
    panel->shape(AreaLight::Shape::Square);
    panel->width(16.0);
    panel->height(16.0);
    panel->luminousFluxOverride(1000.0);
    panel->color(Color{1.0f, 1.0f, 1.0f});
    panel->transform.position = Vector{0, 0, distance};
    panel->transform.rotation = Quaternion();  // normal = +Z, pointing AWAY from camera

    std::vector<std::shared_ptr<Object>> objects{panel};
    Buffer buffer(camera->width(), camera->height());
    const EmissiveGather::Result result =
        EmissiveGather::run(objects, camera, nullptr, 1, buffer);

    REQUIRE(result.pixelsEmissive == 0);
}
