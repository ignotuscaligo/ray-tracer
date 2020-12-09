#pragma once

#include "Vector.h"

struct Bounds;
struct Quaternion;

struct Pyramid
{
    Pyramid() = default;
    Pyramid(const Vector& position, const Quaternion& rotation, float pitch, float yaw, float pitchStep, float yawStep);

    bool containsPoint(const Vector& point) const;
    bool intersectsBounds(const Bounds& bounds) const;

    Vector origin;
    Vector direction;
    Vector vertical;
    Vector horizontal;
    float verticalDot;
    float horizontalDot;
};