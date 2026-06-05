#include "PngExport.h"

#include "PngWriteSession.h"

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

    // Route through the single RAII libpng owner. PngWriteSession handles the
    // FILE*/png_struct/png_info lifetime, the setjmp error landing pad, and the
    // input validation; this path just supplies the 8-bit RGBA bytes.
    PngWriteSession session(path);
    if (!session.valid())
    {
        return false;
    }

    return session.writeRgba8(static_cast<uint32_t>(width),
                              static_cast<uint32_t>(height), pixels);
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
