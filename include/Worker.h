#pragma once

#include "Image.h"
#include "Object.h"
#include "Photon.h"
#include "Pixel.h"
#include "PixelSensor.h"
#include "Tree.h"
#include "Volume.h"
#include "WorkQueue.h"

#include <atomic>
#include <memory>
#include <vector>

struct Worker
{
    Worker() = default;
    Worker(const Worker& other);
    void startWrite(std::shared_ptr<Tree<PhotonHit>> tree);
    void run();

    size_t index;
    std::vector<std::shared_ptr<Object>> objects;
    std::shared_ptr<WorkQueue<Photon>> photonQueue;
    std::shared_ptr<WorkQueue<PhotonHit>> hitQueue;
    std::shared_ptr<std::vector<PixelSensor>> pixelSensors;
    std::shared_ptr<Tree<PhotonHit>> finalTree;
    std::shared_ptr<Image> image;
    size_t fetchSize = 0;
    size_t startPixel = 0;
    size_t endPixel = 0;
    std::atomic_bool running;
    std::atomic_bool writePixels;
    std::atomic_bool writeComplete;
    std::atomic_bool suspend;
    Pixel workingPixel;

    size_t workDuration = 0;
    size_t pushHitDuration = 0;
    size_t castDuration = 0;
};
