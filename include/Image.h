#pragma once

#include "Pixel.h"

#include <memory>

class Image
{
public:
    Image(int width, int height);

    int width() const;
    int height() const;

    Pixel* getRow(int row);
    void setPixel(int x, int y, Pixel pixel);
    Pixel getPixel(int x, int y);
    void clear();

private:
    int m_width;
    int m_height;
    std::unique_ptr<Pixel> m_pixels;
};