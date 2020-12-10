#pragma once

#include "Vector.h"

struct Hit
{
    Vector position;
    Vector normal;
    float distance = 0; // Distance from ray origin to hit position

    Hit() = default;
};
