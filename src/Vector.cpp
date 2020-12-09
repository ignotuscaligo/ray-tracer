#include "Vector.h"

#include <cassert>
#include <cmath>
#include <limits>

Axis nextAxis(Axis axis)
{
    return static_cast<Axis>((static_cast<int>(axis) + 1) % 3);
}

Vector::Vector()
    : x(0)
    , y(0)
    , z(0)
    , _w(0)
{
}

Vector::Vector(float ix, float iy, float iz)
    : x(ix)
    , y(iy)
    , z(iz)
    , _w(0)
{
}

Vector::Vector(__m128&& idata)
    : data(idata)
    , _w(0)
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
    data = rhs.data;
    return *this;
}

Vector Vector::operator+=(const Vector& rhs)
{
    data = _mm_add_ps(data, rhs.data);
    return *this;
}

Vector Vector::operator*=(float rhs)
{
    data = _mm_mul_ps(data, _mm_set1_ps(rhs));
    return *this;
}

Vector Vector::operator/=(float rhs)
{
    data = _mm_div_ps(data, _mm_set1_ps(rhs));
    return *this;
}

Vector::operator __m128()
{
    return data;
}

Vector::operator const float*()
{
    return &x;
}

float Vector::magnitudeSquared() const
{
    return Vector::dot(*this, *this);
}

float Vector::magnitude() const
{
    return std::sqrt(magnitudeSquared());
}

Vector Vector::normalize()
{
    __m128 input = _mm_set_ps(0, 0, 0, magnitudeSquared());
    __m128 invroot = _mm_rsqrt_ps(input);

    *this *= invroot.m128_f32[0];

    return *this;
}

Vector Vector::cross(const Vector& a, const Vector& b)
{
    // y, z, x
    __m128 aA = _mm_set_ps(0, a.x, a.z, a.y);

    // z, x, y
    __m128 aB = _mm_set_ps(0, a.y, a.x, a.z);

    // y, z, x
    __m128 bA = _mm_set_ps(0, b.x, b.z, b.y);

    // z, x, y
    __m128 bB = _mm_set_ps(0, b.y, b.x, b.z);

    return _mm_sub_ps(_mm_mul_ps(aA, bB), _mm_mul_ps(aB, bA));
}

float Vector::dot(const Vector& a, const Vector& b)
{
    __m128 mul = _mm_mul_ps(a.data, b.data);

    return mul.m128_f32[0] + mul.m128_f32[1] + mul.m128_f32[2];
}

Vector Vector::normalizedSub(const Vector& lhs, const Vector& rhs)
{
    // sub = lhs - rhs
    __m128 sub = _mm_sub_ps(lhs.data, rhs.data);

    // mul = sub * sub
    __m128 mul = _mm_mul_ps(sub, sub);

    // magSqr =  mul.x + mul.y + mul.z
    __m128 magSqr = _mm_set1_ps(mul.m128_f32[0] + mul.m128_f32[1] + mul.m128_f32[2]);

    // invRoot = inverse_square(magSqr)
    __m128 invRoot = _mm_rsqrt_ps(magSqr);

    // result = sub * invRoot
    return _mm_mul_ps(sub, invRoot);
}

Vector operator+(const Vector& lhs, const Vector& rhs)
{
    return _mm_add_ps(lhs.data, rhs.data);
}

Vector operator-(const Vector& vector)
{
    return _mm_sub_ps(_mm_set1_ps(0.0), vector.data);
}

Vector operator-(const Vector& lhs, const Vector& rhs)
{
    return _mm_sub_ps(lhs.data, rhs.data);
}

Vector operator*(const Vector& lhs, const float& rhs)
{
    return _mm_mul_ps(lhs.data, _mm_set1_ps(rhs));
}

Vector operator*(const float& lhs, const Vector& rhs)
{
    return _mm_mul_ps(_mm_set1_ps(lhs), rhs.data);
}
