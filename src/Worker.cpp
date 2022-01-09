#include "Worker.h"

#include "Color.h"
#include "Light.h"
#include "Pixel.h"
#include "Utility.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <optional>
#include <thread>

namespace
{

constexpr double selfHitThreshold = std::numeric_limits<double>::epsilon();

}

Worker::Worker(size_t index, size_t fetchSize)
    : m_index(index)
    , m_fetchSize(fetchSize)
    , m_running(false)
    , m_suspend(false)
{
}

void Worker::start()
{
    // std::cout << m_index << ": start()" << std::endl;
    if (m_running)
    {
        return;
    }

    m_running = true;
    m_suspend = false;

    m_thread = std::thread([this]() {
        exec();
    });
}

void Worker::suspend()
{
    // std::cout << m_index << ": suspend()" << std::endl;
    m_suspend = true;
}

void Worker::resume()
{
    // std::cout << m_index << ": resume()" << std::endl;
    m_suspend = false;
}

void Worker::stop()
{
    // std::cout << m_index << ": stop()" << std::endl;
    m_running = false;
    m_thread.join();
}

void Worker::exec()
{
    // std::cout << m_index << ": start thread" << std::endl;

    // std::vector<PhotonHit> hits;
    // Pixel workingPixel;

    if (!photonQueue || !hitQueue || !finalHitQueue || !image || !camera || !buffer || !materialLibrary || !lightQueue)
    {
        std::cout << m_index << ": ABORT: missing required references!" << std::endl;
        m_running = false;
    }

    while (m_running)
    {
        if (m_suspend)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (lightQueue->remainingPhotons() > 0 && photonQueue->freeSpace() > m_fetchSize * 16)
        {
            if (!processLights())
            {
                break;
            }
        }

        if (photonQueue->available() > 0)
        {
            if (!processPhotons())
            {
                break;
            }
        }

        if (hitQueue->available() > 0)
        {
            if (!processHits())
            {
                break;
            }
        }

        if (finalHitQueue->available() > 0)
        {
            if (!processFinalHits())
            {
                break;
            }
        }

        std::this_thread::yield();
    }

    // std::cout << m_index << ": end thread" << std::endl;
}

bool Worker::processLights()
{
    auto workStart = std::chrono::system_clock::now();

    for (auto& object : objects)
    {
        if (!object->hasType<Light>())
        {
            continue;
        }

        size_t photonCount = lightQueue->fetchPhotons(object->name(), m_fetchSize);

        if (photonCount == 0)
        {
            continue;
        }

        double photonBrightness = lightQueue->getPhotonBrightness(object->name());

        auto photons = photonQueue->initialize(photonCount);

        std::static_pointer_cast<Light>(object)->emit(photons, photonBrightness, m_generator);

        emitProcessed += photons.size();

        photonQueue->ready(photons);

        break;
    }

    auto workEnd = std::chrono::system_clock::now();
    auto workDuration = std::chrono::duration_cast<std::chrono::microseconds>(workEnd - workStart);
    emitDuration += workDuration.count();

    return true;
}

bool Worker::processPhotons()
{
    // std::cout << m_index << ": processing photons" << std::endl;

    auto workStart = std::chrono::system_clock::now();
    // std::cout << m_index << ": fetching " << m_fetchSize << " photons" << std::endl;
    auto photonsBlock = photonQueue->fetch(m_fetchSize);

    // std::cout << m_index << ": processing " << photonsBlock.size() << " photons" << std::endl;

    m_hitBuffer.clear();

    for (auto& photon : photonsBlock)
    {
        // Skip photons with no brightness left, will drop them from the queue
        if (photon.color.brightness() < std::numeric_limits<double>::epsilon())
        {
            continue;
        }

        m_volumeHitBuffer.clear();

        for (auto& object : objects)
        {
            if (!object->hasType<Volume>())
            {
                continue;
            }

            std::optional<Hit> hit = std::static_pointer_cast<Volume>(object)->castRay(photon.ray, m_castBuffer);

            if (hit)
            {
                m_volumeHitBuffer.push_back({photon, *hit});
            }
        }

        if (!m_volumeHitBuffer.empty())
        {
            double minDistance = std::numeric_limits<double>::max();
            size_t minIndex = 0;
            bool validHit = false;

            for (int i = 0; i < m_volumeHitBuffer.size(); ++i)
            {
                if (m_volumeHitBuffer[i].hit.distance < minDistance && m_volumeHitBuffer[i].hit.distance > selfHitThreshold)
                {
                    validHit = true;
                    minIndex = i;
                    minDistance = m_volumeHitBuffer[i].hit.distance;
                }
            }

            if (validHit)
            {
                m_hitBuffer.push_back(m_volumeHitBuffer[minIndex]);
            }
        }
    }

    if (!m_hitBuffer.empty())
    {
        auto hitsBlock = hitQueue->initialize(m_hitBuffer.size());

        for (size_t i = 0; i < hitsBlock.size(); ++i)
        {
            hitsBlock[i] = m_hitBuffer[i];
        }

        hitQueue->ready(hitsBlock);

        size_t bounceThreshold = 6;
        size_t bouncedPhotonCount = 0;

        for (auto& photonHit : m_hitBuffer)
        {
            if (photonHit.photon.bounces < bounceThreshold)
            {
                ++bouncedPhotonCount;
            }
        }

        if (bouncedPhotonCount > 0)
        {
            size_t bounceCount = 1;
            auto bouncedPhotonsBlock = photonQueue->initialize(bouncedPhotonCount * bounceCount);
            size_t photonIndex = 0;

            if (bouncedPhotonsBlock.size() != bouncedPhotonCount * bounceCount)
            {
                std::cout << m_index << ": photon queue overflow!" << std::endl;
            }

            for (auto& photonHit : m_hitBuffer)
            {
                if (photonHit.photon.bounces < bounceThreshold)
                {
                    std::shared_ptr<Material> material = materialLibrary->fetchByIndex(photonHit.hit.material);

                    size_t startIndex = photonIndex;
                    size_t endIndex = startIndex + bounceCount;
                    material->bounce(bouncedPhotonsBlock, startIndex, endIndex, photonHit, m_generator);

                    photonIndex += bounceCount;
                }
            }

            photonQueue->ready(bouncedPhotonsBlock);
        }
    }

    photonsProcessed += photonsBlock.size();

    photonQueue->release(photonsBlock);

    auto workEnd = std::chrono::system_clock::now();
    auto workDuration = std::chrono::duration_cast<std::chrono::microseconds>(workEnd - workStart);
    photonDuration += workDuration.count();

    // std::cout << m_index << ": finished processing photons, generated " << hits.size() << " hits" << std::endl;

    return true;
}

