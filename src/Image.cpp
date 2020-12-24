#include "Image.h"

#include <algorithm>
#include <cassert>
#include <cstring>

Image::Image(size_t width, size_t height)
    : m_width(width)
    , m_height(height)
    , m_pixels(m_width * m_height)
{
}

size_t Image::width() const noexcept
{
    return m_width;
}

size_t Image::height() const noexcept
{
    return m_height;
}

void Image::setPixel(size_t x, size_t y, Pixel pixel) noexcept
{
    if (x >= m_width || y >= m_height)
    {
        return;
    }

    m_pixels[(y * m_width) + x] = pixel;
}

Pixel& Image::getPixel(size_t x, size_t y) noexcept
{
    assert(x < m_width && y < m_height);
    return m_pixels[(y * m_width) + x];
}

void Image::clear()
{
    std::fill(m_pixels.begin(), m_pixels.end(), 0);
}
