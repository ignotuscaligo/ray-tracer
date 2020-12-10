#include "Quaternion.h"

#include <cmath>

Quaternion::Quaternion()
    : x(0.0f)
    , y(0.0f)
    , z(0.0f)
    , w(1.0f)
{
}

Quaternion::Quaternion(float ix, float iy, float iz, float iw)
    : x(ix)
    , y(iy)
    , z(iz)
    , w(iw)
{
}

Quaternion::Quaternion(Vector vector)
    : x(vector.x)
    , y(vector.y)
    , z(vector.z)
    , w(0.0f)
{
}

float Quaternion::magnitudeSqr() const
{
    return x * x + y * y + z * z + w * w;
}

float Quaternion::magnitude() const
{
    return std::sqrt(magnitudeSqr());
}

Quaternion Quaternion::conjugate() const
{
    return {
        -x,
        -y,
        -z,
        w
    };
}

Quaternion Quaternion::inverse() const
{
    return conjugate() * (1.0f / magnitudeSqr());
}

Quaternion Quaternion::fromPitchYawRoll(float pitch, float yaw, float roll)
{
    float p = pitch / 2.0f;
    float y = yaw / 2.0f;
    float r = roll / 2.0f;

    float cp = std::cos(p);
    float cy = std::cos(y);
    float cr = std::cos(r);

    float sp = std::sin(p);
    float sy = std::sin(y);
    float sr = std::sin(r);

    return {
        sp * cy * cr + cp * sy * sr,
        cp * sy * cr - sp * cy * sr,
        cp * cy * sr + sp * sy * cr,
        cp * cy * cr - sp * sy * sr
    };
}

Quaternion Quaternion::fromAxisAngle(Vector axis, float angle)
{
    float sa = std::sin(angle / 2.0f);

    return {
        axis.x * sa,
        axis.y * sa,
        axis.z * sa,
        std::cos(angle / 2.0f)
    };
}

Quaternion operator*(const Quaternion& lhs, const Quaternion& rhs)
{
    return {
        lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y + lhs.w * rhs.x,
        -lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x + lhs.w * rhs.y,
        lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w + lhs.w * rhs.z,
        -lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z + lhs.w * rhs.w
    };
}

Vector operator*(const Quaternion& lhs, const Vector& rhs)
{
    Quaternion v{rhs};

    v = lhs * v * lhs.conjugate();

    return {
        v.x,
        v.y,
        v.z
    };
}

Quaternion operator*(const Quaternion& lhs, float rhs)
{
    return {
        lhs.x * rhs,
        lhs.y * rhs,
        lhs.z * rhs,
        lhs.w * rhs
    };
}
