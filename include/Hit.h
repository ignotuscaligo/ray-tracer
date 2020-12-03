#pragma once

#include "Vector.h"

struct Hit
{
    Vector position;
    Vector normal;
    float distance; // Distance from ray origin to hit position

    Hit() = default;
};
