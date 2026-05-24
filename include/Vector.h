#pragma once

#include "RandomGenerator.h"

// AVX intrinsics are x86-only. On Apple Silicon (and any non-x86 target) we
// fall back to a scalar implementation that exposes the same Vector API.
// The original AVX path is preserved behind __AVX__ so an x86 build still
// gets the vectorized version.
#if defined(__AVX__)
#  include <immintrin.h>
#  include <xmmintrin.h>
#  define RAY_TRACER_HAS_AVX 1
#else
#  define RAY_TRACER_HAS_AVX 0
#endif

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
#if RAY_TRACER_HAS_AVX
    Vector(__m256d&& idata) noexcept;
#endif
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
#if RAY_TRACER_HAS_AVX
    explicit operator __m256d() noexcept;
#endif
    explicit operator const double*() noexcept;

    double magnitudeSquared() const noexcept;
    double magnitude() const noexcept;
    Vector normalize() noexcept;
    Vector normalized() const noexcept;

    static double angleBetween(const Vector& a, const Vector& b) noexcept;
    static Vector cross(const Vector& a, const Vector& b) noexcept;
#if RAY_TRACER_HAS_AVX
    static double dot(const __m256d& a, const __m256d& b) noexcept;
#endif
    static double dot(const Vector& a, const Vector& b) noexcept;
#if RAY_TRACER_HAS_AVX
    static __m256d normalized(const __m256d& a) noexcept;
#endif
    static Vector normalized(const Vector& a) noexcept;
    static Vector normalizedSub(const Vector& lhs, const Vector& rhs) noexcept;
    static Vector reflected(const Vector& incident, const Vector& normal) noexcept;
    static Vector random(RandomGenerator& generator, double magnitude = 1.0f);
    static Vector randomSphere(RandomGenerator& generator, double magnitude = 1.0f);

    static const Vector unitX;
    static const Vector unitY;
    static const Vector unitZ;

#if RAY_TRACER_HAS_AVX
    __m256d data;
#endif

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
