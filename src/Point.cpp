#include "Point.h"

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
