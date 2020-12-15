#include "Vector.h"

#include "Utility.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <limits>

#define _USE_MATH_DEFINES
#include <math.h>

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
    __m128 input = _mm_set1_ps(magnitudeSquared());
    __m128 invroot = _mm_rsqrt_ps(input);

    data = _mm_mul_ps(data, invroot);

    return *this;
}

Vector Vector::normalized() const
{
    // mul = this * this
    __m128 mul = _mm_mul_ps(data, data);

    // magSqr =  mul.x + mul.y + mul.z
    __m128 magSqr = _mm_set1_ps(mul.m128_f32[0] + mul.m128_f32[1] + mul.m128_f32[2]);

    // invRoot = inverse_square(magSqr)
    __m128 invRoot = _mm_rsqrt_ps(magSqr);

    // result = this * invRoot
    return _mm_mul_ps(data, invRoot);
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

// r = incident − 2 * (incident ⋅ normal) * normal
Vector Vector::reflected(const Vector& incident, const Vector& normal)
{
    __m128 mul = _mm_mul_ps(incident.data, normal.data);

    __m128 dot2 = _mm_set1_ps(2 * (mul.m128_f32[0] + mul.m128_f32[1] + mul.m128_f32[2]));

    return _mm_sub_ps(incident.data, _mm_mul_ps(dot2, normal.data));
}

Vector Vector::random(float magnitude)
{
    return Vector::randomSphere() * Utility::random(magnitude);
}

Vector Vector::randomSphere(float magnitude)
{
    float theta = 2 * M_PI * Utility::random();
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
