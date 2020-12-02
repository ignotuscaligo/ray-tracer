#pragma once

#include "Vector.h"

struct Hit
{
    Vector incident; // Direction from incoming ray
    Vector position;
    Vector normal;
    float distance; // Distance from ray origin to hit position
};
