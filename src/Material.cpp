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

    // Primary-bounce-first daughter generation:
    //   daughter index 0 = sampleMode() — deterministic BRDF peak (cosine peak for
    //                      Lambertian, perfect reflection for Mirror/Microfacet)
    //   daughter indices 1..N-1 = sample() — drawn from the BRDF distribution
    //
    // This guarantees every bounce produces at least one photon along the
    // dominant outgoing direction, which is the photon-mapping-correct fix for
    // chrome-sphere starvation: without it, mirror surfaces only ever receive
    // light from the rare cosine-sample that happened to point at them. With
    // primary-mode-first, every diffuse hit produces a photon directly along
    // its normal, which is the maximum-likelihood direction to reach a nearby
    // specular surface and bounce off into the camera cone.
    for (size_t i = startIndex; i < endIndex; ++i)
    {
        // Carry forward all photon state (time, color, anything added later) by copy,
        // then overwrite the fields that the bounce actually changes.
        photonBlock[i] = photonHit.photon;
        photonBlock[i].bounces = photonHit.photon.bounces + 1;

        const bool primary = (i == startIndex);
        BSDFSample s = primary
            ? sampleMode(incident, normal, generator)
            : sample(incident, normal, generator);

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
