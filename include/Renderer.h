#pragma once

#include "BounceStore.h"
#include "Buffer.h"
#include "DensityGrid.h"
#include "EmissiveGather.h"
#include "Image.h"
#include "MirrorGather.h"
#include "ProbeGather.h"
#include "SceneLoader.h"

#include <cstdint>
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
    std::shared_ptr<Buffer> buffer;  // composited splat + mirror-gather buffer (1/N normalized)
    std::shared_ptr<Image> image;    // tonemapped 16-bit image for this camera

    // Storage pivot: mirror-gather diagnostics for this camera (how many delta
    // pixels reflected the grid vs stayed black).
    MirrorGather::Result mirror;

    // Emissive-gather diagnostics for this camera (how many pixels a light
    // fixture was directly visible in, and at what radiance).
    EmissiveGather::Result emissive;

    // Phase 2a probe-guided unified-gather diagnostics for this camera (pixels
    // hit / gathered / extended through delta, deposits summed). Populated only
    // when the probe gather is enabled; otherwise the mirror/splat path is used.
    ProbeGather::Result probe;

    // Wall-clock time this camera's mirror gather took. The shared photon pass
    // time (which now includes the direct splat) is reported once at the
    // RenderResult level.
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

    // Peak (high-water-mark) slot occupancy of the photon queue over the whole
    // render. Single-photon trace-to-completion keeps the population constant (one
    // outgoing photon per bounce), so the queue only ever holds emitted batches in
    // flight — there is no separate emitter / overflow queue. Reported by the
    // render-test CLI.
    size_t peakPhotonQueue = 0;

    // Storage pivot: the QUANTIZED DENSITY GRID built during the photon pass. This
    // is the compact reflection store that replaces the per-photon BounceCloud +
    // HashGrid. Memory is bounded by occupied cells (grid->cellCount()), not photon
    // count — the headline memory win. The direct image comes from the forward
    // splat (no per-photon storage); mirror reflections come from this grid.
    std::shared_ptr<DensityGrid> densityGrid;

    // Storage pivot: mirror-gather diagnostics for the PRIMARY camera.
    MirrorGather::Result mirror;

    // Phase 2a: the raw-bounce store built during the photon pass (probe-guided
    // gather). Memory is bounded by the probe keep-test (visible-surface-area),
    // not photon count. Null when the legacy density-grid path is used.
    std::shared_ptr<BounceStore> bounceStore;

    // Phase 2a: emitter radiance deposits stored on light-fixture surfaces so the
    // unified gather renders fixtures directly AND in mirrors (no special pass).
    size_t emitterDepositsKept = 0;

    // Phase 2a: deposits DROPPED because the BounceStore hit its capacity ceiling
    // during the photon pass. Nonzero means the image is quietly missing energy
    // (the gather summed fewer deposits than the photon pass produced). Surfaced
    // at end-of-render (loud warning + this counter) so an overflow is never
    // silently swallowed; raise $bounceStoreCapacity (or lower photon budget) to
    // clear it.
    std::uint64_t bounceStoreDropped = 0;
};

namespace Renderer
{

// Optional progress callback. Invoked periodically from the orchestration loop
// with the number of work items still outstanding (emissions + photons + hits +
// final hits). Return false to request an early abort. May be null.
using ProgressCallback = std::function<bool(size_t remainingWork)>;

// Optional PROGRESSIVE PREVIEW callback. Invoked periodically from the same
// orchestration loop (a few times/sec) with the PRIMARY camera's in-progress
// splat buffer — the direct-lighting accumulator the workers are filling right
// now. This lets a UI snapshot the image AS IT CONVERGES (the editor's live
// viewport overlay). Contract:
//   - `buffer` is the live splat buffer; read it with Buffer::fetchColor, which
//     does atomic loads. Reads race with concurrent worker fetch_adds; per-pixel
//     loads are atomic (no UB), and a snapshot tolerating minor cross-pixel
//     tearing is acceptable for a preview.
//   - `emittedFraction` is photonsEmittedSoFar / photonsTotal in (0,1]. The
//     single-photon buffer holds energy scaled by 1/N_total, so it is DARK early
//     and reaches full brightness only when all photons have landed. Divide the
//     tonemap input by `emittedFraction` (i.e. scale up by N_total/N_emitted) for
//     STABLE brightness as it converges, instead of dark-then-bright flicker.
//   - `saturationLuminance` is the primary camera's L_max for tonemapBufferToImage.
// The preview shows DIRECT lighting only — mirror/emissive gather run after the
// photon pass, so reflections appear in the final frame, not the live preview.
// May be null.
using PreviewCallback =
    std::function<void(const Buffer& buffer, double emittedFraction, double saturationLuminance)>;

// Run the photon path tracer to completion for the scene's start frame and
// return the populated buffer + tonemapped image. This is the orchestration
// that previously lived inline in src/main.cpp: spin up Workers, seed the light
// queue, drain the pipeline, then tonemap the Buffer into an Image.
//
// Renders a single frame (settings.startFrame). Multi-frame/animation output
// remains the executable's responsibility (it loops and calls per frame).
//
// Throws if a worker raises an exception.
RenderResult renderFrame(const LoadedScene& scene, ProgressCallback progress = nullptr,
                         PreviewCallback preview = nullptr);

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
