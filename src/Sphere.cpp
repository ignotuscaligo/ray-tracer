#include "Sphere.h"

Sphere::Sphere(const Vector& center, double radius)
    : center(center)
    , radius(radius)
{
}

bool Sphere::containsPoint(const Vector& point) const
{
    return (point - center).magnitudeSquared() <= radius * radius;
}
