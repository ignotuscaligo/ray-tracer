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
    Vector(Vector&& other) noexcept;
    ~Vector() = default;

    double getAxis(Axis axis) const noexcept;
    double operator[](Axis axis) const noexcept;

    Vector operator=(const Vector& rhs) noexcept;
    Vector operator=(Vector&& rhs) noexcept;
    Vector operator+=(const Vector& rhs) noexcept;
    Vector operator*=(double rhs) noexcept;
    Vector operator/=(double rhs) noexcept;
    explicit operator __m256d() noexcept;
    explicit operator const double*() noexcept;

    double magnitudeSquared() const noexcept;
    double magnitude() const noexcept;
    Vector normalize() noexcept;
    Vector normalized() const noexcept;

    static Vector cross(const Vector& a, const Vector& b) noexcept;
    static double dot(const __m256d& a, const __m256d& b) noexcept;
    static double dot(const Vector& a, const Vector& b) noexcept;
    static __m256d normalized(const __m256d& a) noexcept;
    static Vector normalized(const Vector& a) noexcept;
    static Vector normalizedSub(const Vector& lhs, const Vector& rhs) noexcept;
    static Vector reflected(const Vector& incident, const Vector& normal) noexcept;
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

Vector operator+(const Vector& lhs, const Vector& rhs) noexcept;
Vector operator-(const Vector& point) noexcept;
Vector operator-(const Vector& lhs, const Vector& rhs) noexcept;
Vector operator*(const Vector& lhs, const double& rhs) noexcept;
Vector operator*(const double& lhs, const Vector& rhs) noexcept;
Vector operator/(const Vector& lhs, const double& rhs) noexcept;
