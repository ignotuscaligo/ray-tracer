#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

// Tracks how many photons remain to be emitted per light, plus the per-photon
// physical flux weight each light's photons carry.
//
// Single-photon model: the per-photon weight is the light's total luminous flux
// DIVIDED BY its emission count, Phi / N (lumens per photon). The 1/N
// normalization is BAKED at emission so the downstream gather is a pure additive
// sum — no 1/N divide at lookup. Summing all N photons' deposits reconstructs the
// light's flux Phi (modulated by transport). This keeps brightness count-agnostic:
// raising N lowers each photon's magnitude proportionally, so the summed image is
// unchanged (only noise drops).
//
// (Earlier this stored Phi directly and deferred the 1/N to image-conversion /
// gather time; the single-photon redesign moves that divide back to emission so
// the additive gather needs no count normalization.)
class LightQueue
{
public:
    // Register a light with its total emission count (N) and its total luminous
    // flux Phi (lumens). The per-photon carried weight stored is Phi / N (the 1/N
    // is baked here so the gather is a pure additive sum).
    void registerLight(const std::string& name, size_t count, double luminousFlux);

    size_t remainingPhotons() const;
    size_t fetchPhotons(const std::string& name, size_t count);

    // Per-photon flux weight (lumens), Phi / N — the emission magnitude each of
    // this light's photons carries.
    double getPhotonFlux(const std::string& name) const;

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, size_t> m_photons;
    std::unordered_map<std::string, double> m_flux;
    std::atomic_uint32_t m_remaining;
};
