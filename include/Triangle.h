#pragma once

#include "Bounds.h"
#include "Vector.h"

struct Triangle
{
    Vector a;
    Vector b;
    Vector c;
    Vector center;
    Vector normal;

    Vector aNormal;
    Vector bNormal;
    Vector cNormal;

    Triangle() = default;
    Triangle(Vector ia, Vector ib, Vector ic) noexcept;

    Limits getLimits(Axis axis) const noexcept;
    Bounds getBounds() const noexcept;
    Vector getPosition(const Vector& coords) const noexcept;
    Vector getNormal(const Vector& coords) const noexcept;
};

static Triangle operator+(const Triangle& lhs, const Vector& rhs) noexcept;
