#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

// A simple orbit (arcball-style) camera. The camera always looks at a target
// point and is positioned on a sphere around it, parameterized by yaw, pitch,
// and distance. Mouse drag orbits (changes yaw/pitch); scroll dollies in/out
// (changes distance).
//
// Header-only so the math is trivially unit-testable without GL.
class OrbitCamera
{
public:
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    float distance = 5.0f;
    float yaw = 0.0f;    // radians, around world +Y
    float pitch = 0.3f;  // radians, clamped away from the poles
    float fovYRadians = glm::radians(45.0f);
    float nearPlane = 0.05f;
    float farPlane = 10000.0f;

    // Clamp pitch to just inside the poles so the view direction never becomes
    // parallel to the up vector (which would make the lookAt basis degenerate).
    static constexpr float kPitchLimit = 1.55334f;  // ~89 degrees

    void orbit(float deltaYaw, float deltaPitch)
    {
        yaw += deltaYaw;
        pitch += deltaPitch;
        if (pitch > kPitchLimit) pitch = kPitchLimit;
        if (pitch < -kPitchLimit) pitch = -kPitchLimit;
    }

    void dolly(float factor)
    {
        // Multiplicative zoom keeps the perceived speed roughly constant across
        // scales and prevents the distance from crossing zero.
        distance *= factor;
        if (distance < 1e-3f) distance = 1e-3f;
    }

    // Pan the camera by sliding the target within the camera's view plane.
    // `deltaRight`/`deltaUp` are in target-space world units (the caller scales
    // raw pixel deltas by distance so panning feels consistent at any zoom).
    // Because eye() is derived from target, moving the target moves the whole
    // camera rig — the classic DCC "track" / "truck" pan.
    void pan(float deltaRight, float deltaUp)
    {
        const glm::vec3 forward = glm::normalize(target - eye());
        const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        glm::vec3 right = glm::cross(forward, worldUp);
        const float rightLen = glm::length(right);
        // Near the poles forward ~ worldUp; fall back to a stable right vector.
        right = rightLen > 1e-5f ? right / rightLen : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 up = glm::normalize(glm::cross(right, forward));
        target += right * deltaRight + up * deltaUp;
    }

    // Default framing for an empty scene: look at the world origin from a
    // three-quarter angle above the XZ ground plane (the standard DCC startup
    // pose). The grid fills the lower view and the gnomon sits at screen center.
    void frameOrigin(float viewDistance = 12.0f)
    {
        target = glm::vec3(0.0f, 0.0f, 0.0f);
        distance = viewDistance;
        yaw = glm::radians(45.0f);
        pitch = glm::radians(30.0f);
        nearPlane = 0.05f;
        farPlane = std::max(viewDistance * 100.0f, 1000.0f);
    }

    glm::vec3 eye() const
    {
        const float cosPitch = std::cos(pitch);
        const glm::vec3 offset{
            distance * cosPitch * std::sin(yaw),
            distance * std::sin(pitch),
            distance * cosPitch * std::cos(yaw)};
        return target + offset;
    }

    glm::mat4 viewMatrix() const
    {
        return glm::lookAt(eye(), target, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    glm::mat4 projectionMatrix(float aspect) const
    {
        if (aspect <= 0.0f) aspect = 1.0f;
        return glm::perspective(fovYRadians, aspect, nearPlane, farPlane);
    }

    // Frame the camera so an axis-aligned bounding box [minB, maxB] fits in
    // view. Sets the target to the box center and the distance so the box's
    // bounding sphere fits within the vertical FOV with a small margin.
    void frameBounds(const glm::vec3& minB, const glm::vec3& maxB)
    {
        target = 0.5f * (minB + maxB);
        const float radius = 0.5f * glm::length(maxB - minB);
        const float safeRadius = radius > 1e-4f ? radius : 1.0f;
        // distance such that the bounding sphere subtends the vertical FOV.
        distance = (safeRadius / std::sin(fovYRadians * 0.5f)) * 1.25f;
        nearPlane = std::max(distance * 0.001f, 1e-3f);
        farPlane = distance * 100.0f;
    }
};
