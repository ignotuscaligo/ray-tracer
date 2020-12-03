#pragma once

#include "Bounds.h"
#include "Hit.h"
#include "Triangle.h"
#include "Vector.h"

#include <optional>

struct Ray
{
    Vector origin;
    Vector direction;

    Ray() = default;
    Ray(Vector iorigin, Vector idirection);
};

bool rayIntersectsBounds(const Ray& ray, const Bounds& bounds);

std::optional<Hit> rayIntersectsTriangle(const Ray& ray, const Triangle& triangle);
