#pragma once

#include <png.h>

struct Pixel
{
    png_uint_16 red = 0;
    png_uint_16 green = 0;
    png_uint_16 blue = 0;

    png_uint_16 operator[](size_t index) const
    {
        if (index == 0)
        {
            return red;
        }
        else if (index == 1)
        {
            return green;
        }
        else
        {
            return blue;
        }
    }
};
