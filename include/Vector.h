#pragma once

#include "RandomGenerator.h"

#include <xmmintrin.h>

enum class Axis
{
    X,
    Y,
    Z
};

Axis nextAxis(Axis axis);

union Vector
{
    Vector();
    Vector(float ix, float iy, float iz);
    Vector(__m128&& idata);

    float getAxis(Axis axis) const;
    float operator[](Axis axis) const;

    Vector operator=(const Vector& rhs);
    Vector operator+=(const Vector& rhs);
    Vector operator*=(float rhs);
    Vector operator/=(float rhs);
    explicit operator __m128();
    explicit operator const float*();

    float magnitudeSquared() const;
    float magnitude() const;
    Vector normalize();
    Vector normalized() const;

    static Vector cross(const Vector& a, const Vector& b);
    static float dot(const __m128& a, const __m128& b);
    static float dot(const Vector& a, const Vector& b);
    static __m128 normalized(const __m128& a);
    static Vector normalized(const Vector& a);
    static Vector normalizedSub(const Vector& lhs, const Vector& rhs);
    static Vector reflected(const Vector& incident, const Vector& normal);
    static Vector random(RandomGenerator& generator, float magnitude = 1.0f);
    static Vector randomSphere(RandomGenerator& generator, float magnitude = 1.0f);

    static const Vector unitX;
    static const Vector unitY;
    static const Vector unitZ;

    __m128 data;

    struct alignas(float)
    {
        float x, y, z, _w;
    };
};

Vector operator+(const Vector& lhs, const Vector& rhs);
Vector operator-(const Vector& point);
Vector operator-(const Vector& lhs, const Vector& rhs);
Vector operator*(const Vector& lhs, const float& rhs);
Vector operator*(const float& lhs, const Vector& rhs);
Vector operator/(const Vector& lhs, const float& rhs);
