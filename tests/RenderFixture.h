#pragma once

#include <catch2/catch_test_macros.hpp>

#include "Buffer.h"
#include "Color.h"
#include "PixelCoords.h"
#include "Renderer.h"
#include "SceneLoader.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// ============================================================================
// RenderFixture.h — shared objective-test infrastructure
// ============================================================================
//
// Built per docs/test-plan-fable-2026-06.md ("Test infrastructure to build
// first — the unlock"). Three pieces live here:
//
//   1. RenderScene — the temp-JSON -> SceneLoader::loadFromFile ->
//      Renderer::renderFrame -> cleanup pattern that was copy-pasted across
//      test_ShutterBrightness / test_AnimatedGather / test_MultiCameraProbe,
//      extracted into one RAII fixture.
//
//   2. Image-statistics helpers on the PRE-TONEMAP float Buffer (meanLuminance,
//      regionMean, centroid, secondMoment, rmse). Tests assert on RADIANCE, not
//      on the 16-bit tonemapped pixels (which quantize away small regressions).
//      The float buffer is RenderResult::buffer / CameraRender::buffer — the
//      gather's accumulation buffer before tonemapBufferToImage runs.
//
//   3. Statistical assert helpers (REQUIRE_MEAN_CI, REQUIRE_PROPORTION_CI,
//      chiSquareStatistic) so a tolerance is an explicit confidence interval,
//      not a magic margin.
//
// The furnace fixture (sealed-cube scene + truncated-geometric-series oracle)
// lives in FurnaceFixture.h, which builds on this.

namespace rt_test
{

// ===== Pre-tonemap float-buffer image statistics =====
//
// All operate on the linear radiance Buffer (RenderResult::buffer or a
// CameraRender::buffer), NOT the tonemapped Image. Luminance is the unweighted
// channel mean (R+G+B)/3 — the SAME convention CameraRender::meanLuminance uses,
// so a fixture's meanLuminance(buffer) matches the renderer's reported value.

inline double pixelLuminance(const Color& c)
{
    return (static_cast<double>(c.red) + static_cast<double>(c.green) +
            static_cast<double>(c.blue)) / 3.0;
}

// Rec.709 luminance, for tests that want a perceptual weighting.
inline double pixelLuminance709(const Color& c)
{
    return 0.2126 * c.red + 0.7152 * c.green + 0.0722 * c.blue;
}

inline double meanLuminance(const Buffer& buffer, size_t width, size_t height)
{
    if (width == 0 || height == 0)
    {
        return 0.0;
    }
    double sum = 0.0;
    for (size_t y = 0; y < height; ++y)
    {
        for (size_t x = 0; x < width; ++x)
        {
            sum += pixelLuminance(buffer.fetchColor({x, y}));
        }
    }
    return sum / (static_cast<double>(width) * static_cast<double>(height));
}

// Mean luminance over an inclusive pixel rectangle [x0,x1] x [y0,y1].
inline double regionMean(const Buffer& buffer, size_t x0, size_t y0, size_t x1,
                         size_t y1)
{
    double sum = 0.0;
    size_t count = 0;
    for (size_t y = y0; y <= y1; ++y)
    {
        for (size_t x = x0; x <= x1; ++x)
        {
            sum += pixelLuminance(buffer.fetchColor({x, y}));
            ++count;
        }
    }
    return (count > 0) ? sum / static_cast<double>(count) : 0.0;
}

// Per-channel region mean (for parity ratios that must be evaluated per channel).
inline Color regionMeanColor(const Buffer& buffer, size_t x0, size_t y0, size_t x1,
                             size_t y1)
{
    double r = 0.0, g = 0.0, b = 0.0;
    size_t count = 0;
    for (size_t y = y0; y <= y1; ++y)
    {
        for (size_t x = x0; x <= x1; ++x)
        {
            const Color c = buffer.fetchColor({x, y});
            r += c.red;
            g += c.green;
            b += c.blue;
            ++count;
        }
    }
    if (count == 0)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }
    const double inv = 1.0 / static_cast<double>(count);
    return Color{static_cast<float>(r * inv), static_cast<float>(g * inv),
                 static_cast<float>(b * inv)};
}

struct Centroid
{
    double x = 0.0;
    double y = 0.0;
    double totalWeight = 0.0;  // sum of luminance over the region (the normalizer)
};

// Luminance-weighted centroid (first moment) of a region — the "where is the
// bright blob" oracle. Coordinates are in pixel space.
inline Centroid centroid(const Buffer& buffer, size_t x0, size_t y0, size_t x1,
                         size_t y1)
{
    Centroid c;
    double wx = 0.0, wy = 0.0, wsum = 0.0;
    for (size_t y = y0; y <= y1; ++y)
    {
        for (size_t x = x0; x <= x1; ++x)
        {
            const double w = pixelLuminance(buffer.fetchColor({x, y}));
            wx += w * static_cast<double>(x);
            wy += w * static_cast<double>(y);
            wsum += w;
        }
    }
    if (wsum > 0.0)
    {
        c.x = wx / wsum;
        c.y = wy / wsum;
    }
    c.totalWeight = wsum;
    return c;
}

