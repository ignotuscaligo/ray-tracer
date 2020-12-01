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

    Triangle() = default;
    Triangle(Vector ia, Vector ib, Vector ic);

    Limits getLimits(Axis axis) const;
    Bounds getBounds() const;
};

static Triangle operator+(const Triangle& lhs, const Vector& rhs)
{
	return Triangle(
		lhs.a + rhs,
		lhs.b + rhs,
		lhs.c + rhs
	);
}
