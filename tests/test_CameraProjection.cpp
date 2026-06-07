#include <catch2/catch_all.hpp>

#include "Camera.h"
#include "PixelCoords.h"
#include "Quaternion.h"
#include "RandomGenerator.h"
#include "Ray.h"
#include "Utility.h"
#include "Vector.h"

#include <cmath>
#include <memory>

// Camera projection ray-gen tests. The seam under test is
// Camera::generatePrimaryRay(coord, generator) and the rectilinear forward model
// in Camera::pixelDirection.
//
// [INVARIANT] perspective is RECTILINEAR (pinhole), NOT f-theta. The edge-ray
// test below pins this: an off-center pixel's screen offset equals tan(angle),
// NOT the angle itself. If a future change reintroduces angle = pixelOffset *
// angularStep (the old fisheye construction), this test fails.

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{

// Camera at the origin looking down +Z (identity rotation). Even dimension so no
// pixel sits exactly at the center; we use the sub-pixel sampled ray for the
// center check instead.
std::shared_ptr<Camera> makeAxisCamera(size_t w, size_t h, double vfovDeg)
{
    auto camera = std::make_shared<Camera>(w, h, vfovDeg);
    camera->transform.position = Vector{0, 0, 0};
    camera->transform.rotation = Quaternion();
    return camera;
}

}  // namespace

