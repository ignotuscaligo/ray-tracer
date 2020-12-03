#pragma once

#include "Color.h"
#include "Ray.h"

struct Photon
{
    Ray ray;
    Color color;

    Photon() = default;
};
