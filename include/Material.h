#pragma once

#include "Color.h"
#include "Photon.h"
#include "RandomGenerator.h"
#include "Vector.h"
#include "WorkQueue.h"

#include <string>

// A single Monte Carlo BRDF sample.
//
// direction  — outgoing world-space direction sampled from the BRDF (unit length, in the
//              same hemisphere as `normal` for reflective materials).
// weight     — the throughput multiplier to apply to the incoming photon's energy on this
//              bounce. Encodes f(wi,wo) * cos(theta_o) / pdf(wo) for non-delta BRDFs, or
//              just the reflectance for delta BRDFs. Values are bounded in [0,1] for
//              energy-conserving materials.
// pdf        — probability density of the sampled direction (in solid-angle measure). Zero
//              for delta distributions.
// isDelta    — true when the BRDF is a Dirac delta (e.g. a perfect mirror). Camera splats
//              and density-based MIS should skip delta materials because the probability of
//              sampling any specific outgoing direction is zero.
struct BSDFSample
{
    Vector direction{};
    Color weight{1.0f};
    double pdf = 0.0;
    bool isDelta = false;
    bool valid = false;
};

class Material
{
public:
    Material() = default;
    Material(const std::string& name);
    virtual ~Material() = default;

    std::string name() const;

    // ===== BRDF interface (the canonical surface contract) =====

    // Draw a Monte Carlo sample of an outgoing direction for an incoming photon. `incident`
    // points along the incoming photon's travel direction (into the surface). `normal` is
    // the outward surface normal.
    virtual BSDFSample sample(const Vector& incident, const Vector& normal, RandomGenerator& generator) const = 0;

    // Evaluate the BRDF f(wi, wo) for a specific pair of directions. Both `wi` and `wo`
    // point away from the surface in this convention (wi is the direction the incoming
    // photon came FROM, wo is the direction energy is leaving toward — e.g. the camera).
    // Returns the BRDF value per channel (not multiplied by cos theta).
    virtual Color evaluate(const Vector& wi, const Vector& wo, const Vector& normal) const = 0;

    // Probability density (solid-angle measure) of sampling `wo` given `wi`. Returns 0 for
    // delta distributions.
    virtual double pdf(const Vector& wi, const Vector& wo, const Vector& normal) const = 0;

    virtual bool isDelta() const { return false; }

    // Number of daughter photons to spawn per surface hit (per-material fan-out).
    // Delta materials (mirror) return 1 — there is only one valid outgoing direction.
    // Diffuse / wide-lobe BRDFs (Lambertian) return a large value to populate the
    // hemisphere; narrow-lobe BRDFs (rough microfacet) scale by lobe width (roughness).
    //
    // The Worker's processPhotons stage queries this per-hit and allocates N daughter
    // slots, then calls bounce() with endIndex = startIndex + N. Energy is split 1/N
    // across the daughters inside bounce() — total outgoing energy per hit stays
    // equal to incoming_energy * material_albedo (no double-counting).
    virtual size_t daughterPhotonCount() const = 0;

    // ===== Pipeline integration =====

    // Default implementation that draws a sample per output slot and applies the throughput
    // weight. Subclasses can override for batched optimization but don't have to.
    virtual void bounce(WorkQueue<Photon>::Block photonBlock, size_t startIndex, size_t endIndex, const PhotonHit& photonHit, RandomGenerator& generator) const;

    // Default implementation evaluates the BRDF against the camera direction and modulates
    // by the photon's color. Delta materials return black (a delta bounce has zero
    // probability of landing exactly on the camera, so direct splat contribution is zero).
    virtual Color colorForHit(const Vector& pixelDirection, const PhotonHit& photonHit) const;

private:
    std::string m_name;
};