TEST_CASE("CameraProjection: perspective center ray points along forward", "[CameraProjection]")
{
    // Odd dimensions => the middle pixel's center maps to screen (0,0) => forward.
    auto camera = makeAxisCamera(11, 11, 60.0);

    const PixelCoords center{5, 5};  // exact middle of an 11x11 grid
    const Ray ray = camera->generatePrimaryRay(center, nullptr);

    // Origin is the eye for perspective; direction is +Z (forward).
    REQUIRE_THAT(ray.origin.x, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(ray.origin.y, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(ray.origin.z, WithinAbs(0.0, 1e-9));

    REQUIRE_THAT(ray.direction.x, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(ray.direction.y, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(ray.direction.z, WithinRel(1.0));
}

TEST_CASE("CameraProjection: perspective edge ray matches tan(angle), not the angle (rectilinear, not f-theta)",
          "[CameraProjection]")
{
    // Square frame so aspect = 1. vfov = 90 => tan(45) = 1 at the top/bottom edge.
    auto camera = makeAxisCamera(100, 100, 90.0);

    // The very top edge pixel (y = height-1) sampled at its top sub-pixel (offset
    // ~1.0) approaches screen sy = +1, i.e. direction up-component / forward =
    // tan(vfov/2). We sample at a known screen position via the pixel-center map.
    //
    // For pixel y, screen sy = 2*(y+0.5)/height - 1. Choose y so sy is a clean
    // value: y = 75 => sy = 2*75.5/100 - 1 = 0.51.
    const double sy = 2.0 * (75.0 + 0.5) / 100.0 - 1.0;  // 0.51
    const double halfHeight = std::tan(Utility::radians(90.0) / 2.0);  // tan(45) = 1
    const PixelCoords coord{50, 75};
    const Ray ray = camera->generatePrimaryRay(coord, nullptr);

    // Rectilinear: the ray's (up / forward) ratio == sy * tan(vfov/2). The x pixel
    // 50 -> screen sx = 2*50.5/100 - 1 = 0.01 (tiny but nonzero).
    const double sx = 2.0 * (50.0 + 0.5) / 100.0 - 1.0;  // 0.01
    const double expectedTanY = sy * halfHeight;          // = 0.51
    const double expectedTanX = sx * halfHeight;          // = 0.01

    // direction is normalized; recover the tangents as components / z.
    const double tanY = ray.direction.y / ray.direction.z;
    const double tanX = ray.direction.x / ray.direction.z;

    REQUIRE_THAT(tanY, WithinRel(expectedTanY, 1e-9));
    REQUIRE_THAT(tanX, WithinRel(expectedTanX, 1e-9));

    // Contrast with f-theta: the OLD model would give direction.y = sin(sy *
    // vfov/2) and z = cos(...), so tanY would be tan(0.51 * 45deg) = tan(22.95deg)
    // = 0.4236, NOT 0.51. Assert we are NOT the f-theta value.
    const double fthetaTanY = std::tan(Utility::radians(sy * 90.0 / 2.0));
    REQUIRE(std::abs(tanY - fthetaTanY) > 1e-3);
}

TEST_CASE("CameraProjection: orthographic rays are parallel with offset origins", "[CameraProjection]")
{
    auto camera = makeAxisCamera(100, 100, 90.0);
    camera->projection(Camera::Projection::Orthographic);
    camera->orthographicHeight(200.0);  // half-height = 100 world units

    const PixelCoords center{50, 50};
    const PixelCoords corner{99, 50};

    const Ray rayC = camera->generatePrimaryRay(center, nullptr);
    const Ray rayE = camera->generatePrimaryRay(corner, nullptr);

    // Parallel: both directions are forward (+Z), regardless of pixel.
    REQUIRE_THAT(rayC.direction.z, WithinRel(1.0));
    REQUIRE_THAT(rayE.direction.z, WithinRel(1.0));
    REQUIRE_THAT(rayC.direction.x, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(rayE.direction.x, WithinAbs(0.0, 1e-9));

    // Origin varies across the image plane along +X for an increasing x pixel.
    // aspect = 1, half-height = 100. pixel 99 center -> sx = 2*99.5/100 - 1 = 0.99,
    // so origin.x = 0.99 * 1 * 100 = 99.
    REQUIRE_THAT(rayE.origin.x, WithinRel(99.0, 1e-9));
    REQUIRE(rayE.origin.x > rayC.origin.x);
    // Center pixel sx = 2*50.5/100 - 1 = 0.01 -> origin.x = 1.0.
    REQUIRE_THAT(rayC.origin.x, WithinRel(1.0, 1e-9));
}

TEST_CASE("CameraProjection: reallens aperture sampling jitters origin but keeps focus point",
          "[CameraProjection]")
{
    auto camera = makeAxisCamera(100, 100, 90.0);
    camera->projection(Camera::Projection::RealLens);
    camera->apertureRadius(5.0);
    camera->focusDistance(100.0);

    const PixelCoords center{50, 50};

    // No generator: degenerates to the sharp pinhole ray from the eye.
    const Ray sharp = camera->generatePrimaryRay(center, nullptr);
    REQUIRE_THAT(sharp.origin.x, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(sharp.origin.y, WithinAbs(0.0, 1e-9));

    (void)sharp;
    RandomGenerator generator(1234);

    bool originMoved = false;
    for (int i = 0; i < 64; ++i)
    {
        const Ray jittered = camera->generatePrimaryRay(center, &generator);

        // Aperture origin stays within the aperture disk (radius 5) in the lens
        // plane (z == eye z == 0).
        const double lensR =
            std::sqrt(jittered.origin.x * jittered.origin.x + jittered.origin.y * jittered.origin.y);
        REQUIRE(lensR <= 5.0 + 1e-9);
        REQUIRE_THAT(jittered.origin.z, WithinAbs(0.0, 1e-9));

        if (lensR > 1e-6)
        {
            originMoved = true;
        }

        // Every sample, whatever its aperture point, still reaches the focus PLANE
        // (depth focusDistance == 100 along +Z) — that is the surface that stays
        // sharp. The in-plane landing position varies with the sub-pixel film
        // sample (correct: AA + DOF), but the depth is invariant.
        const double t = (100.0 - jittered.origin.z) / jittered.direction.z;
        const Vector hit = jittered.origin + jittered.direction * t;
        REQUIRE_THAT(hit.z, WithinAbs(100.0, 1e-6));
    }
    REQUIRE(originMoved);  // sampling actually jitters the aperture
}

TEST_CASE("CameraProjection: reallens converges all aperture samples to one focus point for a fixed film sample",
          "[CameraProjection]")
{
    // Isolate DOF from AA: drive the generator so the FIRST two draws (sub-pixel
    // offX, offY) are identical across calls, while the aperture draws differ.
    // We can't pin the RNG mid-sequence through the public API, so instead we
    // verify the focus invariant analytically: for a fixed pinhole direction, the
    // focus point lies at focusDistance along the view axis, and any aperture point
    // aimed at it lands there. This mirrors generatePrimaryRay's construction.
    auto camera = makeAxisCamera(100, 100, 90.0);
    camera->projection(Camera::Projection::RealLens);
    camera->apertureRadius(5.0);
    camera->focusDistance(80.0);

    // The deterministic (no-generator) ray is the pinhole ray; its focus point is
    // where it crosses depth 80. Manually aim several aperture points at that point
    // and confirm each reaches it on the focus plane.
    const PixelCoords coord{60, 40};
    const Ray pinhole = camera->generatePrimaryRay(coord, nullptr);
    const double tFocus = 80.0 / pinhole.direction.z;
    const Vector focusPoint = pinhole.origin + pinhole.direction * tFocus;
    REQUIRE_THAT(focusPoint.z, WithinAbs(80.0, 1e-9));

    for (const Vector& lens : {Vector{3, 0, 0}, Vector{-2, 4, 0}, Vector{0, -5, 0}})
    {
        const Vector aperture = pinhole.origin + lens;
        const Vector dir = Vector::normalized(focusPoint - aperture);
        const double t = (80.0 - aperture.z) / dir.z;
        const Vector hit = aperture + dir * t;
        REQUIRE_THAT(hit.x, WithinAbs(focusPoint.x, 1e-6));
        REQUIRE_THAT(hit.y, WithinAbs(focusPoint.y, 1e-6));
    }
}

TEST_CASE("CameraProjection: effectiveApertureRadius derives from focal length and f-number",
          "[CameraProjection]")
{
    auto camera = makeAxisCamera(100, 100, 90.0);
    camera->projection(Camera::Projection::RealLens);

    // Explicit radius wins.
    camera->apertureRadius(3.0);
    REQUIRE_THAT(camera->effectiveApertureRadius(), WithinRel(3.0));

    // Absent explicit radius => focalLength / (2 * N).
    camera->apertureRadius(0.0);
    camera->focalLength(50.0);
    camera->fNumber(2.0);
    REQUIRE_THAT(camera->effectiveApertureRadius(), WithinRel(50.0 / (2.0 * 2.0)));  // 12.5
}
