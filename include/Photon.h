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
    int bounces = 0;
    // Wave 6: index of the light this photon originated from (set at emission by
    // the Light's index in the scene light list; inherited by every daughter so
    // each bounce deposit can be attributed back to its source light). -1 until a
    // light stamps it. Carried into the BounceRecord at deposit time.
    int lightId = -1;
    // Emission timestamp (seconds, relative to some shared epoch). Used downstream by the
    // camera's exposure-window gate to support motion blur and rolling-shutter effects
    // (vision doc pillars 2 + 3). Defaults to 0 — every photon emitted "now" — until a
    // light samples its emission time within an exposure window.
    float time = 0.0f;
};

struct PhotonHit
{
    Photon photon;
    Hit hit;
};
