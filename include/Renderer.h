#pragma once

#include "BounceCloud.h"
#include "Buffer.h"
#include "Gather.h"
#include "HashGrid.h"
#include "Image.h"
#include "SceneLoader.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Wave 6: per-camera output of a multi-camera render. The photon pass / bounce
// cloud is shared; each camera contributes one of these (its own gather + image
// at its own resolution/exposure). For a single-camera scene there is exactly one.
struct CameraRender
{
    std::shared_ptr<Camera> camera;  // the camera that produced this image
    std::string outputName;          // base name for the PNG (may be empty)
    std::shared_ptr<Buffer> buffer;  // physical-luminance gather buffer (1/N normalized)
    std::shared_ptr<Image> image;    // tonemapped 16-bit image for this camera
    Gather::GatherResult gather;     // gather diagnostics for this camera

    // Wall-clock time this camera's gather took (Milestone 2 timing). The shared
    // photon pass time is reported once at the RenderResult level.
    double gatherSeconds = 0.0;
    // Mean luminance over the gather buffer (pre-exposure), for the brightness-
    // stability sanity check as the per-pixel footprint shrinks with resolution.
    double meanLuminance = 0.0;
};

// Result of a completed render pass for a single frame.
struct RenderResult
{
    // Raw energy accumulator (linear, unbounded). Useful for progressive preview
    // and for callers that want to do their own tonemapping. For multi-camera
    // renders this is the PRIMARY (first) camera's buffer; see `cameras` for all.
    std::shared_ptr<Buffer> buffer;
    // Tonemapped 16-bit image (gamma-corrected, flipped to image orientation) —
    // identical to what the executable writes to PNG. Primary camera for multi-cam.
    std::shared_ptr<Image> image;

    // Wave 6: one entry per camera in the scene (declaration order). The photon
    // pass ran once and is shared; each entry holds that camera's own gather/image.
    std::vector<CameraRender> cameras;

    // Wall-clock seconds the SHARED photon pass + cloud + grid build took (the
    // part amortized across all cameras). Per-camera gather time lives in each
    // CameraRender::gatherSeconds.
    double photonPassSeconds = 0.0;

    // Peak (high-water-mark) slot occupancy of each pipeline queue over the whole
    // render. Wave 3 evidence: with lazy emitter fan-out the photon queue no
    // longer needs N-daughter contiguous headroom per bounce-hit, and the emitter
    // queue holds compact producers rather than N-multiplied PhotonHits, so the
    // daughter-path memory pressure drops versus the eager path. Reported by the
    // render-test CLI.
    size_t peakPhotonQueue = 0;
    size_t peakHitQueue = 0;
    size_t peakEmitterQueue = 0;
    size_t peakFinalQueue = 0;

    // Wave 4a: the persistent deposit cloud built during the photon pass, and the
    // spatial hash grid built over it after the pass drains. These are populated
    // but NOT yet used to produce the image — the forward splat still drives the
    // image this wave (Wave 4b switches the image source to a gather over these).
    // bounceCloud may hold fewer records than were attempted if the budget was
    // hit (see bounceCloud->budgetHit()).
    std::shared_ptr<BounceCloud> bounceCloud;
    std::shared_ptr<HashGrid> hashGrid;

    // Wave 4b: diagnostics from the gather pass that produced the image (the
    // forward splat is removed; the gather is the sole image source). Reports how
    // many camera pixels found a visible surface, how many gathered >= 1 deposit,
    // how many were left black because the visible surface is a delta/specular
    // material (Wave 4c will ray-extend those), how many camera rays missed all
    // geometry, plus the max footprint radius and mean deposits per gathered pixel.
    Gather::GatherResult gather;
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
