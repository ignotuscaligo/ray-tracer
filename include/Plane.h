#pragma once

#include "Vector.h"

struct Plane
{
    Plane() = default;
    Plane(const Vector& a, const Vector& b, const Vector& c);
    Plane(const Vector& b, const Vector& c) : Plane(Vector(), b, c) {}

    bool pointAbovePlane(const Vector& point) const;

    Vector normal;
    double dot;
};
