#pragma once

#include "Pixel.h"

#include <memory>

class Image
{
public:
    Image(size_t width, size_t height);

    size_t width() const;
    size_t height() const;

    Pixel* getRow(size_t row);
    void setPixel(size_t x, size_t y, Pixel pixel);
    Pixel& getPixel(size_t x, size_t y);
    void clear();

private:
    size_t m_width;
    size_t m_height;
    std::unique_ptr<Pixel> m_pixels;
};