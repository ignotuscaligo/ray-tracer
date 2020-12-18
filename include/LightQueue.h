#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

class LightQueue
{
public:
    void setPhotonCount(const std::string& name, size_t count);
    size_t remainingPhotons() const;
    size_t fetchPhotons(const std::string& name, size_t count);
    float getPhotonBrightness(const std::string& name) const;

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, size_t> m_photons;
    std::unordered_map<std::string, float> m_brightness;
    std::atomic_uint32_t m_remaining;
};
