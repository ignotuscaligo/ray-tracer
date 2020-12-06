#pragma once

#include "Color.h"
#include "Ray.h"
#include "Hit.h"

#include <optional>

struct Photon
{
    Ray ray;
    Color color;
    int x;
    int y;

    Photon() = default;
};

struct PhotonHit
{
    Photon photon;
    Hit hit;
};
