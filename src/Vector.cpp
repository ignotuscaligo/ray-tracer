#include "Vector.h"

#include "Utility.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <limits>

#define USE_APPX_INV_SQR 0

const Vector Vector::unitX{1, 0, 0};
const Vector Vector::unitY{0, 1, 0};
const Vector Vector::unitZ{0, 0, 1};

Vector::Vector() noexcept
    : x(0)
    , y(0)
    , z(0)
    , _w(0)
{
}

Vector::Vector(double ix, double iy, double iz) noexcept
    : x(ix)
    , y(iy)
    , z(iz)
    , _w(0)
{
}

#if RAY_TRACER_HAS_AVX
Vector::Vector(__m256d&& idata) noexcept
    : data(std::move(idata))
{
}
#endif

Vector::Vector(const Vector& other) noexcept
#if RAY_TRACER_HAS_AVX
    : data(other.data)
#else
    : x(other.x)
    , y(other.y)
    , z(other.z)
    , _w(other._w)
#endif
{
}

Vector::Vector(Vector&& other) noexcept
#if RAY_TRACER_HAS_AVX
    : data(std::move(other.data))
#else
    : x(other.x)
    , y(other.y)
    , z(other.z)
    , _w(other._w)
#endif
{
}

double Vector::getAxis(Axis axis) const noexcept
{
    if (axis == Axis::X)
    {
        return x;
    }
    else if (axis == Axis::Y)
    {
        return y;
    }
    else
    {
        return z;
    }
}

double Vector::operator[](Axis axis) const noexcept
{
    return getAxis(axis);
}

Vector Vector::operator=(const Vector& rhs) noexcept
{
#if RAY_TRACER_HAS_AVX
    data = rhs.data;
#else
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    _w = rhs._w;
#endif
    return *this;
}

Vector Vector::operator=(Vector&& rhs) noexcept
{
#if RAY_TRACER_HAS_AVX
    data = std::move(rhs.data);
#else
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    _w = rhs._w;
#endif
    return *this;
}

Vector Vector::operator+=(const Vector& rhs) noexcept
{
#if RAY_TRACER_HAS_AVX
    data = _mm256_add_pd(data, rhs.data);
#else
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
#endif
    return *this;
}

Vector Vector::operator*=(double rhs) noexcept
{
#if RAY_TRACER_HAS_AVX
    data = _mm256_mul_pd(data, _mm256_set1_pd(rhs));
#else
    x *= rhs;
    y *= rhs;
    z *= rhs;
#endif
    return *this;
}

Vector Vector::operator/=(double rhs) noexcept
{
#if RAY_TRACER_HAS_AVX
    data = _mm256_div_pd(data, _mm256_set1_pd(rhs));
#else
    x /= rhs;
    y /= rhs;
    z /= rhs;
#endif
    return *this;
}

#if RAY_TRACER_HAS_AVX
Vector::operator __m256d() noexcept
{
    return data;
}
#endif

Vector::operator const double*() noexcept
{
    return &x;
}

double Vector::magnitudeSquared() const noexcept
{
#if RAY_TRACER_HAS_AVX
    return Vector::dot(data, data);
#else
    return Vector::dot(*this, *this);
#endif
}

double Vector::magnitude() const noexcept
{
    return std::sqrt(magnitudeSquared());
}

Vector Vector::normalize() noexcept
{
#if RAY_TRACER_HAS_AVX
    data = Vector::normalized(data);
#else
    *this = Vector::normalized(*this);
#endif

    return *this;
}

Vector Vector::normalized() const noexcept
{
#if RAY_TRACER_HAS_AVX
    return Vector::normalized(data);
#else
    return Vector::normalized(*this);
#endif
}

double Vector::angleBetween(const Vector& a, const Vector& b) noexcept
{
    double dot = Vector::dot(a, b);
    double mag = std::sqrt(a.magnitudeSquared() * b.magnitudeSquared());

    if (mag > 0.0)
    {
        return std::acos(dot / mag);
    }
    else
    {
        return 0.0;
    }
}

