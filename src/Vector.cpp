#include "Vector.h"

#include "Utility.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <limits>

#define USE_APPX_INV_SQR 0

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
    return Vector::dot(data, data);
}

float Vector::magnitude() const
{
    return std::sqrt(magnitudeSquared());
}

Vector Vector::normalize()
{
    data = Vector::normalized(data);

    return *this;
}

Vector Vector::normalized() const
{
    return Vector::normalized(data);
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

float Vector::dot(const __m128& a, const __m128& b)
{
    __m128 mul = _mm_mul_ps(a, b);

    return mul.m128_f32[0] + mul.m128_f32[1] + mul.m128_f32[2];
}

float Vector::dot(const Vector& a, const Vector& b)
{
    return Vector::dot(a.data, b.data);
}

__m128 Vector::normalized(const __m128& a)
{
    __m128 dot = _mm_set1_ps(Vector::dot(a, a));

#if USE_APPX_INV_SQR
    // invRoot = inverse_square(dot)
    __m128 invRoot = _mm_rsqrt_ps(dot);

    // result = a * invRoot
    return _mm_mul_ps(a, invRoot);
#else
    // root = sqrt(dot)
    __m128 root = _mm_sqrt_ps(dot);

    // result = a / root
    return _mm_div_ps(a, root);
#endif
}

Vector Vector::normalized(const Vector& a)
{
    return Vector::normalized(a.data);
}

Vector Vector::normalizedSub(const Vector& lhs, const Vector& rhs)
{
    // sub = lhs - rhs
    __m128 sub = _mm_sub_ps(lhs.data, rhs.data);

    // result = sub * invRoot
    return Vector::normalized(sub);
}

// r = incident − 2 * (incident ⋅ normal) * normal
Vector Vector::reflected(const Vector& incident, const Vector& normal)
{
    __m128 dot2 = _mm_set1_ps(2 * (Vector::dot(incident, normal)));

    return _mm_sub_ps(incident.data, _mm_mul_ps(dot2, normal.data));
}

Vector Vector::random(float magnitude)
{
    return Vector::randomSphere() * Utility::random(magnitude);
}

Vector Vector::randomSphere(float magnitude)
{
    float theta = 2 * Utility::pi * Utility::random();
    float phi = std::acos(1.0f - 2.0f * Utility::random());

    return {
        std::sin(phi) * std::cos(theta) * magnitude,
        std::sin(phi) * std::sin(theta) * magnitude,
        std::cos(phi) * magnitude
    };
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

Vector operator/(const Vector& lhs, const float& rhs)
{
    return _mm_div_ps(lhs.data, _mm_set1_ps(rhs));
}
