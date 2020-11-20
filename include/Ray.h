#pragma once

#include "Point.h"
#include "Bounds.h"
#include "Triangle.h"

struct Ray
{
    Point origin;
    Point direction;

    Ray() = default;
    Ray(Point iorigin, Point idirection);
};

bool rayIntersectsBounds(const Ray& ray, const Bounds& bounds);
bool rayIntersectsTriangle(const Ray& ray, const Triangle& triangle);
