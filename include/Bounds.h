#pragma once

#include "Vector.h"

struct Limits
{
    float min;
    float max;

    Limits();
    Limits(float imin, float imax);
    Limits(float value);

    bool contains(float value) const;
    bool intersects(const Limits& other) const;

    Limits operator=(const Limits& rhs);
    Limits operator+=(const Limits& rhs);
};

struct Bounds
{
    Limits x;
    Limits y;
    Limits z;

    Bounds();
    Bounds(Limits ix, Limits iy, Limits iz);
    Bounds(Vector vector);

    void extend(Limits limits, Axis axis);
    Limits getLimits(Axis axis) const;
    Limits operator[](Axis axis) const;
    bool contains(const Vector& vector) const;
    bool intersects(const Bounds& other) const;

    Bounds operator=(const Bounds& rhs);
    Bounds operator+=(const Bounds& rhs);
};