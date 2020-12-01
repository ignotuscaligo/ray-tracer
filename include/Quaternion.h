#pragma once

#include "Vector.h"

struct Quaternion
{
    float x;
    float y;
    float z;
    float w;

    Quaternion();
    Quaternion(float ix, float iy, float iz, float iw);
    Quaternion(Vector vector);

    Quaternion conjugate() const;

    static Quaternion fromPitchYawRoll(float pitch, float yaw, float roll);
    static Quaternion fromAxisAngle(Vector axis, float angle);
};

Quaternion operator*(const Quaternion& lhs, const Quaternion& rhs);
Vector operator*(const Quaternion& lhs, const Vector& rhs);
