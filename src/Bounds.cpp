#include "Bounds.h"

#include <algorithm>

Limits::Limits()
    : min(0.0)
    , max(0.0)
{
}

Limits::Limits(double imin, double imax)
    : min(imin)
    , max(imax)
{
}

Limits::Limits(double value)
    : Limits(value, value)
{
}

bool Limits::contains(double value) const
{
    return value >= min && value <= max;
}

bool Limits::intersects(const Limits& other) const
{
    double highestMin = std::max(min, other.min);
    double lowestMax = std::min(max, other.max);
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

Bounds::Bounds(Vector min, Vector max)
    : x(min.x, max.x)
    , y(min.y, max.y)
    , z(min.z, max.z)
{
}

void Bounds::extend(Limits limits, Axis axis)
{
    if (axis == Axis::X)
    {
        x += limits;
    }
    else if (axis == Axis::Y)
    {
        y += limits;
    }
    else
    {
        z += limits;
    }
}

Limits Bounds::getLimits(Axis axis) const
{
    if (axis == Axis::X)
    {
        return x;
    }
    else if (axis == Axis::Y)
    {
        return y;
    }
    else
    {
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

Vector Bounds::minimum() const
{
    return {
        x.min,
        y.min,
        z.min
    };
}

Vector Bounds::maximum() const
{
    return {
        x.max,
        y.max,
        z.max
    };
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
