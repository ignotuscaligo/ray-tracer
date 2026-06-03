#include "LightQueue.h"

void LightQueue::registerLight(const std::string& name, size_t count, double luminousFlux)
{
    std::scoped_lock<std::mutex> lock(m_mutex);
    // Count-independent: store the light's total luminous flux Phi directly. Each
    // photon will carry Phi as its weight. No 1/count here — the divide by N is
    // applied once at image-conversion time.
    m_flux.insert_or_assign(name, luminousFlux);
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
