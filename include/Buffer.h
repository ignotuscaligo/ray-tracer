#pragma once

#include "Color.h"
#include "PixelCoords.h"

#include <atomic>
#include <vector>

class Buffer
{
public:
    Buffer(size_t width, size_t height);

    void clear();
    void addColor(PixelCoords coords, const Color& color);
    Color fetchColor(PixelCoords coords) const;

private:
    const size_t m_width;
    const size_t m_height;
    std::vector<std::atomic_uint32_t> m_buffer;
};
