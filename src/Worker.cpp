#include "Worker.h"

#include "Color.h"

#include <chrono>
#include <iostream>
#include <optional>
#include <thread>

Worker::Worker(const Worker& other)
{
    index = other.index;
    objects = other.objects;
    photonQueue = other.photonQueue;
    hitQueue = other.hitQueue;
    fetchSize = other.fetchSize;
    running = other.running.load();
    writePixels = other.writePixels.load();
    writeComplete = other.writeComplete.load();
    suspend = other.suspend.load();
}

void Worker::startWrite(std::shared_ptr<Tree<PhotonHit>> tree)
{
    finalTree = tree;
    writeComplete = false;
    writePixels = true;
}

void Worker::run()
{
    // std::cout << index << ": start thread" << std::endl;

    std::vector<PhotonHit> hits;

    if (!photonQueue || !hitQueue || !pixelSensors || !image)
    {
        std::cout << index << ": abort thread, missing required references" << std::endl;
        running = false;
    }

    while (running)
    {
        if (suspend)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (writePixels && !writeComplete)
        {
            if (!finalTree)
            {
                std::cout << index << ": abort thread, finalTree missing when writePixels was enabled" << std::endl;
                break;
            }

            for (size_t i = startPixel; i < endPixel; ++i)
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

            writeComplete = true;
            writePixels = false;
            finalTree = nullptr;
        }
        else if (photonQueue->available() > 0)
        {
            auto workStart = std::chrono::system_clock::now();
            // std::cout << index << ": fetching " << fetchSize << " photons" << std::endl;
            auto photons = photonQueue->fetch(fetchSize);

            // std::cout << index << ": processing " << photons.size() << " photons" << std::endl;

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

            // std::cout << index << ": finished processing, generated " << hitsGenerated << " hits" << std::endl;
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // std::cout << index << ": end thread" << std::endl;
}
