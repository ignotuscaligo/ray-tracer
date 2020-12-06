#include "Bounds.h"

#include <algorithm>

Limits::Limits()
    : min(0.0f)
    , max(0.0f)
{
}

Limits::Limits(float imin, float imax)
    : min(imin)
    , max(imax)
{
}

Limits::Limits(float value)
    : Limits(value, value)
{
}

bool Limits::contains(float value) const
{
    return value >= min && value <= max;
}

bool Limits::intersects(const Limits& other) const
{
    float highestMin = std::max(min, other.min);
    float lowestMax = std::min(max, other.max);
    return lowestMax >= highestMin;
}

Limits Limits::operator=(const Limits& rhs)
{
    min = rhs.min;
    max = rhs.max;
    return *this;
}

Limits Limits::operator+=(const Limits& rhs)
{
    min = std::min(min, rhs.min);
    max = std::max(max, rhs.max);
    return *this;
}

Bounds::Bounds()
{
}

Bounds::Bounds(Limits ix, Limits iy, Limits iz)
    : x(ix)
    , y(iy)
    , z(iz)
{
}

Bounds::Bounds(Vector vector)
    : Bounds({vector.x}, {vector.y}, {vector.z})
{
}

void Bounds::extend(Limits limits, Axis axis)
{
    switch (axis)
    {
        case Axis::X:
            x += limits;
            break;

        case Axis::Y:
            y += limits;
            break;

        case Axis::Z:
            z += limits;
            break;
    }
}

Limits Bounds::getLimits(Axis axis) const
{
    switch (axis)
    {
        case Axis::X:
            return x;

        case Axis::Y:
            return y;

        case Axis::Z:
            return z;
    }
}

Limits Bounds::operator[](Axis axis) const
{
    return getLimits(axis);
}

bool Bounds::contains(const Vector& vector) const
{
    return x.contains(vector.x)
        && y.contains(vector.y)
        && z.contains(vector.z);
}

bool Bounds::intersects(const Bounds& other) const
{
    return x.intersects(other.x)
        && y.intersects(other.y)
        && z.intersects(other.z);
}

Bounds Bounds::operator=(const Bounds& rhs)
{
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;

    return *this;
}

Bounds Bounds::operator+=(const Bounds& rhs)
{
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;

    return *this;
}
