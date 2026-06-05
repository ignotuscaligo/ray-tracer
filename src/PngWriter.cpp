#include "PngWriter.h"

#include "PngWriteSession.h"

#include <iostream>
#include <vector>

bool PngWriter::writeImage(const std::filesystem::path& path, Image& image,
                           const std::string& title)
{
    std::cout << "---" << std::endl;
    std::cout << "Write image " << path.generic_string() << std::endl;

    const uint32_t width = static_cast<uint32_t>(image.width());
    const uint32_t height = static_cast<uint32_t>(image.height());

    // Pack the Image's Pixels into a flat 16-bit RGB buffer (row major,
    // top-left origin) for the session to encode.
    std::vector<png_uint_16> pixels(static_cast<size_t>(width) * height * 3U);
    for (size_t y = 0; y < image.height(); ++y)
    {
        for (size_t x = 0; x < image.width(); ++x)
        {
            const Pixel& p = image.getPixel(x, y);
            const size_t base = ((y * image.width()) + x) * 3U;
            pixels[base + 0] = p[0];
            pixels[base + 1] = p[1];
            pixels[base + 2] = p[2];
        }
    }

    PngWriteSession session(path);
    if (!session.valid())
    {
        std::cout << "Failed to initialize png writer for "
                  << path.generic_string() << std::endl;
        return false;
    }

    if (!session.writeRgb16(width, height, pixels, title))
    {
        std::cout << "Failed to write png " << path.generic_string()
                  << std::endl;
        return false;
    }

    return true;
}
