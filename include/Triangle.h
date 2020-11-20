#pragma once

#include "Bounds.h"
#include "Point.h"

struct Triangle
{
    Point a;
    Point b;
    Point c;
    Point center;
    Point normal;

    Triangle() = default;
    Triangle(Point ia, Point ib, Point ic);

    Limits getLimits(Axis axis) const;
    Bounds getBounds() const;
};

static Triangle operator+(const Triangle& lhs, const Point& rhs)
{
	return Triangle(
		lhs.a + rhs,
		lhs.b + rhs,
		lhs.c + rhs
	);
}
