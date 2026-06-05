// Tests for the shared RAII libpng owner (PngWriteSession). These are the
// regression guards for the bugs the wrap was created to kill:
//
//   * the move-ctor / move-assign double-free (construct, move, let both
//     destruct -- must be clean under AddressSanitizer);
//   * input validation (zero dimensions / short buffers rejected before any
//     libpng handle is touched);
//   * a real round-trip (write a small image, confirm success + on-disk
//     dimensions / format).

#include <catch2/catch_all.hpp>

#include "PngWriteSession.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{

std::filesystem::path tempPath(const std::string& name)
{
    return std::filesystem::temp_directory_path() / name;
}

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
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
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

    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r',
                                         '\n', 0x1a, '\n'};
    for (int i = 0; i < 8; ++i)
        if (buf[i] != sig[i]) return h;
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

// Regression guard for the historical double-free: the old PngWriter copied the
// raw png/FILE pointers on move without nulling the source, so both objects
// destructed onto the same handles. Constructing, moving (both ctor and
// assignment), and letting every object destruct must be clean -- under ASan
// this is where the double png_destroy_write_struct / fclose would fire.
TEST_CASE("PngWriteSession move does not double-free", "[PngWriteSession]")
{
    const auto path = tempPath("rt_pws_move.png");

    SECTION("move construction transfers ownership, source becomes empty")
    {
        PngWriteSession a(path);
        REQUIRE(a.valid());

        PngWriteSession b(std::move(a));
        REQUIRE(b.valid());
        // a is moved-from: it must own nothing now, so its destructor is a
        // no-op and does not touch b's handles.
        REQUIRE_FALSE(a.valid());  // NOLINT(bugprone-use-after-move)
        // Both a and b destruct at scope end; ASan must stay clean.
    }

    SECTION("move assignment releases the target and nulls the source")
    {
        PngWriteSession a(path);
        REQUIRE(a.valid());

        const auto path2 = tempPath("rt_pws_move2.png");
        PngWriteSession b(path2);
        REQUIRE(b.valid());

        // b already owns handles; assigning into it must release those exactly
        // once and then take a's, leaving a empty.
        b = std::move(a);
        REQUIRE(b.valid());
        REQUIRE_FALSE(a.valid());  // NOLINT(bugprone-use-after-move)

        std::filesystem::remove(path2);
    }

    SECTION("self-move-assignment is a safe no-op")
    {
        PngWriteSession a(path);
        REQUIRE(a.valid());
        PngWriteSession& ref = a;
        a = std::move(ref);  // NOLINT(clang-diagnostic-self-move)
        REQUIRE(a.valid());
    }

    std::filesystem::remove(path);
}

TEST_CASE("PngWriteSession rejects bad input before touching libpng",
          "[PngWriteSession]")
{
    const auto path = tempPath("rt_pws_bad.png");

    SECTION("rgb16: zero dimensions rejected")
    {
        PngWriteSession s(path);
        REQUIRE(s.valid());
        std::vector<png_uint_16> px(3, 0);
        REQUIRE_FALSE(s.writeRgb16(0, 1, px));
        REQUIRE_FALSE(s.writeRgb16(1, 0, px));
    }

    SECTION("rgb16: short buffer rejected")
    {
        PngWriteSession s(path);
        REQUIRE(s.valid());
        // Claims 4x4 RGB == 48 values but supplies far fewer.
        std::vector<png_uint_16> px(3, 0);
        REQUIRE_FALSE(s.writeRgb16(4, 4, px));
    }

    SECTION("rgba8: zero dimensions and null pointer rejected")
    {
        PngWriteSession s(path);
        REQUIRE(s.valid());
        std::vector<uint8_t> px(static_cast<size_t>(4) * 4 * 4, 0);
        REQUIRE_FALSE(s.writeRgba8(0, 4, px.data()));
        REQUIRE_FALSE(s.writeRgba8(4, 0, px.data()));
        REQUIRE_FALSE(s.writeRgba8(4, 4, nullptr));
    }

    std::filesystem::remove(path);
}

TEST_CASE("PngWriteSession is invalid for an unwritable path",
          "[PngWriteSession]")
{
    // A path inside a non-existent directory cannot be opened for writing.
    PngWriteSession s(tempPath("rt_pws_nonexistent_dir/inner.png"));
    REQUIRE_FALSE(s.valid());
    std::vector<png_uint_16> px(3, 0);
    REQUIRE_FALSE(s.writeRgb16(1, 1, px));
}

TEST_CASE("PngWriteSession rgb16 round-trips to a valid 16-bit RGB PNG",
          "[PngWriteSession]")
{
    const uint32_t w = 5;
    const uint32_t h = 3;
    std::vector<png_uint_16> px(static_cast<size_t>(w) * h * 3U);
    for (size_t i = 0; i < px.size(); ++i)
    {
        px[i] = static_cast<png_uint_16>((i * 257) & 0xffff);
    }

    const auto path = tempPath("rt_pws_rgb16.png");
    {
        PngWriteSession s(path);
        REQUIRE(s.valid());
        REQUIRE(s.writeRgb16(w, h, px, "round-trip"));
        // A session writes at most once.
        REQUIRE_FALSE(s.writeRgb16(w, h, px));
    }

    REQUIRE(std::filesystem::exists(path));
    const PngHeader hdr = readPngHeader(path);
    std::filesystem::remove(path);

    REQUIRE(hdr.ok);
    REQUIRE(hdr.width == w);
    REQUIRE(hdr.height == h);
    REQUIRE(hdr.bitDepth == 16);
    REQUIRE(hdr.colorType == 2);  // PNG_COLOR_TYPE_RGB
}

TEST_CASE("PngWriteSession rgba8 round-trips to a valid 8-bit RGBA PNG",
          "[PngWriteSession]")
{
    const uint32_t w = 4;
    const uint32_t h = 6;
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4U);
    for (size_t i = 0; i < px.size(); ++i)
    {
        px[i] = static_cast<uint8_t>(i & 0xff);
    }

    const auto path = tempPath("rt_pws_rgba8.png");
    {
        PngWriteSession s(path);
        REQUIRE(s.valid());
        REQUIRE(s.writeRgba8(w, h, px.data()));
    }

    REQUIRE(std::filesystem::exists(path));
    const PngHeader hdr = readPngHeader(path);
    std::filesystem::remove(path);

    REQUIRE(hdr.ok);
    REQUIRE(hdr.width == w);
    REQUIRE(hdr.height == h);
    REQUIRE(hdr.bitDepth == 8);
    REQUIRE(hdr.colorType == 6);  // PNG_COLOR_TYPE_RGBA
}
