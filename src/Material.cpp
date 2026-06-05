#include "Material.h"

#include "Contracts.h"
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

    // The hit normal arrives as a bare Vector from the pipeline; the geometry
    // producers (Ray.cpp / Volume.cpp / EmissiveGather.cpp) all emit it
    // normalized, so wrap it once here as a UnitVector for the typed BSDF
    // interface. alreadyNormalized's contract catches a non-unit normal slipping
    // through in debug/sanitizer builds; it is zero-cost in release.
    const UnitVector unitNormal = UnitVector::alreadyNormalized(normal);

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

        const BSDFSample s = sample(incident, unitNormal, generator);

        // BSDF boundary postcondition. sample() is the public surface contract
        // every Material subclass must honor; these were previously only checked
        // in test_BSDF. Promoting them to an in-code postcondition catches a
        // non-conforming subclass at the live call site (the worker's scatter),
        // not just under the unit test. A valid sample must carry:
        //   - finite, non-negative per-channel weight (the MC throughput f*cos/pdf;
        //     never negative, never NaN/Inf — energy can't go negative or undefined),
        //   - pdf >= 0 (a probability density; 0 iff a delta lobe),
        //   - a finite, unit-length outgoing direction (every dot-product / scatter
        //     downstream assumes the direction is normalized).
        // Inert in release; the math is untouched.
        POSTCONDITION_MSG(std::isfinite(s.weight.red) && std::isfinite(s.weight.green) &&
                              std::isfinite(s.weight.blue),
                          "BSDF sample weight must be finite");
        POSTCONDITION_MSG(s.weight.red >= 0.0f && s.weight.green >= 0.0f && s.weight.blue >= 0.0f,
                          "BSDF sample weight must be non-negative");
        POSTCONDITION_MSG(std::isfinite(s.pdf) && s.pdf >= 0.0, "BSDF sample pdf must be finite and >= 0");
        if (s.valid)
        {
            const double dirLen2 = Vector::dot(s.direction, s.direction);
            POSTCONDITION_MSG(std::isfinite(dirLen2), "BSDF sampled direction must be finite");
            POSTCONDITION_MSG(std::abs(dirLen2 - 1.0) < 1e-3,
                              "BSDF sampled direction must be unit length");
            // NOTE: the "pdf == 0 iff delta" relation (review-2 §4b-3) is a
            // property of the pdf() QUERY method (a delta lobe has measure-zero
            // solid-angle density), NOT of the BSDFSample::pdf field returned by
            // sample(). By this codebase's convention a delta sample reports
            // pdf == 1.0 (the throughput is the bare reflectance; pdf == 1 means
            // "no division"), verified in test_BSDF. So it is deliberately NOT
            // asserted on the sample here — only the general pdf >= 0 above.
        }

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
