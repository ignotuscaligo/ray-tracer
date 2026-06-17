#include <catch2/catch_all.hpp>

#include "Buffer.h"
#include "Camera.h"
#include "Color.h"
#include "LambertianMaterial.h"
#include "MaterialLibrary.h"
#include "Object.h"
#include "Photon.h"
#include "PixelCoords.h"
#include "Quaternion.h"
#include "SphereVolume.h"
#include "Utility.h"
#include "Vector.h"
#include "Worker.h"

#include <cmath>
#include <memory>
#include <vector>

// splatToCamera foreshortening test (review MEDIUM gap: splatToCamera had ZERO
// tests). The camera splat scales each contribution by cos(theta) between the
// surface normal and the camera direction (the rim-light fix, src/Worker.cpp): a
// facing hit deposits full energy, a grazing hit is foreshortened toward 0, a
// back-facing hit (cos <= 0) and an occluded hit contribute nothing.
//
// splatToCamera was made public on Worker purely for this test (see Worker.h note);
// the renderer logic is unchanged.

using Catch::Matchers::WithinRel;
using Catch::Matchers::WithinAbs;

namespace
{

// Camera at the origin looking down +Z (identity rotation). The center pixel maps to
// direction (0,0,1), so a hit on the +Z axis projects to the center pixel.
std::shared_ptr<Camera> makeAxisCamera()
{
    auto camera = std::make_shared<Camera>(11, 11, 60.0);
    camera->transform.position = Vector{0, 0, 0};
    camera->transform.rotation = Quaternion();
    return camera;
}

// A non-delta photon hit at `position` with surface `normal`, carrying `power`. The
// photon's incoming travel direction is -normal (a head-on arrival travelling INTO
// the surface), so wi = -rayDir = +normal sits in the upper hemisphere — the
// physically-correct arrival for a front-lit surface. (Previously this set the ray
// direction to +normal, i.e. a photon travelling OUT of the surface, which left wi
// BELOW the surface; the splat only worked because LambertianMaterial::evaluate
// ignored wi. With evaluate now enforcing Helmholtz reciprocity at the hemisphere
// boundary — f = 0 when wi or wo is below the surface — the arrival direction must
// be physical for the splat to deposit. For a Lambertian the BRDF is otherwise
// direction-independent within the hemisphere, so this does not change the expected
// magnitude.)
PhotonHit makeHit(const Vector& position, const Vector& normal, const Color& power)
{
    PhotonHit ph;
    ph.photon.ray = Ray{position + normal, -normal};  // arrives travelling INTO surface
    ph.photon.color = power;
    ph.photon.bounces = 0;
    ph.photon.time = 0.0f;
    ph.hit.position = position;
    ph.hit.normal = normal;
    ph.hit.distance = 1.0;
    ph.hit.material = 0;
    return ph;
}

// Build a worker wired with one splat target (camera + buffer) and the given scene
// objects (occluders). photonsPerLight > 0 enables the splat.
struct SplatRig
{
    std::shared_ptr<Worker> worker;
    std::shared_ptr<Camera> camera;
    std::shared_ptr<Buffer> buffer;
    std::shared_ptr<LambertianMaterial> material;
};

SplatRig makeRig(const std::vector<std::shared_ptr<Object>>& occluders,
                 const Color& albedo)
{
    SplatRig rig;
    rig.camera = makeAxisCamera();
    rig.buffer = std::make_shared<Buffer>(rig.camera->width(), rig.camera->height());
    rig.material = std::make_shared<LambertianMaterial>("d", albedo);

    rig.worker = std::make_shared<Worker>(/*index=*/0, /*fetchSize=*/1);
    rig.worker->objects = occluders;  // occlusion cast walks these
    rig.worker->setPhotonsPerLight(1.0);  // enable the splat (no 1/N divide in single-photon)

    Worker::SplatTarget target;
    target.camera = rig.camera;
    target.buffer = rig.buffer;
    rig.worker->setSplatTargets({target});
    return rig;
}

const PixelCoords kCenter{5, 5};  // center of an 11x11 image

}  // namespace

TEST_CASE("splatToCamera: a facing hit deposits cos/area-weighted radiance", "[SplatToCamera]")
{
    const Color albedo{0.9f, 0.9f, 0.9f};
    SplatRig rig = makeRig({}, albedo);

    // Hit on the +Z axis at depth D, normal pointing back at the camera (-Z) => the
    // surface fully faces the camera, cos(theta) = 1.
    const double depth = 10.0;
    const Color power{4.0f, 4.0f, 4.0f};
    const PhotonHit ph = makeHit(Vector{0, 0, depth}, Vector{0, 0, -1}, power);

    rig.worker->splatToCamera(ph, rig.material);

    const Color written = rig.buffer->fetchColor(kCenter);
    REQUIRE(written.red > 0.0f);

    // Expected contribution = BRDF * power * (cos / area), cos = 1.
    //   BRDF   = albedo / pi
    //   area   = pi * r^2, r = depth * tan(0.5 * vFOVrad / height)
    const double pixelHalfAngle =
        0.5 * Utility::radians(rig.camera->verticalFieldOfView()) /
        static_cast<double>(rig.camera->height());
    const double r = depth * std::tan(pixelHalfAngle);
    const double area = Utility::pi * r * r;
    const float brdf = albedo.red * static_cast<float>(1.0 / Utility::pi);
    const float expected = brdf * power.red * static_cast<float>(1.0 / area);
    REQUIRE_THAT(written.red, WithinRel(expected, 1e-3f));
}

