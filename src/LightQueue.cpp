#include "LightQueue.h"

void LightQueue::setPhotonCount(const std::string& name, size_t count)
{
    std::scoped_lock<std::mutex> lock(m_mutex);
    m_brightness.insert_or_assign(name, 1.0f / static_cast<float>(count));
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

float LightQueue::getPhotonBrightness(const std::string& name) const
{
    std::scoped_lock<std::mutex> lock(m_mutex);
    if (m_brightness.count(name) == 0)
    {
        return 0.0f;
    }

    return m_brightness.at(name);
}
