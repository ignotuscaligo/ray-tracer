#include "Material.h"

#include "Utility.h"

#include <algorithm>
#include <cmath>

Material::Material(const std::string& name)
    : m_name(name)
{
}

std::string Material::name() const
{
    return m_name;
}

void Material::bounce(WorkQueue<Photon>::Block photonBlock, size_t startIndex, size_t endIndex, const PhotonHit& photonHit, RandomGenerator& generator) const
{
    const Vector& incident = photonHit.photon.ray.direction;
    const Vector& normal = photonHit.hit.normal;

    // Energy split across N daughters. Each sample()'s `weight` already encodes
    // f*cos/pdf — the standard Monte Carlo throughput — so averaging across N
    // independent samples (the 1/N factor) gives the correct expected outgoing
    // energy = incoming * albedo. Without 1/N, N daughters would carry N*incoming
    // *albedo and inflate the scene's total energy by a factor of N per bounce.
    const size_t n = (endIndex > startIndex) ? (endIndex - startIndex) : 1;
    const float invN = 1.0f / static_cast<float>(n);

    for (size_t i = startIndex; i < endIndex; ++i)
    {
        // Carry forward all photon state (time, color, anything added later) by copy,
        // then overwrite the fields that the bounce actually changes.
        photonBlock[i] = photonHit.photon;
        photonBlock[i].bounces = photonHit.photon.bounces + 1;

        BSDFSample s = sample(incident, normal, generator);

        if (!s.valid)
        {
            // Mark output as no-energy so it gets dropped at the next processPhotons pass.
            photonBlock[i].ray = {photonHit.hit.position, normal};
            photonBlock[i].color = Color{0.0f, 0.0f, 0.0f};
            continue;
        }

        photonBlock[i].ray = {photonHit.hit.position, s.direction};
        photonBlock[i].color = photonHit.photon.color * s.weight * invN;
    }
}

Color Material::colorForHit(const Vector& pixelDirection, const PhotonHit& photonHit) const
{
    if (isDelta())
    {
        // Delta BRDF: probability of any non-delta direction (such as the line to the
        // camera) is zero. Forward photon tracing's camera splat cannot resolve specular
        // contributions; that's a known limitation that bidirectional path tracing or
        // photon-map gathering would address.
        return Color{0.0f, 0.0f, 0.0f};
    }

    // `incident` here is the direction the photon CAME FROM (opposite of travel direction).
    const Vector wi = -photonHit.photon.ray.direction;
    // `wo` is the direction toward the camera (opposite of the pixel-to-hit direction).
    const Vector wo = -pixelDirection;

    const double cosOut = std::max(0.0, Vector::dot(wo, photonHit.hit.normal));
    if (cosOut <= 0.0)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }

    Color f = evaluate(wi, wo, photonHit.hit.normal);
    return photonHit.photon.color * f * static_cast<float>(cosOut);
}
