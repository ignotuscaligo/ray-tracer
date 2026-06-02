#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Minimal 8-bit RGBA -> PNG writer for screenshot capture.
//
// This is deliberately separate from the library's PngWriter (which writes
// 16-bit RGB from the ray tracer's Image type). Screenshots come straight out
// of glReadPixels as 8-bit RGBA, so a dedicated writer avoids a lossy round
// trip through the Image type and keeps the capture path simple.
//
// GL-free on purpose: it takes a raw pixel buffer, so the GL code can read
// pixels and hand them here, and the tests can synthesize a buffer and exercise
// the encoder without any GL context.
namespace PngExport
{

// Write `width`*`height` RGBA pixels (row-major, top-left origin, 4 bytes per
// pixel) to `path` as an 8-bit RGBA PNG. `pixels` must contain at least
// width*height*4 bytes. Returns true on success.
bool writeRgba(const std::string& path, int width, int height,
               const std::vector<uint8_t>& pixels);

// Same as above from a raw pointer.
bool writeRgba(const std::string& path, int width, int height,
               const uint8_t* pixels);

// Flip an RGBA buffer vertically in place (GL's framebuffer origin is
// bottom-left; PNGs are top-left). Exposed for the capture path and for tests.
void flipVertical(std::vector<uint8_t>& pixels, int width, int height);

}  // namespace PngExport
