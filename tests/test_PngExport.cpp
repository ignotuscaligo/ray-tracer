// Tests for the screenshot PNG writer (editor/PngExport.cpp). GL-free: the
// encoder takes a raw RGBA buffer, so we synthesize buffers and exercise the
// encode + vertical-flip paths without any OpenGL context. This is the same
// encoder the editor's screenshot command uses to turn glReadPixels output into
// a PNG, so verifying it here covers the non-GL half of screenshot capture.

#include <catch2/catch_all.hpp>

#include "PngExport.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{

std::filesystem::path tempPath(const std::string& name)
{
    return std::filesystem::temp_directory_path() / name;
}

// Read the first 8 bytes (PNG signature) and the IHDR width/height from a file.
struct PngHeader
{
    bool ok = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t bitDepth = 0;
    uint8_t colorType = 0;
};

uint32_t readBE32(const unsigned char* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

PngHeader readPngHeader(const std::filesystem::path& path)
{
    PngHeader h;
    std::ifstream in(path, std::ios::binary);
    if (!in) return h;
    unsigned char buf[33] = {0};
    in.read(reinterpret_cast<char*>(buf), sizeof(buf));
    if (in.gcount() < 33) return h;

    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    for (int i = 0; i < 8; ++i)
        if (buf[i] != sig[i]) return h;

    // Bytes 8..15: first chunk length (4) + type (4) == "IHDR". IHDR data starts
    // at byte 16: width(4) height(4) bitDepth(1) colorType(1).
    if (!(buf[12] == 'I' && buf[13] == 'H' && buf[14] == 'D' && buf[15] == 'R'))
        return h;
    h.width = readBE32(buf + 16);
    h.height = readBE32(buf + 20);
    h.bitDepth = buf[24];
    h.colorType = buf[25];
    h.ok = true;
    return h;
}

}  // namespace

TEST_CASE("writeRgba produces a valid PNG with the requested dimensions", "[PngExport]")
{
    const int w = 7;
    const int h = 5;
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < px.size(); i += 4)
    {
        px[i + 0] = static_cast<uint8_t>(i & 0xff);
        px[i + 1] = static_cast<uint8_t>((i >> 1) & 0xff);
        px[i + 2] = static_cast<uint8_t>((i >> 2) & 0xff);
        px[i + 3] = 255;
    }

    const auto path = tempPath("rt_pngexport_basic.png");
    REQUIRE(PngExport::writeRgba(path.string(), w, h, px));

    const PngHeader hdr = readPngHeader(path);
    std::filesystem::remove(path);

    REQUIRE(hdr.ok);
    REQUIRE(hdr.width == static_cast<uint32_t>(w));
    REQUIRE(hdr.height == static_cast<uint32_t>(h));
    REQUIRE(hdr.bitDepth == 8);
    REQUIRE(hdr.colorType == 6);  // PNG_COLOR_TYPE_RGBA
}

TEST_CASE("writeRgba rejects bad arguments", "[PngExport]")
{
    std::vector<uint8_t> px(4 * 4 * 4, 0);
    const auto path = tempPath("rt_pngexport_bad.png");

    // Zero / negative dimensions.
    REQUIRE_FALSE(PngExport::writeRgba(path.string(), 0, 4, px));
    REQUIRE_FALSE(PngExport::writeRgba(path.string(), 4, -1, px));

    // Buffer too small for the claimed dimensions.
    REQUIRE_FALSE(PngExport::writeRgba(path.string(), 100, 100, px));

    // Null pointer overload.
    REQUIRE_FALSE(PngExport::writeRgba(path.string(), 4, 4, static_cast<const uint8_t*>(nullptr)));

    std::filesystem::remove(path);  // no-op if never written
}

TEST_CASE("flipVertical swaps top and bottom rows (GL bottom-left -> PNG top-left)", "[PngExport]")
{
    // 2 px wide, 3 px tall. Tag each row by its first pixel's red channel.
    const int w = 2;
    const int h = 3;
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 0);
    const size_t rowBytes = static_cast<size_t>(w) * 4;
    px[0 * rowBytes + 0] = 10;  // row 0 red
    px[1 * rowBytes + 0] = 20;  // row 1 red
    px[2 * rowBytes + 0] = 30;  // row 2 red

    PngExport::flipVertical(px, w, h);

    // Row 0 and row 2 swap; the middle row stays put.
    REQUIRE(px[0 * rowBytes + 0] == 30);
    REQUIRE(px[1 * rowBytes + 0] == 20);
    REQUIRE(px[2 * rowBytes + 0] == 10);
}

TEST_CASE("flipVertical is a no-op for degenerate sizes", "[PngExport]")
{
    std::vector<uint8_t> px{1, 2, 3, 4};
    const std::vector<uint8_t> before = px;
    PngExport::flipVertical(px, 0, 0);
    REQUIRE(px == before);
    PngExport::flipVertical(px, 1, 1);  // single row: nothing to swap
    REQUIRE(px == before);
}
