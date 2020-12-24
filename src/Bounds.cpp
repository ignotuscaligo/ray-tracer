#include "Bounds.h"

#include <algorithm>

Limits::Limits(double imin, double imax) noexcept
    : min(imin)
    , max(imax)
{
}

Limits::Limits(double value) noexcept
    : Limits(value, value)
{
}

bool Limits::contains(double value) const noexcept
{
    return value >= min && value <= max;
}

bool Limits::intersects(const Limits& other) const noexcept
{
    const double highestMin = std::max(min, other.min);
    const double lowestMax = std::min(max, other.max);
    return lowestMax >= highestMin;
}

Limits Limits::operator=(const Limits& rhs) noexcept
{
    min = rhs.min;
    max = rhs.max;
    return *this;
}

Limits Limits::operator+=(const Limits& rhs) noexcept
{
    min = std::min(min, rhs.min);
    max = std::max(max, rhs.max);
    return *this;
}

Bounds::Bounds(Limits ix, Limits iy, Limits iz) noexcept
    : x(ix)
    , y(iy)
    , z(iz)
{
}

Bounds::Bounds(Vector vector) noexcept
    : Bounds({vector.x}, {vector.y}, {vector.z})
{
}

Bounds::Bounds(Vector min, Vector max) noexcept
    : x(min.x, max.x)
    , y(min.y, max.y)
    , z(min.z, max.z)
{
}

void Bounds::extend(Limits limits, Axis axis) noexcept
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

Limits Bounds::getLimits(Axis axis) const noexcept
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

Limits Bounds::operator[](Axis axis) const noexcept
{
    return getLimits(axis);
}

bool Bounds::contains(const Vector& vector) const noexcept
{
    return x.contains(vector.x)
        && y.contains(vector.y)
        && z.contains(vector.z);
}

bool Bounds::intersects(const Bounds& other) const noexcept
{
    return x.intersects(other.x)
        && y.intersects(other.y)
        && z.intersects(other.z);
}

Vector Bounds::minimum() const noexcept
{
    return {
        x.min,
        y.min,
        z.min
    };
}

Vector Bounds::maximum() const noexcept
{
    return {
        x.max,
        y.max,
        z.max
    };
}

Bounds Bounds::operator=(const Bounds& rhs) noexcept
{
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;

    return *this;
}

Bounds Bounds::operator+=(const Bounds& rhs) noexcept
{
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;

    return *this;
}
