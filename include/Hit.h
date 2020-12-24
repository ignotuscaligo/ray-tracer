#pragma once

#include "Vector.h"

struct Hit
{
    Hit() = default;
    Hit(const Hit& hit) noexcept;

    Hit operator=(const Hit& rhs) noexcept;

    Vector position;
    Vector normal;
    double distance = 0; // Distance from ray origin to hit position
    size_t material = 0;
};