bool Worker::processHits()
{
    // std::cout << m_index << ": processing hits" << std::endl;
    auto workStart = std::chrono::system_clock::now();

    auto hitsBlock = hitQueue->fetch(m_fetchSize);

    m_hitBuffer.clear();

    Vector cameraPosition = camera->position();
    Vector cameraNormal = camera->forward();

    for (auto& photonHit : hitsBlock)
    {
        std::optional<PixelCoords> coord = camera->coordForPoint(photonHit.hit.position);

        // Not within the camera frustum, skip
        if (!coord)
        {
            continue;
        }

        Vector pixelDirection = camera->pixelDirection(*coord);
        double dot = Vector::dot(pixelDirection, photonHit.hit.normal);

        // Not facing the pixel, skip
        if (dot >= 0.0)
        {
            continue;
        }

        Vector path = cameraPosition - photonHit.hit.position;
        double cameraDistance = path.magnitude();

        if (cameraDistance < selfHitThreshold)
        {
            continue;
        }

        Ray ray{photonHit.hit.position, path / cameraDistance};

        std::optional<Hit> closestHit;

        // Do any objects obscure this hit?
        for (auto& object : objects)
        {
            if (!object->hasType<Volume>())
            {
                continue;
            }

            std::optional<Hit> hit = std::static_pointer_cast<Volume>(object)->castRay(ray, m_castBuffer);

            if (hit)
            {
                if (hit->distance > selfHitThreshold && (!closestHit || hit->distance < closestHit->distance))
                {
                    closestHit = hit;
                }
            }
        }

        // If no object was hit, or the closest hit object is behind the camera, the hit is valid
        if (!closestHit || closestHit->distance > cameraDistance)
        {
            m_hitBuffer.push_back(photonHit);
        }
    }

    if (!m_hitBuffer.empty())
    {
        auto validHitsBlock = finalHitQueue->initialize(m_hitBuffer.size());

        for (size_t i = 0; i < validHitsBlock.size(); ++i)
        {
            validHitsBlock[i] = m_hitBuffer[i];
        }

        finalHitQueue->ready(validHitsBlock);
    }

    hitsProcessed += hitsBlock.size();

    hitQueue->release(hitsBlock);

    auto workEnd = std::chrono::system_clock::now();
    auto workDuration = std::chrono::duration_cast<std::chrono::microseconds>(workEnd - workStart);
    hitDuration += workDuration.count();

    return true;
}

bool Worker::processFinalHits()
{
    auto workStart = std::chrono::system_clock::now();
    auto hitsBlock = finalHitQueue->fetch(m_fetchSize);

    for (auto& photonHit : hitsBlock)
    {
        std::optional<PixelCoords> coord = camera->coordForPoint(photonHit.hit.position);

        if (!coord)
        {
            continue;
        }

        Vector pixelDirection = camera->pixelDirection(*coord);

        std::shared_ptr<Material> material = materialLibrary->fetchByIndex(photonHit.hit.material);

        if (material)
        {
            buffer->addColor(*coord, material->colorForHit(pixelDirection, photonHit));
        }
    }

    finalHitsProcessed += hitsBlock.size();

    finalHitQueue->release(hitsBlock);

    auto workEnd = std::chrono::system_clock::now();
    auto workDuration = std::chrono::duration_cast<std::chrono::microseconds>(workEnd - workStart);
    writeDuration += workDuration.count();

    return true;
}