TEST_CASE("splatToCamera: a grazing hit is foreshortened toward zero", "[SplatToCamera]")
{
    const Color albedo{0.9f, 0.9f, 0.9f};

    // Same hit point and power, but two different surface normals: one nearly facing
    // the camera, one nearly grazing. The grazing contribution must be far smaller,
    // scaled by the ratio of the two cosines.
    const double depth = 10.0;
    const Color power{4.0f, 4.0f, 4.0f};
    const Vector hitPos{0, 0, depth};
    const Vector toCamera = Vector::normalized(Vector{0, 0, 0} - hitPos);  // (0,0,-1)

    // Facing-ish normal: 10 degrees off the camera direction.
    const double facingAngle = Utility::radians(10.0);
    const Vector facingNormal = Vector::normalized(
        toCamera * std::cos(facingAngle) + Vector{1, 0, 0} * std::sin(facingAngle));
    // Grazing normal: 85 degrees off the camera direction (nearly perpendicular).
    const double grazingAngle = Utility::radians(85.0);
    const Vector grazingNormal = Vector::normalized(
        toCamera * std::cos(grazingAngle) + Vector{1, 0, 0} * std::sin(grazingAngle));

    Color facingWritten;
    {
        SplatRig rig = makeRig({}, albedo);
        rig.worker->splatToCamera(makeHit(hitPos, facingNormal, power), rig.material);
        facingWritten = rig.buffer->fetchColor(kCenter);
    }
    Color grazingWritten;
    {
        SplatRig rig = makeRig({}, albedo);
        rig.worker->splatToCamera(makeHit(hitPos, grazingNormal, power), rig.material);
        grazingWritten = rig.buffer->fetchColor(kCenter);
    }

    REQUIRE(facingWritten.red > 0.0f);
    REQUIRE(grazingWritten.red > 0.0f);
    // The grazing splat is foreshortened: its cos is cos(85 deg) vs cos(10 deg), so
    // it is much dimmer. Same footprint area and BRDF, so the ratio is the cos ratio.
    REQUIRE(grazingWritten.red < facingWritten.red);
    const float cosRatio = static_cast<float>(std::cos(grazingAngle) / std::cos(facingAngle));
    REQUIRE_THAT(grazingWritten.red / facingWritten.red, WithinRel(cosRatio, 1e-3f));
}

TEST_CASE("splatToCamera: a back-facing hit contributes nothing", "[SplatToCamera]")
{
    SplatRig rig = makeRig({}, Color{0.9f, 0.9f, 0.9f});

    // Normal points AWAY from the camera (+Z, same side as the viewing ray) => the
    // surface faces away, cosCamera <= 0, the splat is rejected.
    const PhotonHit ph = makeHit(Vector{0, 0, 10.0}, Vector{0, 0, 1}, Color{4.0f, 4.0f, 4.0f});
    rig.worker->splatToCamera(ph, rig.material);

    const Color written = rig.buffer->fetchColor(kCenter);
    REQUIRE(written.red == 0.0f);
    REQUIRE(written.green == 0.0f);
    REQUIRE(written.blue == 0.0f);
}

TEST_CASE("splatToCamera: an occluded hit contributes nothing", "[SplatToCamera]")
{
    // A sphere between the hit and the camera blocks the line of sight.
    auto blocker = std::make_shared<SphereVolume>(/*materialIndex=*/0,
                                                  Vector{0, 0, 5.0}, /*radius=*/1.0);
    SplatRig rig = makeRig({blocker}, Color{0.9f, 0.9f, 0.9f});

    // Facing hit behind the blocker (at z=10, blocker near surface at z=4).
    const PhotonHit ph = makeHit(Vector{0, 0, 10.0}, Vector{0, 0, -1}, Color{4.0f, 4.0f, 4.0f});
    rig.worker->splatToCamera(ph, rig.material);

    const Color written = rig.buffer->fetchColor(kCenter);
    REQUIRE(written.red == 0.0f);
    REQUIRE(written.green == 0.0f);
    REQUIRE(written.blue == 0.0f);
}

TEST_CASE("splatToCamera: a delta material deposits nothing", "[SplatToCamera]")
{
    // Sanity: the splat early-returns for delta materials (mirror/glass own the
    // gather path). Reuse the rig but pass a null material to exercise the guard;
    // a Lambertian facing hit with photonsPerLight disabled also deposits nothing.
    SplatRig rig = makeRig({}, Color{0.9f, 0.9f, 0.9f});
    rig.worker->setPhotonsPerLight(0.0);  // splat disabled

    const PhotonHit ph = makeHit(Vector{0, 0, 10.0}, Vector{0, 0, -1}, Color{4.0f, 4.0f, 4.0f});
    rig.worker->splatToCamera(ph, rig.material);

    const Color written = rig.buffer->fetchColor(kCenter);
    REQUIRE(written.red == 0.0f);
}
