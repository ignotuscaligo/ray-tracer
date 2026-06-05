#include "LightQueue.h"

void LightQueue::registerLight(const std::string& name, size_t count, double luminousFlux)
{
    std::scoped_lock<std::mutex> lock(m_mutex);
    // Single-photon model: BAKE the per-photon magnitude = Phi / N at emission, so
    // the carried weight already encodes the photon-count normalization. The gather
    // is then a PURE ADDITIVE SUM with no 1/N divide at lookup time — emission
    // distributes the light's total flux Phi across its N photons, and summing all
    // deposits reconstructs Phi (modulated by transport) directly.
    //
    // This is the count-equivalence guarantee: 100 photons each carrying Phi/100
    // and 10 photons each carrying Phi/10 deposit the same expected total energy,
    // so doubling N halves per-photon magnitude and the image brightness is
    // unchanged (only noise drops). A zero count would divide by zero; guard it.
    const double perPhotonFlux = (count > 0) ? (luminousFlux / static_cast<double>(count)) : 0.0;
    m_flux.insert_or_assign(name, perPhotonFlux);
    m_photons.insert_or_assign(name, count);
    m_remaining.fetch_add(count);
}

size_t LightQueue::remainingPhotons() const
{
    return m_remaining.load();
}

size_t LightQueue::fetchPhotons(const std::string& name, size_t count)
{
    if (m_photons.count(name) == 0)
    {
        return 0;
    }

    size_t fetched = 0;

    {
        std::scoped_lock<std::mutex> lock(m_mutex);

        size_t remaining = m_photons[name];
        fetched = std::min(count, remaining);
        remaining -= fetched;
        m_photons[name] = remaining;
        m_remaining.fetch_sub(fetched);
    }

    return fetched;
}

double LightQueue::getPhotonFlux(const std::string& name) const
{
    std::scoped_lock<std::mutex> lock(m_mutex);
    if (m_flux.count(name) == 0)
    {
        return 0.0;
    }

    return m_flux.at(name);
}
