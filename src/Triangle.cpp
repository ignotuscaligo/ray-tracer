#include "Triangle.h"

#include <algorithm>
#include <initializer_list>

Triangle::Triangle(Point ia, Point ib, Point ic)
    : a(ia)
    , b(ib)
    , c(ic)
    , center(
        (a.x + b.x + c.x) / 3.0f,
        (a.y + b.y + c.y) / 3.0f,
        (a.z + b.z + c.z) / 3.0f)
{
    Point ab = b - a;
    Point ac = c - a;
    normal = cross(ab, ac);
    normal.normalize();
}

Limits Triangle::getLimits(Axis axis) const
{
    Limits limits;

    std::initializer_list<float> values = {a.getAxis(axis), b.getAxis(axis), c.getAxis(axis)};

    limits.min = std::min(values);
    limits.max = std::max(values);

    return limits;
}

Bounds Triangle::getBounds() const
{
    return {
        getLimits(Axis::X),
        getLimits(Axis::Y),
        getLimits(Axis::Z)
    };
}
