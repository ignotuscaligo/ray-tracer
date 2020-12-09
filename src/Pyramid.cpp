#include "Pyramid.h"

#include "Bounds.h"
#include "Vector.h"
#include "Quaternion.h"

#include <cmath>

Pyramid::Pyramid(const Vector& position, const Quaternion& rotation, float pitch, float yaw, float pitchStep, float yawStep)
{
    origin = position;

    Quaternion centerRotation = Quaternion::fromPitchYawRoll(pitch, yaw, 0);

    vertical = {0, std::sin(pitchStep / 2.0f), 0};
    horizontal = {std::sin(yawStep / 2.0f), 0, 0};

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

    float vertMag = vertical.magnitude();
    float horizMag = horizontal.magnitude();

    float minVert = Vector::dot(minTest * vertMag, vertical);
    float maxVert = Vector::dot(maxTest * vertMag, vertical);

    if (minVert > verticalDot && maxVert > verticalDot)
    {
        return false;
    }

    if (minVert < -verticalDot && maxVert < -verticalDot)
    {
        return false;
    }

    float minHoriz = Vector::dot(minTest * horizMag, horizontal);
    float maxHoriz = Vector::dot(maxTest * horizMag, horizontal);

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
