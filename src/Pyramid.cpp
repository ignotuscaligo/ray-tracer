#include "Pyramid.h"

#include "Bounds.h"
#include "Vector.h"
#include "Quaternion.h"
#include "Utility.h"

#include <cmath>

Pyramid::Pyramid(const Vector& position, const Quaternion& rotation, double verticalFieldOfView, double horizontalFieldOfView)
{
    origin = position;
    direction = rotation * Vector(0, 0, 1);
    vertical = rotation * Vector(0, std::sin(Utility::radians(verticalFieldOfView) / 2.0), 0);
    horizontal = rotation * Vector(std::sin(Utility::radians(horizontalFieldOfView) / 2.0), 0, 0);

    verticalDot = vertical.magnitudeSquared();
    horizontalDot = horizontal.magnitudeSquared();
}

Pyramid::Pyramid(const Vector& position, const Quaternion& rotation, double pitch, double yaw, double pitchStep, double yawStep)
{
    origin = position;

    Quaternion centerRotation = Quaternion::fromPitchYawRoll(pitch, yaw, 0);

    vertical = {0, std::sin(pitchStep / 2.0), 0};
    horizontal = {std::sin(yawStep / 2.0), 0, 0};

    direction = (rotation * (centerRotation * Vector(0, 0, 1)));

    vertical = (rotation * (centerRotation * vertical));
    horizontal = (rotation * (centerRotation * horizontal));

    verticalDot = vertical.magnitude();
    verticalDot *= verticalDot;
    horizontalDot = horizontal.magnitude();
    horizontalDot *= horizontalDot;
}

bool Pyramid::containsPoint(const Vector& point) const
{
    Vector test = Vector::normalizedSub(point, origin);

    return Vector::dot(test, direction) > 0
        && std::abs(Vector::dot(test, vertical)) <= verticalDot
        && std::abs(Vector::dot(test, horizontal)) <= horizontalDot;
}

bool Pyramid::intersectsBounds(const Bounds& bounds) const
{
    Vector minTest = Vector::normalizedSub(bounds.minimum(), origin);
    Vector maxTest = Vector::normalizedSub(bounds.maximum(), origin);

    double vertMag = vertical.magnitude();
    double horizMag = horizontal.magnitude();

    double minVert = Vector::dot(minTest * vertMag, vertical);
    double maxVert = Vector::dot(maxTest * vertMag, vertical);

    if (minVert > verticalDot && maxVert > verticalDot)
    {
        return false;
    }

    if (minVert < -verticalDot && maxVert < -verticalDot)
    {
        return false;
    }

    double minHoriz = Vector::dot(minTest * horizMag, horizontal);
    double maxHoriz = Vector::dot(maxTest * horizMag, horizontal);

    if (minHoriz > horizontalDot && maxHoriz > horizontalDot)
    {
        return false;
    }

    if (minHoriz < -horizontalDot && maxHoriz < -horizontalDot)
    {
        return false;
    }

    return true;
}

Vector Pyramid::relativePositionInFrustum(const Vector& point) const
{
    Vector test = Vector::normalizedSub(point, origin);

    return {
        ((Vector::dot(test, horizontal) / horizontalDot) + 1.0) / 2.0,
        ((Vector::dot(test, vertical) / verticalDot) + 1.0) / 2.0,
        Vector::dot(test, direction)
    };
}
