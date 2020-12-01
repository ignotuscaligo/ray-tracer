#pragma once

#include "Vector.h"
#include "Quaternion.h"

struct Transform
{
    Vector position;
    Quaternion rotation;
    Vector scale;

    Vector forward() const;
};