Vector Vector::cross(const Vector& a, const Vector& b) noexcept
{
#if RAY_TRACER_HAS_AVX
    // y, z, x
    const __m256d aA = _mm256_set_pd(0, a.x, a.z, a.y);

    // z, x, y
    const __m256d aB = _mm256_set_pd(0, a.y, a.x, a.z);

    // y, z, x
    const __m256d bA = _mm256_set_pd(0, b.x, b.z, b.y);

    // z, x, y
    const __m256d bB = _mm256_set_pd(0, b.y, b.x, b.z);

    return _mm256_sub_pd(_mm256_mul_pd(aA, bB), _mm256_mul_pd(aB, bA));
#else
    return Vector{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
#endif
}

#if RAY_TRACER_HAS_AVX
double Vector::dot(const __m256d& a, const __m256d& b) noexcept
{
    const Vector mul{_mm256_mul_pd(a, b)};
    return mul.x + mul.y + mul.z;
}
#endif

double Vector::dot(const Vector& a, const Vector& b) noexcept
{
#if RAY_TRACER_HAS_AVX
    return Vector::dot(a.data, b.data);
#else
    return a.x * b.x + a.y * b.y + a.z * b.z;
#endif
}

#if RAY_TRACER_HAS_AVX
__m256d Vector::normalized(const __m256d& a) noexcept
{
    const __m256d dot = _mm256_set1_pd(Vector::dot(a, a));

#if USE_APPX_INV_SQR
    // invRoot = inverse_square(dot)
    const __m256d invRoot = _mm256_rsqrt_pd(dot);

    // result = a * invRoot
    return _mm256_mul_pd(a, invRoot);
#else
    // root = sqrt(dot)
    const __m256d root = _mm256_sqrt_pd(dot);

    // result = a / root
    return _mm256_div_pd(a, root);
#endif
}
#endif

Vector Vector::normalized(const Vector& a) noexcept
{
#if RAY_TRACER_HAS_AVX
    return Vector::normalized(a.data);
#else
    const double mag = a.magnitude();
    if (mag > 0.0)
    {
        return Vector{a.x / mag, a.y / mag, a.z / mag};
    }
    return Vector{};
#endif
}

Vector Vector::normalizedSub(const Vector& lhs, const Vector& rhs) noexcept
{
#if RAY_TRACER_HAS_AVX
    // sub = lhs - rhs
    const __m256d sub = _mm256_sub_pd(lhs.data, rhs.data);

    // result = sub * invRoot
    return Vector::normalized(sub);
#else
    return Vector::normalized(Vector{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z});
#endif
}

// r = incident − 2 * (incident ⋅ normal) * normal
Vector Vector::reflected(const Vector& incident, const Vector& normal) noexcept
{
#if RAY_TRACER_HAS_AVX
    const __m256d dot2 = _mm256_set1_pd(2 * (Vector::dot(incident, normal)));

    return _mm256_sub_pd(incident.data, _mm256_mul_pd(dot2, normal.data));
#else
    const double dot2 = 2.0 * Vector::dot(incident, normal);
    return Vector{
        incident.x - dot2 * normal.x,
        incident.y - dot2 * normal.y,
        incident.z - dot2 * normal.z
    };
#endif
}

Vector Vector::random(RandomGenerator& generator, double magnitude)
{
    return Vector::randomSphere(generator) * generator.value(magnitude);
}

Vector Vector::randomSphere(RandomGenerator& generator, double magnitude)
{
    const double theta = 2 * Utility::pi * generator.value();
    const double phi = std::acos(1.0 - 2.0 * generator.value());

    return {
        std::sin(phi) * std::cos(theta) * magnitude,
        std::sin(phi) * std::sin(theta) * magnitude,
        std::cos(phi) * magnitude
    };
}

Vector operator+(const Vector& lhs, const Vector& rhs) noexcept
{
#if RAY_TRACER_HAS_AVX
    return _mm256_add_pd(lhs.data, rhs.data);
#else
    return Vector{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
#endif
}

Vector operator-(const Vector& vector) noexcept
{
#if RAY_TRACER_HAS_AVX
    return _mm256_sub_pd(_mm256_set1_pd(0.0), vector.data);
#else
    return Vector{-vector.x, -vector.y, -vector.z};
#endif
}

Vector operator-(const Vector& lhs, const Vector& rhs) noexcept
{
#if RAY_TRACER_HAS_AVX
    return _mm256_sub_pd(lhs.data, rhs.data);
#else
    return Vector{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
#endif
}

Vector operator*(const Vector& lhs, const double& rhs) noexcept
{
#if RAY_TRACER_HAS_AVX
    return _mm256_mul_pd(lhs.data, _mm256_set1_pd(rhs));
#else
    return Vector{lhs.x * rhs, lhs.y * rhs, lhs.z * rhs};
#endif
}

Vector operator*(const double& lhs, const Vector& rhs) noexcept
{
#if RAY_TRACER_HAS_AVX
    return _mm256_mul_pd(_mm256_set1_pd(lhs), rhs.data);
#else
    return Vector{lhs * rhs.x, lhs * rhs.y, lhs * rhs.z};
#endif
}

Vector operator/(const Vector& lhs, const double& rhs) noexcept
{
#if RAY_TRACER_HAS_AVX
    return _mm256_div_pd(lhs.data, _mm256_set1_pd(rhs));
#else
    return Vector{lhs.x / rhs, lhs.y / rhs, lhs.z / rhs};
#endif
}
