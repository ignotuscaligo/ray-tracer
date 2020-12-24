#pragma once

#include "Vector.h"

struct Bounds;
struct Quaternion;

struct Pyramid
{
    Pyramid() = default;
    Pyramid(const Vector& position, const Quaternion& rotation, double verticalFieldOfView, double horizontalFieldOfView);
    Pyramid(const Vector& position, const Quaternion& rotation, double pitch, double yaw, double pitchStep, double yawStep);

    bool containsPoint(const Vector& point) const;
    bool intersectsBounds(const Bounds& bounds) const;
    Vector relativePositionInFrustum(const Vector& point) const;

    Vector origin;
    Vector direction;
    Vector vertical;
    Vector horizontal;
    double verticalDot = 0;
    double horizontalDot = 0;
};
