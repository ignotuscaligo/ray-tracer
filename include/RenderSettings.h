#pragma once

#include <cstddef>

// Tunables for a render pass. These mirror the fields that main.cpp historically
// read out of a scene JSON's $workerConfiguration / $renderConfiguration blocks.
// Defaults match the historical ProjectConfiguration defaults so that a render
// driven purely from defaults behaves identically to the pre-refactor executable.
struct RenderSettings
{
    static constexpr size_t kMillion = 1000000;
    static constexpr size_t kThousand = 1000;

    size_t photonQueueSize = 20 * kMillion;
    size_t hitQueueSize = 5 * kMillion;
    size_t emittingQueueSize = 20 * kMillion;
    size_t finalQueueSize = 100 * kThousand;
    size_t photonsPerLight = 20 * kMillion;
    size_t workerCount = 32;
    size_t fetchSize = 100000;
    size_t imageWidth = 1080;
    size_t imageHeight = 1080;
    size_t startFrame = 0;
    size_t endFrame = 0;
    size_t bounceThreshold = 1;
};
