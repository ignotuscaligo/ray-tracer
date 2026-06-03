#pragma once

#include "Color.h"
#include "Photon.h"
#include "Vector.h"

#include <cstdint>

// A compact daughter-photon PRODUCER (Wave 3).
//
// Wave 2 eagerly materialized all N daughter photons of a bounce-hit at once via
// Material::bounce(), and the EmittingQueue had to store full PhotonHits (~168
// bytes each) until there was room for the whole N-photon fan-out. With
// Lambertian N=9 (and microfacet/light fan-out on top), that made the daughter
// path the dominant memory + back-pressure cost in the pipeline.
//
// An Emitter replaces that with the lazy producer model the lights already use:
// a light registers a remaining-photon COUNT and generates its next photon(s)
// on demand into reserved queue space (LightQueue + Light::emit). An Emitter is
// the same shape — a producer that carries
//   (1) the minimal state needed to GENERATE a daughter (hit point, surface
//       normal, incoming direction, parent photon color/time/bounce-depth, and
//       a material reference), and
//   (2) a remaining-daughter count.
// It does NOT store the materialized daughters. A worker, once it has RESERVED
// space in the photon queue (claim-output-first — Wave 1's losslessness), pulls
// an emitter and generates as many daughters as fit into the reserved space,
// advancing m_generated; if daughters remain the emitter goes back in the queue,
// and when m_generated == m_total it is done.
//
// Equivalence with Wave 2's eager fan-out is exact-by-construction (see
// Material::generateDaughters): the daughter at global index i is produced from
// the same sampleMode()/sample() call (index 0 = BRDF mode, 1..N-1 = sampled),
// carries the same 1/N energy split (invN keyed on m_total, NOT on how the N
// were chunked across pulls), and consumes the worker RNG in the same per-index
// order. Splitting one emitter's N daughters across multiple pulls only changes
// which RNG draws interleave with OTHER emitters — a Monte-Carlo reshuffle, not
// a change in the expected image. So the render matches Wave 2 within noise.
struct Emitter
{
    Emitter() = default;

    // Build a compact emitter from a fully-resolved bounce-hit. `totalDaughters`
    // is the material's daughterPhotonCount() for this hit, captured here so the
    // energy split (1/N) and the index-0-is-BRDF-mode rule stay identical no
    // matter how the N daughters are chunked across reserved-space pulls.
    Emitter(const PhotonHit& photonHit, std::uint32_t totalDaughters)
        : incident(photonHit.photon.ray.direction)
        , normal(photonHit.hit.normal)
        , position(photonHit.hit.position)
        , color(photonHit.photon.color)
        , time(photonHit.photon.time)
        , bounces(photonHit.photon.bounces)
        , material(photonHit.hit.material)
        , m_total(totalDaughters)
        , m_generated(0)
    {
    }

    // Incoming photon travel direction (into the surface).
    Vector incident;
    // Outward surface normal at the hit.
    Vector normal;
    // World-space hit point — the origin of every daughter ray.
    Vector position;
    // Parent photon color (pre-split). Daughters carry color * weight * (1/total).
    Color color;
    // Parent photon emission timestamp, carried forward to every daughter.
    float time = 0.0f;
    // Parent bounce depth; daughters are stamped bounces + 1.
    int bounces = 0;
    // Material-library index used to generate daughters and to recover albedo.
    size_t material = 0;

    // Total daughters this emitter will ever produce (the 1/N denominator and
    // the global daughter index basis).
    std::uint32_t total() const { return m_total; }
    // Daughters produced so far across all pulls.
    std::uint32_t generated() const { return m_generated; }
    // Daughters still to produce.
    std::uint32_t remaining() const { return m_total - m_generated; }
    bool done() const { return m_generated >= m_total; }

    // Advance the generated cursor after a pull produced `count` daughters.
    void advance(std::uint32_t count) { m_generated += count; }

private:
    std::uint32_t m_total = 0;
    std::uint32_t m_generated = 0;
};
