#include "Image.h"

#include <cstring>

Image::Image(int width, int height)
    : m_width(width)
    , m_height(height)
    , m_pixels(new Pixel[m_width * m_height])
{
}

int Image::width() const
{
    return m_width;
}

int Image::height() const
{
    return m_height;
}

Pixel* Image::getRow(int row)
{
    return &(m_pixels.get()[row * m_width]);
}

void Image::setPixel(int x, int y, Pixel pixel)
{
    m_pixels.get()[(y * m_width) + x] = pixel;
}

void Image::clear()
{
    std::memset(m_pixels.get(), 0, m_width * m_height * sizeof(Pixel));
}
