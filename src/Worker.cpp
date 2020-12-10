#include "Worker.h"

#include "Color.h"
#include "Pixel.h"

#include <chrono>
#include <iostream>
#include <optional>
#include <thread>

Worker::Worker(size_t index, size_t fetchSize, size_t startPixel, size_t endPixel)
    : m_index(index)
    , m_fetchSize(fetchSize)
    , m_startPixel(startPixel)
    , m_endPixel(endPixel)
    , m_running(false)
    , m_suspend(false)
    , m_writePixels(false)
    , m_writeComplete(false)
{
}

void Worker::start()
{
    // std::cout << m_index << ": start()" << std::endl;
    m_running = true;
    m_suspend = false;
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
}

void Worker::startWrite(std::shared_ptr<Tree<PhotonHit>> tree)
{
    // std::cout << m_index << ": startWrite(...)" << std::endl;

    if (!tree)
    {
        std::cout << m_index << ": ERROR: tree missing when writePixels() was called" << std::endl;
        return;
    }

    finalTree = tree;
    m_writeComplete = false;
    m_writePixels = true;
}

bool Worker::writeComplete() const
{
    return m_writeComplete.load();
}

void Worker::exec()
{
    // std::cout << m_index << ": start thread" << std::endl;

    std::vector<PhotonHit> hits;
    Pixel workingPixel;

    if (!photonQueue || !hitQueue || !pixelSensors || !image)
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

        if (m_writePixels && !m_writeComplete)
        {
            // std::cout << m_index << ": writing pixels " << m_startPixel << " to " << m_endPixel << std::endl;

            if (!finalTree)
            {
                std::cout << m_index << ": ABORT: finalTree missing!" << std::endl;
                break;
            }

            for (size_t i = m_startPixel; i < m_endPixel; ++i)
            {
                PixelSensor& sensor = pixelSensors->at(i);

                Color color{};

                std::vector<PhotonHit> hits = finalTree->fetchWithinPyramid(sensor.pyramid);

                for (auto& photonHit : hits)
                {
                    float dot = Vector::dot(-sensor.pyramid.direction, photonHit.hit.normal);

                    if (dot > 0)
                    {
                        color += photonHit.photon.color;
                    }
                }

                workingPixel.red = std::min(static_cast<int>(color.red * 255), 255);
                workingPixel.green = std::min(static_cast<int>(color.green * 255), 255);
                workingPixel.blue = std::min(static_cast<int>(color.blue * 255), 255);
                image->setPixel(sensor.x, sensor.y, workingPixel);
            }

            m_writePixels = false;
            finalTree = nullptr;
            m_writeComplete = true;
        }
        else if (photonQueue->available() > 0)
        {
            auto workStart = std::chrono::system_clock::now();
            // std::cout << m_index << ": fetching " << m_fetchSize << " photons" << std::endl;
            auto photons = photonQueue->fetch(m_fetchSize);

            // std::cout << m_index << ": processing " << photons.size() << " photons" << std::endl;

            size_t hitsGenerated = 0;

            hits.clear();

            for (auto& photon : photons)
            {
                std::vector<PhotonHit> hitResults;

                for (auto& object : objects)
                {
                    if (!object->hasType<Volume>())
                    {
                        continue;
                    }

                    auto castStart = std::chrono::system_clock::now();
                    std::optional<Hit> hit = std::static_pointer_cast<Volume>(object)->castRay(photon.ray);
                    auto castEnd = std::chrono::system_clock::now();
                    castDuration += std::chrono::duration_cast<std::chrono::microseconds>(castEnd - castStart).count();

                    if (hit)
                    {
                        auto pushHitStart = std::chrono::system_clock::now();
                        hitResults.push_back({photon, *hit});
                        auto pushHitEnd = std::chrono::system_clock::now();
                        pushHitDuration += std::chrono::duration_cast<std::chrono::microseconds>(pushHitEnd - pushHitStart).count();
                    }
                }

                if (!hitResults.empty())
                {
                    float minDistance = std::numeric_limits<float>::max();
                    size_t minIndex = 0;

                    for (int i = 0; i < hitResults.size(); ++i)
                    {
                        if (hitResults[i].hit.distance < minDistance)
                        {
                            minIndex = i;
                            minDistance = hitResults[i].hit.distance;
                        }
                    }

                    hits.push_back(hitResults[minIndex]);
                    ++hitsGenerated;
                }
            }

            if (!hits.empty())
            {
                auto hitsBlock = hitQueue->initialize(hits.size());

                for (size_t i = 0; i < hitsBlock.size(); ++i)
                {
                    hitsBlock[i] = hits[i];
                }

                hitQueue->ready(hitsBlock);
            }

            photonQueue->release(photons);

            auto workEnd = std::chrono::system_clock::now();

            workDuration += std::chrono::duration_cast<std::chrono::microseconds>(workEnd - workStart).count();

            // std::cout << m_index << ": finished processing, generated " << hitsGenerated << " hits" << std::endl;
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // std::cout << m_index << ": end thread" << std::endl;
}
