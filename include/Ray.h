#pragma once

#include "Bounds.h"
#include "Hit.h"
#include "Triangle.h"
#include "Plane.h"
#include "Vector.h"

#include <optional>

struct Ray
{
    Vector origin;
    Vector direction;

    Ray() = default;
    Ray(Vector iorigin, Vector idirection) noexcept;
};

bool rayIntersectsBounds(const Ray& ray, const Bounds& bounds) noexcept;

std::optional<Hit> rayIntersectsTriangle(const Ray& ray, const Triangle& triangle) noexcept;
std::optional<Hit> rayIntersectsPlane(const Ray& ray, const Plane& plane) noexcept;
