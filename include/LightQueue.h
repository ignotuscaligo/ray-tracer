#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

// Tracks how many photons remain to be emitted per light, plus the per-photon
// physical flux weight each light's photons carry.
//
// Wave 2 (physical units): the per-photon weight is now COUNT-INDEPENDENT. It is
// the light's total luminous flux Phi (lumens). Each emitted photon carries Phi
// as its weight; the Buffer sums these raw with NO division by photon count.
// The single 1/N normalization (where N = photons emitted per light) is applied
// once, at image-conversion time, in the Renderer. This is what makes buffer
// accumulation count-agnostic: two half-budget runs summed equal one full run,
// because no per-photon magnitude depends on the total count.
//
// (Previously this stored 1/count and baked the normalization into every photon
// at emission, which made the buffer scale-coupled to the photon budget.)
class LightQueue
{
public:
    // Register a light with its total emission count (N) and its total luminous
    // flux Phi (lumens). The per-photon carried weight is Phi (count-independent).
    void registerLight(const std::string& name, size_t count, double luminousFlux);

    size_t remainingPhotons() const;
    size_t fetchPhotons(const std::string& name, size_t count);

    // Count-independent per-photon flux weight (lumens) for the named light.
    double getPhotonFlux(const std::string& name) const;

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, size_t> m_photons;
    std::unordered_map<std::string, double> m_flux;
    std::atomic_uint32_t m_remaining;
};
