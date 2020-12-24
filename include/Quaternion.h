#pragma once

#include "Vector.h"

struct Quaternion
{
    double x;
    double y;
    double z;
    double w;

    Quaternion();
    Quaternion(double ix, double iy, double iz, double iw);
    Quaternion(Vector vector);

    double magnitudeSqr() const;
    double magnitude() const;
    Quaternion conjugate() const;
    Quaternion inverse() const;

    static Quaternion fromPitchYawRoll(double pitch, double yaw, double roll);
    static Quaternion fromAxisAngle(Vector axis, double angle);
};

Quaternion operator*(const Quaternion& lhs, const Quaternion& rhs);
Vector operator*(const Quaternion& lhs, const Vector& rhs);
Quaternion operator*(const Quaternion& lhs, double rhs);
