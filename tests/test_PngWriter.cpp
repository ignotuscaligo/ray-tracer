// Tests for PngWriter::writeImage's output-directory handling. The renderer
// used to fail the very first render of a scene whose $renderPath directory did
// not exist yet, because the PNG file open failed. writeImage now creates the
// destination's parent directory before writing; these guard that behavior.

#include <catch2/catch_all.hpp>

#include "Image.h"
#include "PngWriter.h"

#include <chrono>
#include <filesystem>
#include <string>

namespace
{

// A unique temp subtree for this test run so we never collide with or disturb
// anything on disk.
std::filesystem::path uniqueDir(const std::string& tag)
{
    return std::filesystem::temp_directory_path() /
           ("rt_pngwriter_" + tag + "_" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
}

Image tinyImage()
{
    Image img(2, 2);
    for (size_t y = 0; y < 2; ++y)
        for (size_t x = 0; x < 2; ++x)
            img.setPixel(x, y, Pixel{});
    return img;
}

}  // namespace

TEST_CASE("PngWriter creates a missing output directory before writing",
          "[PngWriter]")
{
    const std::filesystem::path root = uniqueDir("create");
    // Nested, definitely-absent path: parent (and grandparent) must be created.
    const std::filesystem::path out = root / "renders" / "frame.0.png";

    REQUIRE_FALSE(std::filesystem::exists(root));

    Image img = tinyImage();
    REQUIRE(PngWriter::writeImage(out, img, "dir-create"));

    REQUIRE(std::filesystem::exists(out));
    REQUIRE(std::filesystem::is_directory(out.parent_path()));

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("PngWriter writes to an existing directory (create is a no-op)",
          "[PngWriter]")
{
    const std::filesystem::path root = uniqueDir("exists");
    std::filesystem::create_directories(root);
    const std::filesystem::path out = root / "frame.0.png";

    Image img = tinyImage();
    REQUIRE(PngWriter::writeImage(out, img, "dir-exists"));
    REQUIRE(std::filesystem::exists(out));

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("PngWriter writes a bare filename (no parent) to the current dir",
          "[PngWriter]")
{
    // A path with no parent must not trip the directory creation; it resolves
    // to the current working directory.
    const std::filesystem::path out =
        "rt_pngwriter_bare_" +
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) +
        ".png";
    REQUIRE(out.parent_path().empty());

    Image img = tinyImage();
    REQUIRE(PngWriter::writeImage(out, img, "bare"));
    REQUIRE(std::filesystem::exists(out));

    std::error_code ec;
    std::filesystem::remove(out, ec);
}
