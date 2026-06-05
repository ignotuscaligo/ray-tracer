#pragma once

#include "Vector.h"

struct Plane
{
    Plane() = default;
    Plane(const Vector& a, const Vector& b, const Vector& c);
    Plane(const Vector& b, const Vector& c) : Plane(Vector(), b, c) {}

    bool pointAbovePlane(const Vector& point) const;

    Vector normal;
    // Default member initializer: a default-constructed Plane otherwise left
    // `dot` indeterminate (cppcheck uninitMemberVar). It is read in
    // rayIntersectsPlane (Ray.cpp) as `plane.dot - ...`, so an uninitialized
    // value would be a latent UB hazard for any default-constructed Plane.
    double dot = 0.0;
};
