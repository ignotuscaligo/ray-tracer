#pragma once

#include "Image.h"

#include <filesystem>
#include <string>

// Thin convenience wrapper over PngWriteSession for the headless renderer's
// 16-bit RGB Image output. All the libpng lifetime/error handling lives in
// PngWriteSession; this just packs an Image into the RGB16 layout and routes it
// through that single safe surface.
class PngWriter
{
public:
    // Write `image` to `path` as a 16-bit RGB PNG with an optional tEXt title.
    // Returns true on success. Equivalent on-disk bytes to the previous
    // hand-rolled writer.
    static bool writeImage(const std::filesystem::path& path, Image& image,
                           const std::string& title);
};
