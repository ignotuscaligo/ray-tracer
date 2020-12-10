#include "Image.h"

#include <cstring>

Image::Image(size_t width, size_t height)
    : m_width(width)
    , m_height(height)
    , m_pixels(new Pixel[m_width * m_height])
{
}

size_t Image::width() const
{
    return m_width;
}

size_t Image::height() const
{
    return m_height;
}

Pixel* Image::getRow(size_t row)
{
    return &(m_pixels.get()[row * m_width]);
}

void Image::setPixel(size_t x, size_t y, Pixel pixel)
{
    m_pixels.get()[(y * m_width) + x] = pixel;
}

Pixel Image::getPixel(size_t x, size_t y)
{
    return m_pixels.get()[(y * m_width) + x];
}

void Image::clear()
{
    std::memset(m_pixels.get(), 0, m_width * m_height * sizeof(Pixel));
}
