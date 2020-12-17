#include "Buffer.h"

#include <cmath>
#include <cstdint>

namespace
{

constexpr float scalingFactor = 65535.0f;

}

Buffer::Buffer(size_t width, size_t height)
    : m_width(width)
    , m_height(height)
    , m_buffer(m_width * m_height * 3)
{
    clear();
}

void Buffer::clear()
{
    for (auto& value : m_buffer)
    {
        value = 0;
    }
}

void Buffer::addColor(PixelCoords coords, const Color& color)
{
    size_t index = ((coords.y * m_width) + coords.x) * 3;

    if (index >= m_buffer.size())
    {
        return;
    }

    m_buffer[index + 0].fetch_add(static_cast<uint32_t>(std::round(color.red * scalingFactor)));
    m_buffer[index + 1].fetch_add(static_cast<uint32_t>(std::round(color.green * scalingFactor)));
    m_buffer[index + 2].fetch_add(static_cast<uint32_t>(std::round(color.blue * scalingFactor)));
}

Color Buffer::fetchColor(PixelCoords coords) const
{
    size_t index = ((coords.y * m_width) + coords.x) * 3;

    if (index >= m_buffer.size())
    {
        return {};
    }

    return {
        static_cast<float>(m_buffer[index + 0].load()) / scalingFactor,
        static_cast<float>(m_buffer[index + 1].load()) / scalingFactor,
        static_cast<float>(m_buffer[index + 2].load()) / scalingFactor
    };
}
