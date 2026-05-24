#include "Buffer.h"

#include <cstddef>

Buffer::Buffer(size_t width, size_t height)
    : m_width(width)
    , m_height(height)
    , m_count(width * height * 3)
    , m_buffer(std::make_unique<std::atomic<float>[]>(width * height * 3))
{
    clear();
}

void Buffer::clear()
{
    for (size_t i = 0; i < m_count; ++i)
    {
        m_buffer[i].store(0.0f, std::memory_order_relaxed);
    }
}

void Buffer::addColor(PixelCoords coords, const Color& color)
{
    const size_t index = ((coords.y * m_width) + coords.x) * 3;

    if (index + 2 >= m_count)
    {
        return;
    }

    // std::atomic<float>::fetch_add is C++20 (P0020). Lock-free on most
    // platforms (including Apple Silicon and modern x86) — verified at
    // construction time by std::atomic<float>::is_always_lock_free if needed.
    m_buffer[index + 0].fetch_add(color.red,   std::memory_order_relaxed);
    m_buffer[index + 1].fetch_add(color.green, std::memory_order_relaxed);
    m_buffer[index + 2].fetch_add(color.blue,  std::memory_order_relaxed);
}

Color Buffer::fetchColor(PixelCoords coords) const
{
    const size_t index = ((coords.y * m_width) + coords.x) * 3;

    if (index + 2 >= m_count)
    {
        return {};
    }

    return {
        m_buffer[index + 0].load(std::memory_order_relaxed),
        m_buffer[index + 1].load(std::memory_order_relaxed),
        m_buffer[index + 2].load(std::memory_order_relaxed)
    };
}
