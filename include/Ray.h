#pragma once

#include "Vector.h"
#include "Bounds.h"
#include "Triangle.h"

struct Ray
{
    Vector origin;
    Vector direction;

    Ray() = default;
    Ray(Vector iorigin, Vector idirection);
};

bool rayIntersectsBounds(const Ray& ray, const Bounds& bounds);
bool rayIntersectsTriangle(const Ray& ray, const Triangle& triangle);
