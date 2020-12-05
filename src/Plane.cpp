#include "Plane.h"

Plane::Plane(const Vector& a, const Vector& b, const Vector& c)
{
    normal = Vector::cross(b - a, c - a).normalize();
    dot = Vector::dot(normal, a);
}

bool Plane::pointAbovePlane(const Vector& point) const
{
    return Vector::dot(normal, point) >= dot;
}
