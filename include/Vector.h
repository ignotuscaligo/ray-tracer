#pragma once

#include "RandomGenerator.h"

#include <immintrin.h>
#include <xmmintrin.h>

enum class Axis
{
    X,
    Y,
    Z
};

constexpr Axis nextAxis(Axis axis) noexcept
{
    return static_cast<Axis>((static_cast<int>(axis) + 1) % 3);
}

union Vector
{
    Vector() noexcept;
    Vector(double ix, double iy, double iz) noexcept;
    Vector(__m256d&& idata) noexcept;
    Vector(const Vector& other) noexcept;

    double getAxis(Axis axis) const noexcept;
    double operator[](Axis axis) const noexcept;

    Vector operator=(const Vector& rhs);
    Vector operator+=(const Vector& rhs);
    Vector operator*=(double rhs);
    Vector operator/=(double rhs);
    explicit operator __m256d();
    explicit operator const double*();

    double magnitudeSquared() const;
    double magnitude() const;
    Vector normalize();
    Vector normalized() const;

    static Vector cross(const Vector& a, const Vector& b);
    static double dot(const __m256d& a, const __m256d& b);
    static double dot(const Vector& a, const Vector& b);
    static __m256d normalized(const __m256d& a);
    static Vector normalized(const Vector& a);
    static Vector normalizedSub(const Vector& lhs, const Vector& rhs);
    static Vector reflected(const Vector& incident, const Vector& normal);
    static Vector random(RandomGenerator& generator, double magnitude = 1.0f);
    static Vector randomSphere(RandomGenerator& generator, double magnitude = 1.0f);

    static const Vector unitX;
    static const Vector unitY;
    static const Vector unitZ;

    __m256d data;

    struct alignas(double)
    {
        double x, y, z, _w;
    };
};

Vector operator+(const Vector& lhs, const Vector& rhs);
Vector operator-(const Vector& point);
Vector operator-(const Vector& lhs, const Vector& rhs);
Vector operator*(const Vector& lhs, const double& rhs);
Vector operator*(const double& lhs, const Vector& rhs);
Vector operator/(const Vector& lhs, const double& rhs);
