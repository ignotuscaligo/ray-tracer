#pragma once

#include "Vector.h"

struct Sphere
{
    Sphere() = default;
    Sphere(const Vector& center, double radius);

    bool containsPoint(const Vector& point) const;

    Vector center;
    double radius = 0;
};
