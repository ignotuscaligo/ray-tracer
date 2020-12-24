#pragma once

#include "Vector.h"

struct Limits
{
    Limits() = default;
    Limits(double min, double max) noexcept;
    Limits(double value) noexcept;

    bool contains(double value) const noexcept;
    bool intersects(const Limits& other) const noexcept;

    Limits operator=(const Limits& rhs) noexcept;
    Limits operator+=(const Limits& rhs) noexcept;

    double min = 0.0;
    double max = 0.0;
};

struct Bounds
{
    Bounds() = default;
    Bounds(Limits x, Limits y, Limits z) noexcept;
    Bounds(Vector vector) noexcept;
    Bounds(Vector min, Vector max) noexcept;

    void extend(Limits limits, Axis axis) noexcept;
    Limits getLimits(Axis axis) const noexcept;
    Limits operator[](Axis axis) const noexcept;
    bool contains(const Vector& vector) const noexcept;
    bool intersects(const Bounds& other) const noexcept;
    Vector minimum() const noexcept;
    Vector maximum() const noexcept;

    Bounds operator=(const Bounds& rhs) noexcept;
    Bounds operator+=(const Bounds& rhs) noexcept;

    Limits x;
    Limits y;
    Limits z;
};
