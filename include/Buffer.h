#pragma once

#include "Color.h"
#include "PixelCoords.h"

#include <atomic>
#include <cstddef>
#include <memory>

// Per-pixel energy accumulator. Each (x, y) pixel owns three lock-free
// std::atomic<float> channels (R, G, B) holding the raw summed photon energy
// (NOT a fixed-point integer-scaled quantity). Workers fetch_add into these
// channels from many threads; the PngWriter / tonemapper reads them at the end
// of a frame and converts to sRGB/PNG.
//
// Rationale for floats (was uint32 with a 65535 scaling factor before): the
// fan-out pipeline splits incoming photon energy by 1/N across N daughters.
// With N=32 (Lambertian) over 4 bounces, per-pixel single-photon contributions
// quantized to zero against the integer buffer at any plausible photon budget.
// std::atomic<float>::fetch_add (C++20, P0020) preserves the full dynamic
// range without any scaling factor.
//
// std::atomic<float> is not trivially copyable, so we store the buffer through
// a unique_ptr<std::atomic<float>[]> to keep Buffer move-friendly without
// requiring per-element copy ctors.
class Buffer
{
public:
    Buffer(size_t width, size_t height);

    void clear();
    void addColor(PixelCoords coords, const Color& color);
    Color fetchColor(PixelCoords coords) const;

private:
    const size_t m_width;
    const size_t m_height;
    const size_t m_count;
    std::unique_ptr<std::atomic<float>[]> m_buffer;
};
