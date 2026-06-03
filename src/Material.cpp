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
    // Eager fan-out is now just "generate the whole daughter set at once": global
    // indices [0, N) written contiguously into [startIndex, endIndex). This keeps
    // the Wave 2 behaviour identical while routing through the same primitive the
    // Wave 3 lazy emitter uses, so the eager and lazy paths cannot diverge.
    const size_t n = (endIndex > startIndex) ? (endIndex - startIndex) : 1;
    generateDaughters(photonBlock,
                      /*blockStart=*/startIndex,
                      /*globalStart=*/0,
                      /*count=*/n,
                      /*totalDaughters=*/n,
                      photonHit.photon.ray.direction,
                      photonHit.hit.normal,
                      photonHit.hit.position,
                      photonHit.photon.color,
                      photonHit.photon.time,
                      photonHit.photon.bounces,
                      generator);
}

void Material::generateDaughters(WorkQueue<Photon>::Block photonBlock,
                                 size_t blockStart,
                                 size_t globalStart,
                                 size_t count,
                                 size_t totalDaughters,
                                 const Vector& incident,
                                 const Vector& normal,
                                 const Vector& position,
                                 const Color& parentColor,
                                 float parentTime,
                                 int parentBounces,
                                 RandomGenerator& generator) const
{
    // Energy split across the FULL daughter set N (= totalDaughters), not the
    // size of this chunk. Each sample()'s `weight` already encodes f*cos/pdf —
    // the standard Monte Carlo throughput — so averaging across N independent
    // samples (the 1/N factor) gives the correct expected outgoing energy =
    // incoming * albedo. Keying invN on the chunk size would inflate energy when
    // the fan-out is split across multiple lazy pulls; keying on the total keeps
    // it identical to the eager single-shot fan-out.
    const size_t n = (totalDaughters > 0) ? totalDaughters : 1;
    const float invN = 1.0f / static_cast<float>(n);

    // Primary-bounce-first daughter generation, keyed on the GLOBAL daughter
    // index so a daughter is produced from the same call regardless of how the N
    // are chunked across lazy pulls:
    //   global index 0       = sampleMode() — deterministic BRDF peak (cosine peak
    //                          for Lambertian, perfect reflection for Mirror/Microfacet)
    //   global indices 1..N-1 = sample() — drawn from the BRDF distribution
    //
    // This guarantees every bounce produces at least one photon along the
    // dominant outgoing direction, which is the photon-mapping-correct fix for
    // chrome-sphere starvation: without it, mirror surfaces only ever receive
    // light from the rare cosine-sample that happened to point at them. With
    // primary-mode-first, every diffuse hit produces a photon directly along
    // its normal, which is the maximum-likelihood direction to reach a nearby
    // specular surface and bounce off into the camera cone.
    for (size_t k = 0; k < count; ++k)
    {
        const size_t globalIndex = globalStart + k;
        const size_t slot = blockStart + k;

        // Carry forward parent photon state, then overwrite the fields the bounce
        // actually changes. (The parent ray is fully overwritten below, so only
        // color / time / bounces need carrying.)
        Photon& out = photonBlock[slot];
        out = Photon{};
        out.color = parentColor;
        out.time = parentTime;
        out.bounces = parentBounces + 1;

        const bool primary = (globalIndex == 0);
        BSDFSample s = primary
            ? sampleMode(incident, normal, generator)
            : sample(incident, normal, generator);

        if (!s.valid)
        {
            // Mark output as no-energy so it gets dropped at the next processPhotons pass.
            out.ray = {position, normal};
            out.color = Color{0.0f, 0.0f, 0.0f};
            continue;
        }

        out.ray = {position, s.direction};
        out.color = parentColor * s.weight * invN;
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
