#include "PngExport.h"

#include <png.h>

#include <cstdio>
#include <vector>

namespace PngExport
{

void flipVertical(std::vector<uint8_t>& pixels, int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    std::vector<uint8_t> tmp(rowBytes);
    for (int y = 0; y < height / 2; ++y)
    {
        uint8_t* top = pixels.data() + static_cast<size_t>(y) * rowBytes;
        uint8_t* bottom =
            pixels.data() + static_cast<size_t>(height - 1 - y) * rowBytes;
        std::copy(top, top + rowBytes, tmp.begin());
        std::copy(bottom, bottom + rowBytes, top);
        std::copy(tmp.begin(), tmp.end(), bottom);
    }
}

bool writeRgba(const std::string& path, int width, int height,
               const uint8_t* pixels)
{
    if (width <= 0 || height <= 0 || pixels == nullptr)
    {
        return false;
    }

    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr)
    {
        return false;
    }

    png_structp png =
        png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (png == nullptr)
    {
        std::fclose(file);
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (info == nullptr)
    {
        png_destroy_write_struct(&png, nullptr);
        std::fclose(file);
        return false;
    }

    // libpng uses setjmp/longjmp for error handling.
    if (setjmp(png_jmpbuf(png)))
    {
        png_destroy_write_struct(&png, &info);
        std::fclose(file);
        return false;
    }

    png_init_io(png, file);
    png_set_IHDR(png, info, static_cast<png_uint_32>(width),
                 static_cast<png_uint_32>(height), 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE,
                 PNG_FILTER_TYPE_BASE);
    png_write_info(png, info);

    std::vector<png_bytep> rows(static_cast<size_t>(height));
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height; ++y)
    {
        // const_cast is safe: libpng does not modify row data on write.
        rows[static_cast<size_t>(y)] =
            const_cast<png_bytep>(pixels + static_cast<size_t>(y) * rowBytes);
    }

    png_write_image(png, rows.data());
    png_write_end(png, nullptr);

    png_destroy_write_struct(&png, &info);
    std::fclose(file);
    return true;
}

bool writeRgba(const std::string& path, int width, int height,
               const std::vector<uint8_t>& pixels)
{
    if (pixels.size() < static_cast<size_t>(width) * height * 4)
    {
        return false;
    }
    return writeRgba(path, width, height, pixels.data());
}

}  // namespace PngExport
