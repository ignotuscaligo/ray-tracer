#include "Quaternion.h"

#include <cmath>

Quaternion::Quaternion()
    : x(0.0)
    , y(0.0)
    , z(0.0)
    , w(1.0)
{
}

Quaternion::Quaternion(double ix, double iy, double iz, double iw)
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
    , w(0.0)
{
}

double Quaternion::magnitudeSqr() const
{
    return x * x + y * y + z * z + w * w;
}

double Quaternion::magnitude() const
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
    return conjugate() * (1.0 / magnitudeSqr());
}

Quaternion Quaternion::fromPitchYawRoll(double pitch, double yaw, double roll)
{
    double p = pitch / 2.0;
    double y = yaw / 2.0;
    double r = roll / 2.0;

    double cp = std::cos(p);
    double cy = std::cos(y);
    double cr = std::cos(r);

    double sp = std::sin(p);
    double sy = std::sin(y);
    double sr = std::sin(r);

    return {
        sp * cy * cr + cp * sy * sr,
        cp * sy * cr - sp * cy * sr,
        cp * cy * sr + sp * sy * cr,
        cp * cy * cr - sp * sy * sr
    };
}

Quaternion Quaternion::fromAxisAngle(Vector axis, double angle)
{
    double sa = std::sin(angle / 2.0);

    return {
        axis.x * sa,
        axis.y * sa,
        axis.z * sa,
        std::cos(angle / 2.0)
    };
}

Quaternion operator*(const Quaternion& lhs, const Quaternion& rhs)
{
    return {
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y + lhs.y * rhs.w + lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.w * rhs.z + lhs.z * rhs.w + lhs.x * rhs.y - lhs.y * rhs.x,
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z
    };
}

Vector operator*(const Quaternion& lhs, const Vector& rhs)
{
    Quaternion v{rhs};

    v = (lhs * v) * lhs.conjugate();

    return {
        v.x,
        v.y,
        v.z
    };
}

Quaternion operator*(const Quaternion& lhs, double rhs)
{
    return {
        lhs.x * rhs,
        lhs.y * rhs,
        lhs.z * rhs,
        lhs.w * rhs
    };
}
