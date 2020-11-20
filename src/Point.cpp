#include "Point.h"

#include <cmath>
#include <limits>

Axis nextAxis(Axis axis)
{
    return static_cast<Axis>((static_cast<int>(axis) + 1) % 3);
}

Point::Point()
    : x(0.0f)
    , y(0.0f)
    , z(0.0f)
{
}

Point::Point(float ix, float iy, float iz)
    : x(ix)
    , y(iy)
    , z(iz)
{
}

float Point::getAxis(Axis axis) const
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

float Point::operator[](Axis axis) const
{
    return getAxis(axis);
}

Point Point::operator=(const Point& rhs)
{
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    return *this;
}

Point Point::operator+=(const Point& rhs)
{
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    return *this;
}

Point Point::operator/=(float rhs)
{
    x /= rhs;
    y /= rhs;
    z /= rhs;
    return *this;
}

float Point::magnitude() const
{
    return std::sqrt((x * x) + (y * y) + (z * z));
}

void Point::normalize()
{
    float hyp = magnitude();

    if (hyp > std::numeric_limits<float>::epsilon())
    {
        *this /= hyp;
    }
}

Point cross(const Point& a, const Point& b)
{
    return Point(
        (a.y * b.z) - (a.z * b.y),
        (a.z * b.x) - (a.x * b.z),
        (a.x * b.y) - (a.y * b.x)
    );
}

float dot(const Point& a, const Point& b)
{
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}
