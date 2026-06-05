#pragma once

#include "Color.h"
#include "Ray.h"
#include "Hit.h"

#include <algorithm>
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

// Single-photon DECAY termination predicate. Returns true while the photon should
// keep bouncing, false once it has decayed enough to terminate.
//
// The cutoff is ABSOLUTE: a photon is alive iff its current magnitude exceeds a
// fixed threshold expressed in photon-magnitude units (the same flux/light-count
// units the photon's colour carries):
//   alive  iff  currentMagnitude > terminationThreshold.
// This is a hard brightness floor: below it a bundle is too dim to meaningfully
// affect the image and is dropped, regardless of how bright it was emitted. By
// design this lets a BRIGHTER photon bounce deeper than a dimmer one (it takes
// more bounce-attenuation to fall below the same absolute floor) — the opposite
// of the old relative-to-emission cutoff, and what Elijah explicitly requested.
// NOTE: photon magnitudes are absolute emitted flux / photon-count, so they can
// be large (hundreds+) and are scene-dependent; the bounce cap below is the real
// safety bound on path depth.
inline bool photonDecayAlive(float currentMagnitude,
                             double terminationThreshold) noexcept
{
    return currentMagnitude > static_cast<float>(terminationThreshold);
}

// Convenience overload: the photon's current magnitude is the max colour channel.
inline bool photonDecayAlive(const Photon& photon, double terminationThreshold) noexcept
{
    const float currentMagnitude =
        std::max({photon.color.red, photon.color.green, photon.color.blue});
    return photonDecayAlive(currentMagnitude, terminationThreshold);
}
