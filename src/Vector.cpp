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

const Vector Vector::unitX{1, 0, 0};
const Vector Vector::unitY{0, 1, 0};
const Vector Vector::unitZ{0, 0, 1};

Vector::Vector()
    : x(0)
    , y(0)
    , z(0)
    , _w(0)
{
}

Vector::Vector(double ix, double iy, double iz)
    : x(ix)
    , y(iy)
    , z(iz)
    , _w(0)
{
}

Vector::Vector(__m256d&& idata)
    : data(idata)
    , _w(0)
{
}

double Vector::getAxis(Axis axis) const
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

double Vector::operator[](Axis axis) const
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
    data = _mm256_add_pd(data, rhs.data);
    return *this;
}

Vector Vector::operator*=(double rhs)
{
    data = _mm256_mul_pd(data, _mm256_set1_pd(rhs));
    return *this;
}

Vector Vector::operator/=(double rhs)
{
    data = _mm256_div_pd(data, _mm256_set1_pd(rhs));
    return *this;
}

Vector::operator __m256d()
{
    return data;
}

Vector::operator const double*()
{
    return &x;
}

double Vector::magnitudeSquared() const
{
    return Vector::dot(data, data);
}

double Vector::magnitude() const
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
    __m256d aA = _mm256_set_pd(0, a.x, a.z, a.y);

    // z, x, y
    __m256d aB = _mm256_set_pd(0, a.y, a.x, a.z);

    // y, z, x
    __m256d bA = _mm256_set_pd(0, b.x, b.z, b.y);

    // z, x, y
    __m256d bB = _mm256_set_pd(0, b.y, b.x, b.z);

    return _mm256_sub_pd(_mm256_mul_pd(aA, bB), _mm256_mul_pd(aB, bA));
}

double Vector::dot(const __m256d& a, const __m256d& b)
{
    __m256d mul = _mm256_mul_pd(a, b);

    return mul.m256d_f64[0] + mul.m256d_f64[1] + mul.m256d_f64[2];
}

double Vector::dot(const Vector& a, const Vector& b)
{
    return Vector::dot(a.data, b.data);
}

__m256d Vector::normalized(const __m256d& a)
{
    __m256d dot = _mm256_set1_pd(Vector::dot(a, a));

#if USE_APPX_INV_SQR
    // invRoot = inverse_square(dot)
    __m256d invRoot = _mm256_rsqrt_pd(dot);

    // result = a * invRoot
    return _mm256_mul_pd(a, invRoot);
#else
    // root = sqrt(dot)
    __m256d root = _mm256_sqrt_pd(dot);

    // result = a / root
    return _mm256_div_pd(a, root);
#endif
}

Vector Vector::normalized(const Vector& a)
{
    return Vector::normalized(a.data);
}

Vector Vector::normalizedSub(const Vector& lhs, const Vector& rhs)
{
    // sub = lhs - rhs
    __m256d sub = _mm256_sub_pd(lhs.data, rhs.data);

    // result = sub * invRoot
    return Vector::normalized(sub);
}

// r = incident − 2 * (incident ⋅ normal) * normal
Vector Vector::reflected(const Vector& incident, const Vector& normal)
{
    __m256d dot2 = _mm256_set1_pd(2 * (Vector::dot(incident, normal)));

    return _mm256_sub_pd(incident.data, _mm256_mul_pd(dot2, normal.data));
}

Vector Vector::random(RandomGenerator& generator, double magnitude)
{
    return Vector::randomSphere(generator) * generator.value(magnitude);
}

Vector Vector::randomSphere(RandomGenerator& generator, double magnitude)
{
    double theta = 2 * Utility::pi * generator.value();
    double phi = std::acos(1.0 - 2.0 * generator.value());

    return {
        std::sin(phi) * std::cos(theta) * magnitude,
        std::sin(phi) * std::sin(theta) * magnitude,
        std::cos(phi) * magnitude
    };
}

Vector operator+(const Vector& lhs, const Vector& rhs)
{
    return _mm256_add_pd(lhs.data, rhs.data);
}

Vector operator-(const Vector& vector)
{
    return _mm256_sub_pd(_mm256_set1_pd(0.0), vector.data);
}

Vector operator-(const Vector& lhs, const Vector& rhs)
{
    return _mm256_sub_pd(lhs.data, rhs.data);
}

Vector operator*(const Vector& lhs, const double& rhs)
{
    return _mm256_mul_pd(lhs.data, _mm256_set1_pd(rhs));
}

Vector operator*(const double& lhs, const Vector& rhs)
{
    return _mm256_mul_pd(_mm256_set1_pd(lhs), rhs.data);
}

Vector operator/(const Vector& lhs, const double& rhs)
{
    return _mm256_div_pd(lhs.data, _mm256_set1_pd(rhs));
}
