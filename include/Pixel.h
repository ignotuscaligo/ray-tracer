#pragma once

#include <png.h>

struct Pixel
{
    Pixel(png_uint_16 value = 0) noexcept
        : red(value)
        , green(value)
        , blue(value)
    {
    }

    Pixel(png_uint_16 ired, png_uint_16 igreen, png_uint_16 iblue) noexcept
        : red(ired)
        , green(igreen)
        , blue(iblue)
    {
    }

    png_uint_16 operator[](size_t index) const noexcept
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

    png_uint_16 red = 0;
    png_uint_16 green = 0;
    png_uint_16 blue = 0;
};
