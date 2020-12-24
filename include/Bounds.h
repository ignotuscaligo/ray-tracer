#pragma once

#include "Vector.h"

struct Limits
{
    double min;
    double max;

    Limits();
    Limits(double imin, double imax);
    Limits(double value);

    bool contains(double value) const;
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
    Bounds(Vector min, Vector max);

    void extend(Limits limits, Axis axis);
    Limits getLimits(Axis axis) const;
    Limits operator[](Axis axis) const;
    bool contains(const Vector& vector) const;
    bool intersects(const Bounds& other) const;
    Vector minimum() const;
    Vector maximum() const;

    Bounds operator=(const Bounds& rhs);
    Bounds operator+=(const Bounds& rhs);
};