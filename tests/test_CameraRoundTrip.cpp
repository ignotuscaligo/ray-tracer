#include <catch2/catch_all.hpp>

#include "Camera.h"
#include "PixelCoords.h"
#include "Quaternion.h"
#include "Ray.h"
#include "Utility.h"
#include "Vector.h"

#include <cstdlib>
#include <memory>
#include <vector>

// ============================================================================
// T3 CameraRoundTrip — forward ray-gen and reverse projection are exact inverses
// ============================================================================
//
// DESIGN §6e [INVARIANT]: "The reverse projection is the exact inverse of the
// forward ray-gen." coordForPoint / coordForPointSubPixel (used by the forward
// photon SPLAT) project a world point with the SAME rectilinear math
// generatePrimaryRay uses, and "the two projections are a matched pair and must
// change together." A divergence would silently shear a photon's splat off the
// gather pixel for that pixel — the matched-pair invariant the test-plan review
// flagged as untested (#23).
//
// Oracle (pure algebra, no rendering): take the pixel-CENTER primary ray for pixel
// (x, y); a world point ANYWHERE along that ray (any positive depth) must project
// back to sub-pixel coordinate (x + 0.5, y + 0.5) — the pixel center — within tight
// numerical tolerance. The forward mapping puts pixel k at normalized fraction
// (k + 0.5)/dim, and coordForPointSubPixel inverts to fraction * dim, so the center
// ray's point round-trips to k + 0.5. Off-frustum points return nullopt.

using Catch::Approx;

namespace
{
std::shared_ptr<Camera> makeCamera(size_t w, size_t h, double vfovDeg,
                                   const Vector& pos, const Quaternion& rot)
{
    auto cam = std::make_shared<Camera>(w, h, vfovDeg);
    cam->transform.position = pos;
    cam->transform.rotation = rot;
    return cam;
}
}  // namespace

TEST_CASE("T3 CameraRoundTrip: center ray point projects back to pixel center",
          "[CameraRoundTrip][T3]")
{
    auto cam = makeCamera(64, 48, 55.0, Vector{0, 0, 0}, Quaternion());

    // Sweep a representative set of pixels (corners, edges, interior) at several
    // depths along each pixel's center ray.
    const std::vector<PixelCoords> coords = {
        {0, 0}, {63, 0}, {0, 47}, {63, 47}, {32, 24}, {10, 40}, {50, 5}, {1, 1}};

    for (const PixelCoords& c : coords)
    {
        const Ray center = cam->generatePrimaryRay(c, nullptr);
        for (const double depth : {1.0, 10.0, 137.5, 1000.0})
        {
            const Vector world = center.origin + center.direction * depth;
            const auto rt = cam->coordForPointSubPixel(world);
            REQUIRE(rt.has_value());
            INFO("pixel (" << c.x << "," << c.y << ") depth " << depth
                 << " -> (" << rt->x << "," << rt->y << ")");
            REQUIRE(rt->x == Approx(static_cast<double>(c.x) + 0.5).margin(1e-9));
            REQUIRE(rt->y == Approx(static_cast<double>(c.y) + 0.5).margin(1e-9));
        }
    }
}

TEST_CASE("T3 CameraRoundTrip: inverse holds for a rotated, translated camera",
          "[CameraRoundTrip][T3]")
{
    // The matched pair must hold in an arbitrary pose, not just at the origin facing
    // +z (the splat projects world points from real scene camera poses).
    const Quaternion rot = Quaternion::fromPitchYawRoll(
        Utility::radians(17.0), Utility::radians(-33.0), Utility::radians(8.0));
    auto cam = makeCamera(40, 40, 70.0, Vector{12.0, -5.0, -80.0}, rot);

    for (const PixelCoords& c : {PixelCoords{0, 0}, PixelCoords{39, 39},
                                 PixelCoords{20, 20}, PixelCoords{5, 30}})
    {
        const Ray center = cam->generatePrimaryRay(c, nullptr);
        const Vector world = center.origin + center.direction * 250.0;
        const auto rt = cam->coordForPointSubPixel(world);
        REQUIRE(rt.has_value());
        INFO("pixel (" << c.x << "," << c.y << ") -> (" << rt->x << "," << rt->y << ")");
        REQUIRE(rt->x == Approx(static_cast<double>(c.x) + 0.5).margin(1e-7));
        REQUIRE(rt->y == Approx(static_cast<double>(c.y) + 0.5).margin(1e-7));
    }
}

TEST_CASE("T3 CameraRoundTrip: integer coordForPoint recovers the source pixel within rounding",
          "[CameraRoundTrip][T3]")
{
    // coordForPoint rounds the continuous coordinate to the nearest integer pixel.
    // The matched-pair INVARIANT is carried by coordForPointSubPixel (asserted
    // exactly above); coordForPoint adds a round() whose half-integer behavior at a
    // pixel CENTER (fraction k+0.5) is a +/-1 boundary choice, NOT a projection
    // error. So here we require the recovered pixel is within 1 of the source — the
    // splat lands in the source pixel's immediate neighborhood, which is all the
    // integer round-trip guarantees.
    auto cam = makeCamera(64, 48, 55.0, Vector{0, 0, 0}, Quaternion());
    for (const PixelCoords& c : {PixelCoords{1, 1}, PixelCoords{62, 46},
                                 PixelCoords{32, 24}, PixelCoords{17, 9}})
    {
        const Ray center = cam->generatePrimaryRay(c, nullptr);
        const Vector world = center.origin + center.direction * 42.0;
        const auto px = cam->coordForPoint(world);
        REQUIRE(px.has_value());
        const long dx = static_cast<long>(px->x) - static_cast<long>(c.x);
        const long dy = static_cast<long>(px->y) - static_cast<long>(c.y);
        INFO("source (" << c.x << "," << c.y << ") -> (" << px->x << "," << px->y << ")");
        REQUIRE(std::abs(dx) <= 1);
        REQUIRE(std::abs(dy) <= 1);
    }
}

TEST_CASE("T3 CameraRoundTrip: off-frustum and behind-camera points return nullopt",
          "[CameraRoundTrip][T3]")
{
    auto cam = makeCamera(64, 48, 55.0, Vector{0, 0, 0}, Quaternion());

    // A point directly behind the camera (negative depth along forward).
    const Ray center = cam->generatePrimaryRay(PixelCoords{32, 24}, nullptr);
    const Vector behind = center.origin - center.direction * 50.0;
    REQUIRE_FALSE(cam->coordForPointSubPixel(behind).has_value());

    // A point far outside the lateral frustum at a modest depth (way off to the side
    // relative to the field of view): project past the left/right screen edge.
    const Vector farSide{100000.0, 0.0, 10.0};
    REQUIRE_FALSE(cam->coordForPointSubPixel(farSide).has_value());
}
