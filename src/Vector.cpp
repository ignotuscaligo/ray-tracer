#include "Vector.h"

#include <cmath>
#include <limits>

Axis nextAxis(Axis axis)
{
    return static_cast<Axis>((static_cast<int>(axis) + 1) % 3);
}

Vector::Vector()
    : x(0.0f)
    , y(0.0f)
    , z(0.0f)
{
}

Vector::Vector(float ix, float iy, float iz)
    : x(ix)
    , y(iy)
    , z(iz)
{
}

float Vector::getAxis(Axis axis) const
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

float Vector::operator[](Axis axis) const
{
    return getAxis(axis);
}

Vector Vector::operator=(const Vector& rhs)
{
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    return *this;
}

Vector Vector::operator+=(const Vector& rhs)
{
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    return *this;
}

Vector Vector::operator/=(float rhs)
{
    x /= rhs;
    y /= rhs;
    z /= rhs;
    return *this;
}

float Vector::magnitude() const
{
    return std::sqrt((x * x) + (y * y) + (z * z));
}

Vector Vector::normalize()
{
    float hyp = magnitude();

    if (hyp > std::numeric_limits<float>::epsilon())
    {
        *this /= hyp;
    }

    return *this;
}

Vector cross(const Vector& a, const Vector& b)
{
    return Vector(
        (a.y * b.z) - (a.z * b.y),
        (a.z * b.x) - (a.x * b.z),
        (a.x * b.y) - (a.y * b.x)
    );
}

float dot(const Vector& a, const Vector& b)
{
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}