// Luminance-weighted second moment (spread / variance of the bright blob) about
// its own centroid, in pixel^2. sqrt of this is a blob "radius" — the size oracle
// the mirror-parity test compares (reflected blob size vs direct blob size after
// the unfolded-path-length scale).
inline double secondMoment(const Buffer& buffer, size_t x0, size_t y0, size_t x1,
                           size_t y1)
{
    const Centroid c = centroid(buffer, x0, y0, x1, y1);
    if (c.totalWeight <= 0.0)
    {
        return 0.0;
    }
    double acc = 0.0;
    for (size_t y = y0; y <= y1; ++y)
    {
        for (size_t x = x0; x <= x1; ++x)
        {
            const double w = pixelLuminance(buffer.fetchColor({x, y}));
            const double dx = static_cast<double>(x) - c.x;
            const double dy = static_cast<double>(y) - c.y;
            acc += w * (dx * dx + dy * dy);
        }
    }
    return acc / c.totalWeight;
}

// Root-mean-square per-channel difference between two equal-size buffers — the
// reference-regression backstop and the variance-convergence metric (RMS noise
// vs a reference falls as 1/sqrt(N)).
inline double rmse(const Buffer& a, const Buffer& b, size_t width, size_t height)
{
    if (width == 0 || height == 0)
    {
        return 0.0;
    }
    double acc = 0.0;
    for (size_t y = 0; y < height; ++y)
    {
        for (size_t x = 0; x < width; ++x)
        {
            const Color ca = a.fetchColor({x, y});
            const Color cb = b.fetchColor({x, y});
            const double dr = static_cast<double>(ca.red) - cb.red;
            const double dg = static_cast<double>(ca.green) - cb.green;
            const double db = static_cast<double>(ca.blue) - cb.blue;
            acc += dr * dr + dg * dg + db * db;
        }
    }
    const double n = static_cast<double>(width) * static_cast<double>(height) * 3.0;
    return std::sqrt(acc / n);
}

// True iff every channel of every pixel is bit-identical — the bitwise-
// reproducibility check for the single-thread deterministic mode.
inline bool buffersBitwiseEqual(const Buffer& a, const Buffer& b, size_t width,
                                size_t height)
{
    for (size_t y = 0; y < height; ++y)
    {
        for (size_t x = 0; x < width; ++x)
        {
            const Color ca = a.fetchColor({x, y});
            const Color cb = b.fetchColor({x, y});
            if (ca.red != cb.red || ca.green != cb.green || ca.blue != cb.blue)
            {
                return false;
            }
        }
    }
    return true;
}

// ===== The temp-JSON -> load -> render fixture =====
//
// RAII: writes `json` to a unique temp file, loads it through the production
// SceneLoader, removes the temp file, and renders one frame. Holds the
// LoadedScene + RenderResult so a test can reach the float buffer, the camera
// dimensions, and the probe/diagnostic counters. Renders in the ctor so a test
// is just: RenderScene r{json}; ... assert on r.buffer()/r.result.
class RenderScene
{
public:
    explicit RenderScene(const std::string& json, bool logToStdout = false)
    {
        const std::filesystem::path path = uniqueTempPath();
        {
            std::ofstream out(path);
            out << json;
        }
        m_scene = SceneLoader::loadFromFile(path.string(), logToStdout);
        std::remove(path.string().c_str());

        result = Renderer::renderFrame(m_scene);
    }

    // Render the SAME loaded scene again (a second independent renderFrame) —
    // used by the determinism test to compare two runs of one scene.
    RenderResult renderAgain() const { return Renderer::renderFrame(m_scene); }

    const LoadedScene& scene() const { return m_scene; }
    const RenderSettings& settings() const { return m_scene.settings; }

    // The PRIMARY camera's pre-tonemap float radiance buffer.
    const Buffer& buffer() const { return *result.buffer; }
    size_t width() const { return m_scene.settings.imageWidth; }
    size_t height() const { return m_scene.settings.imageHeight; }

    double meanLuminance() const
    {
        return rt_test::meanLuminance(buffer(), width(), height());
    }

    RenderResult result;

private:
    static std::filesystem::path uniqueTempPath()
    {
        static int counter = 0;
        return std::filesystem::temp_directory_path() /
               ("rt_renderfixture_" + std::to_string(counter++) + "_" +
                std::to_string(reinterpret_cast<std::uintptr_t>(&counter)) + ".json");
    }

    LoadedScene m_scene;
};

}  // namespace rt_test
