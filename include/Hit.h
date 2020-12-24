#pragma once

#include "Vector.h"

struct Hit
{
    Vector position;
    Vector normal;
    double distance = 0; // Distance from ray origin to hit position
    size_t material = 0;

    Hit() = default;
};
