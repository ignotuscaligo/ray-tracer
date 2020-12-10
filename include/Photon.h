#pragma once

#include "Color.h"
#include "Ray.h"
#include "Hit.h"

#include <optional>

struct Photon
{
    Photon() = default;

    Ray ray;
    Color color;
};

struct PhotonHit
{
    Photon photon;
    Hit hit;
};
