#pragma once

#include "Pixel.h"

#include <memory>
#include <vector>

class Image
{
public:
    Image(size_t width, size_t height);

    size_t width() const noexcept;
    size_t height() const noexcept;

    void setPixel(size_t x, size_t y, Pixel pixel) noexcept;
    Pixel& getPixel(size_t x, size_t y) noexcept;
    void clear();

private:
    size_t m_width;
    size_t m_height;
    std::vector<Pixel> m_pixels;
};