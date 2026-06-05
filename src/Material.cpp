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
                      photonHit.photon.lightId,
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
                                 int parentLightId,
                                 RandomGenerator& generator) const
{
    // SINGLE-PHOTON light tracing. Each bounce scatters EXACTLY ONE outgoing
    // photon (the canonical light-tracing / forward-path-tracing model), so the
    // photon population is constant: only light emission adds photons, every
    // bounce is 1-in-1-out. `totalDaughters` is therefore 1 for the live pipeline.
    //
    // No 1/N energy split: with a single outgoing photon there is no fan-out to
    // average over. The photon carries forward magnitude * BSDF weight, where the
    // weight already encodes f*cos/pdf (the Monte Carlo throughput). The expected
    // outgoing energy of the single stochastic sample equals incoming * albedo,
    // which is exactly energy-conserving — the same expectation the old N-daughter
    // 1/N average produced, just with one sample instead of N.
    //
    // The outgoing direction is ALWAYS drawn from the material's STOCHASTIC
    // importance sample (sample() — cosine-weighted / GGX / delta draw), never the
    // deterministic sampleMode() peak. Using the mode for a single photon biases
    // the estimate (the old "N=1 is biased" bug): the mode is the distribution
    // peak, not a fair draw, so a lone mode photon over-weights the dominant lobe
    // direction and under-represents the rest of the BRDF.
    //
    // (totalDaughters / globalStart are retained in the signature for the lazy
    // emitter plumbing and any legacy multi-sample caller; when totalDaughters > 1
    // this still produces independent stochastic samples, just with NO 1/N split.)
    (void)globalStart;
    (void)totalDaughters;

    for (size_t k = 0; k < count; ++k)
    {
        const size_t slot = blockStart + k;

        // Carry forward parent photon state, then overwrite the fields the bounce
        // actually changes. (The parent ray is fully overwritten below, so only
        // color / time / bounces need carrying.)
        Photon& out = photonBlock[slot];
        out = Photon{};
        out.color = parentColor;
        out.time = parentTime;
        out.bounces = parentBounces + 1;
        out.lightId = parentLightId;

        const BSDFSample s = sample(incident, normal, generator);

        if (!s.valid)
        {
            // Mark output as no-energy so it gets dropped at the next processPhotons pass.
            out.ray = {position, normal};
            out.color = Color{0.0f, 0.0f, 0.0f};
            continue;
        }

        out.ray = {position, s.direction};
        out.color = parentColor * s.weight;
    }
}
