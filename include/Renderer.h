#pragma once

#include "Buffer.h"
#include "Image.h"
#include "SceneLoader.h"

#include <functional>
#include <memory>

// Result of a completed render pass for a single frame.
struct RenderResult
{
    // Raw energy accumulator (linear, unbounded). Useful for progressive preview
    // and for callers that want to do their own tonemapping.
    std::shared_ptr<Buffer> buffer;
    // Tonemapped 16-bit image (gamma-corrected, flipped to image orientation) —
    // identical to what the executable writes to PNG.
    std::shared_ptr<Image> image;
};

namespace Renderer
{

// Optional progress callback. Invoked periodically from the orchestration loop
// with the number of work items still outstanding (emissions + photons + hits +
// final hits). Return false to request an early abort. May be null.
using ProgressCallback = std::function<bool(size_t remainingWork)>;

// Run the photon path tracer to completion for the scene's start frame and
// return the populated buffer + tonemapped image. This is the orchestration
// that previously lived inline in src/main.cpp: spin up Workers, seed the light
// queue, drain the pipeline, then tonemap the Buffer into an Image.
//
// Renders a single frame (settings.startFrame). Multi-frame/animation output
// remains the executable's responsibility (it loops and calls per frame).
//
// Throws if a worker raises an exception.
RenderResult renderFrame(const LoadedScene& scene, ProgressCallback progress = nullptr);

// Tonemap a raw energy Buffer into a 16-bit Image. Wave 2: applies the two-step
// physical conversion — (a) raw accumulated photon energy -> physical luminance
// via the single 1/photonsEmitted normalization plus footprint factor, then
// (b) luminance -> [0,1] via the camera's saturation exposure L_max — followed by
// the existing gamma curve and pixel flip.
//
//   photonsEmitted       = photons emitted per light (N); the ONE place count enters.
//   saturationLuminance  = camera L_max = (N_f^2 * K) / (t * S).
//
// Exposed separately so progressive preview can tonemap an in-flight snapshot.
void tonemapBufferToImage(const Buffer& buffer, Image& image, double photonsEmitted, double saturationLuminance);

}
