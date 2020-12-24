#include "Triangle.h"

#include <algorithm>
#include <initializer_list>

Triangle::Triangle(Vector ia, Vector ib, Vector ic) noexcept
    : a(ia)
    , b(ib)
    , c(ic)
    , center(
        (a.x + b.x + c.x) / 3.0,
        (a.y + b.y + c.y) / 3.0,
        (a.z + b.z + c.z) / 3.0)
{
    const Vector ab = b - a;
    const Vector ac = c - a;
    normal = Vector::cross(ab, ac);
    normal.normalize();
}

Limits Triangle::getLimits(Axis axis) const noexcept
{
    Limits limits;

    const std::initializer_list<double> values = {a.getAxis(axis), b.getAxis(axis), c.getAxis(axis)};

    limits.min = std::min(values);
    limits.max = std::max(values);

    return limits;
}

Bounds Triangle::getBounds() const noexcept
{
    return {
        getLimits(Axis::X),
        getLimits(Axis::Y),
        getLimits(Axis::Z)
    };
}

Vector Triangle::getPosition(const Vector& coords) const noexcept
{
    return (coords.x * a) + (coords.y * b) + (coords.z * c);
}

Vector Triangle::getNormal(const Vector& coords) const noexcept
{
    return (coords.x * aNormal) + (coords.y * bNormal) + (coords.z * cNormal);
}

Triangle operator+(const Triangle& lhs, const Vector& rhs) noexcept
{
    return Triangle(
        lhs.a + rhs,
        lhs.b + rhs,
        lhs.c + rhs
    );
}
