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
    // Single-photon decay termination: the photon's magnitude (max colour channel)
    // at EMISSION, stamped once when a light creates the photon and carried forward
    // unchanged through every bounce. A photon is terminated when its CURRENT
    // magnitude falls below terminationFraction * initialMagnitude — a cutoff
    // RELATIVE to emission, so it is scale-invariant: the "100 photons x 1.0 =
    // 10 photons x 10.0" emission equivalence holds (an absolute cutoff would
    // trace bright photons deeper and break that). 0 until a light stamps it.
    float initialMagnitude = 0.0f;
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
// The cutoff is RELATIVE to the photon's emission magnitude:
//   alive  iff  currentMagnitude > terminationFraction * initialMagnitude.
// Because both sides scale linearly with emission magnitude, a photon emitted 10x
// brighter is traced to the same number of bounces as a 10x-dimmer one — the
// "100 photons x 1.0 == 10 photons x 10.0" emission equivalence the single-photon
// model relies on. An ABSOLUTE cutoff would trace bright photons deeper and break
// it. A non-positive initialMagnitude (e.g. an un-stamped synthetic photon) yields
// a zero cutoff, so decay never terminates it (only the bounce cap governs).
inline bool photonDecayAlive(float currentMagnitude,
                             float initialMagnitude,
                             double terminationFraction) noexcept
{
    const float cutoff = (initialMagnitude > 0.0f)
        ? static_cast<float>(terminationFraction) * initialMagnitude
        : 0.0f;
    return currentMagnitude > cutoff;
}

// Convenience overload: the photon's current magnitude is the max colour channel.
inline bool photonDecayAlive(const Photon& photon, double terminationFraction) noexcept
{
    const float currentMagnitude =
        std::max({photon.color.red, photon.color.green, photon.color.blue});
    return photonDecayAlive(currentMagnitude, photon.initialMagnitude, terminationFraction);
}
