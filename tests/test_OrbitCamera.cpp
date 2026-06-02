#include <catch2/catch_all.hpp>

#include "OrbitCamera.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using Catch::Matchers::WithinAbs;

TEST_CASE("OrbitCamera eye position sits on a sphere around the target", "[OrbitCamera]")
{
    OrbitCamera cam;
    cam.target = glm::vec3(1.0f, 2.0f, 3.0f);
    cam.distance = 10.0f;
    cam.yaw = 0.7f;
    cam.pitch = 0.4f;

    const glm::vec3 eye = cam.eye();
    const float dist = glm::length(eye - cam.target);
    REQUIRE_THAT(dist, WithinAbs(10.0, 1e-4));
}

TEST_CASE("OrbitCamera orbit clamps pitch away from the poles", "[OrbitCamera]")
{
    OrbitCamera cam;
    cam.pitch = 0.0f;

    cam.orbit(0.0f, 100.0f);  // try to drive pitch way past the pole
    REQUIRE(cam.pitch <= OrbitCamera::kPitchLimit);

    cam.orbit(0.0f, -100.0f);
    REQUIRE(cam.pitch >= -OrbitCamera::kPitchLimit);
}

TEST_CASE("OrbitCamera dolly scales distance multiplicatively and stays positive", "[OrbitCamera]")
{
    OrbitCamera cam;
    cam.distance = 8.0f;

    cam.dolly(0.5f);
    REQUIRE_THAT(cam.distance, WithinAbs(4.0, 1e-5));

    // Driving distance toward zero must not produce a non-positive distance.
    for (int i = 0; i < 200; ++i)
    {
        cam.dolly(0.5f);
    }
    REQUIRE(cam.distance > 0.0f);
}

TEST_CASE("OrbitCamera view matrix maps the eye to the origin", "[OrbitCamera]")
{
    OrbitCamera cam;
    cam.target = glm::vec3(0.0f);
    cam.distance = 5.0f;
    cam.yaw = 0.3f;
    cam.pitch = 0.2f;

    const glm::mat4 view = cam.viewMatrix();
    const glm::vec4 eyeInView = view * glm::vec4(cam.eye(), 1.0f);

    // The camera eye should map to (approximately) the origin in view space.
    REQUIRE_THAT(eyeInView.x, WithinAbs(0.0, 1e-3));
    REQUIRE_THAT(eyeInView.y, WithinAbs(0.0, 1e-3));
    REQUIRE_THAT(eyeInView.z, WithinAbs(0.0, 1e-3));
}

TEST_CASE("OrbitCamera view matrix places the target down the -Z axis", "[OrbitCamera]")
{
    OrbitCamera cam;
    cam.target = glm::vec3(2.0f, -1.0f, 4.0f);
    cam.distance = 6.0f;
    cam.yaw = -0.5f;
    cam.pitch = 0.25f;

    const glm::mat4 view = cam.viewMatrix();
    const glm::vec4 targetInView = view * glm::vec4(cam.target, 1.0f);

    // The look-at target is in front of the camera => negative Z in view space,
    // and centered on the view axis (x and y near zero).
    REQUIRE(targetInView.z < 0.0f);
    REQUIRE_THAT(targetInView.x, WithinAbs(0.0, 1e-3));
    REQUIRE_THAT(targetInView.y, WithinAbs(0.0, 1e-3));
    REQUIRE_THAT(targetInView.z, WithinAbs(-cam.distance, 1e-3));
}

TEST_CASE("OrbitCamera frameBounds centers the target and fits the box", "[OrbitCamera]")
{
    OrbitCamera cam;
    const glm::vec3 minB(-2.0f, -4.0f, -6.0f);
    const glm::vec3 maxB(2.0f, 4.0f, 6.0f);

    cam.frameBounds(minB, maxB);

    // Target is the box center.
    REQUIRE_THAT(cam.target.x, WithinAbs(0.0, 1e-5));
    REQUIRE_THAT(cam.target.y, WithinAbs(0.0, 1e-5));
    REQUIRE_THAT(cam.target.z, WithinAbs(0.0, 1e-5));

    // Distance is large enough that the bounding sphere fits in the FOV: the
    // sphere radius is half the box diagonal.
    const float radius = 0.5f * glm::length(maxB - minB);
    REQUIRE(cam.distance > radius);
}
