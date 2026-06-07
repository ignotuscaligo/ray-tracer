#pragma once

#include <cstddef>

// Tunables for a render pass. These mirror the fields that main.cpp historically
// read out of a scene JSON's $workerConfiguration / $renderConfiguration blocks.
// Defaults match the historical ProjectConfiguration defaults so that a render
// driven purely from defaults behaves identically to the pre-refactor executable.
struct RenderSettings
{
    static constexpr size_t kMillion = 1000000;
    static constexpr size_t kThousand = 1000;

    size_t photonQueueSize = 20 * kMillion;
    size_t photonsPerLight = 20 * kMillion;
    size_t workerCount = 32;
    size_t fetchSize = 100000;
    size_t imageWidth = 1080;
    size_t imageHeight = 1080;
    size_t startFrame = 0;
    size_t endFrame = 0;
    size_t bounceThreshold = 1;

    // ===== Animation / motion-blur timing =====
    // Frames map to TIME so a keyframed scene can be sampled at each frame's
    // instant. Frame f's shutter OPENS at  t_open = frameOffset + f / frameRate
    // and stays open for `shutterTime` seconds; photons for that frame are
    // stamped with a uniform random time in [t_open, t_open + shutterTime), and
    // the scene's animated transforms are evaluated per-photon at that time. Over
    // many photons the time-varying geometry smears -> motion blur, with blur
    // length proportional to the object's speed during the shutter.
    //
    // Defaults: frameRate 24 fps, shutterTime 0. A ZERO shutter collapses the
    // window to an instant (all photons at t_open) -> NO motion blur and a static
    // scene renders bit-for-bit as before. Motion blur is opt-in via $shutterTime.
    double frameRate = 24.0;
    double shutterTime = 0.0;   // seconds the shutter is open per frame.
    double frameOffset = 0.0;   // seconds added to every frame's open time.

    // The current frame's shutter-open time in seconds. main.cpp sets this per
    // frame (= frameOffset + frame / frameRate) before each renderFrame call; the
    // Renderer uses [frameTime, frameTime + shutterTime) as the exposure window.
    // Default 0 keeps single-frame renders at t=0.
    double frameTime = 0.0;

    // ===== Single-photon decay termination =====
    // A photon is killed once its current magnitude (max colour channel) falls
    // below this ABSOLUTE threshold, expressed in photon-magnitude units (the same
    // emitted flux / photon-count units the photon's colour carries — NOT a
    // fraction of emission). A brighter photon therefore survives more bounces
    // than a dimmer one; bounceThreshold above is the hard per-photon depth cap and
    // the real safety bound.
    //
    // UNITS CAVEAT: because emission magnitude is absolute flux / photonsPerLight,
    // the magnitude scale is scene-dependent (a high-flux light over few photons
    // yields per-photon magnitudes in the hundreds). A single fixed default is
    // therefore NOT robust across all scenes; tune $terminationThreshold per scene
    // if the floor matters. Default 1.0 (a conservative floor; the bounce cap is
    // what actually bounds path depth on normal scenes).
    double terminationThreshold = 1.0;

    // Hash-grid cell edge length (world units). Defaults to the expected gather
    // radius so a radius-r query touches a 3x3x3 cell neighborhood. Built over
    // the cloud after the photon pass drains.
    double hashGridCellSize = 1.0;

    // ===== Storage pivot: density-grid cell-size tunability =====
    // The density grid's cell edge length is auto-derived from the gather
    // footprint scale (camera-to-scene-depth * pixel half-angle). This multiplier
    // scales that auto value, exposing the memory/quality tradeoff as a scene/
    // render param ($densityCellScale): a LARGER scale = coarser cells = LESS
    // memory + blurrier reflections; a SMALLER scale = finer cells = MORE memory +
    // sharper reflections. Default 1.0 uses the footprint-scale cell unchanged.
    // Clamped to a sane floor so it can never go microscopic (which would defeat
    // the compression).
    double densityCellScale = 1.0;

    // ===== Firefly fix: minimum splat-footprint radius =====
    // The direct camera splat normalizes each photon by its pixel footprint area
    // (pi * r^2, r = hitDepth * tan(pixelHalfAngle)). For an indirect photon that
    // lands on geometry very close to the camera, r collapses and 1/(pi r^2)
    // explodes, spiking a single pixel to white (a firefly) — this speckles the
    // emissive panel and leaves stray bright dots on diffuse geometry.
    //
    // The fix floors r at r_min = splatMinRadiusScale * sceneDepthFootprint,
    // where sceneDepthFootprint is the world-space radius a pixel projects to at
    // scene-centroid depth (the same length that sizes the density grid). Tying
    // r_min to that footprint makes it scale with scene/camera geometry rather
    // than being an arbitrary constant. The floor is energy-preserving: a too-
    // concentrated splat is spread over the minimum footprint, not discarded.
    //
    // Default 0.5: a hit at scene depth is unaffected (its r already equals the
    // full footprint); only hits closer than half the scene depth — where the
    // explosion happens — are floored. Set to 0 to disable (legacy behavior).
    double splatMinRadiusScale = 0.5;

    // ===== Optional per-splat luminance clamp (extreme-firefly guard) =====
    // A GENEROUS upper bound on the luminance a SINGLE camera splat may add to a
    // pixel. The minimum-radius floor above only bounds the geometric 1/(pi r^2)
    // blowup; it cannot touch a firefly whose energy comes from a degenerate
    // light-transport path (e.g. a collinear point-light / sphere-top / camera
    // alignment that produces a 2-pixel over-bright dot with a normal footprint
    // and photon-count-invariant energy). This clamp catches those by scaling a
    // splat's contribution down so its luminance never exceeds the threshold,
    // preserving hue (all channels scaled by the same factor).
    //
    // Default 0 = DISABLED (no clamp, image bit-for-bit unchanged). When set, it
    // is meant to be set HIGH — far above any legitimate single-splat luminance —
    // so it only trims extreme outliers and its energy loss is negligible. It
    // does NOT darken or alter the normal image: a normal splat's luminance is
    // orders of magnitude below a sane threshold and passes through untouched.
    double splatLuminanceClamp = 0.0;

    // ===== Phase 2a: probe-guided unified gather =====
    // When true, the renderer replaces the camera splat + density-grid reflection
    // mechanisms with ONE probe-guided raw-bounce gather: a probe pass extends
    // camera rays through delta surfaces to their first non-delta hit; non-delta
    // photon bounces near a probe are kept RAW in a BounceStore (bounces far from
    // every probe are discarded — this bounds memory by visible-surface-area); a
    // unified gather renders both direct and reflected diffuse from those raw
    // bounces. Retires the density grid. Default false keeps the legacy
    // splat+grid path. ($probeGather in the scene render config.)
    bool useProbeGather = false;

    // Maximum raw bounces retained by the BounceStore (slot capacity). Storage is
    // bounded by the probe keep-test (visible-surface-area), but the store still
    // needs a fixed up-front capacity; this is the ceiling. Bounces past it are
    // dropped and counted. Default 40M (~960 MiB at 24 B/record) comfortably holds
    // a Cornell-scale visible surface at multi-million photon budgets.
    size_t bounceStoreCapacity = 40 * kMillion;

    // Keep-radius scale: a non-delta bounce is kept iff a probe lies within
    // (probeKeepRadiusScale * sceneDepthFootprint) of it. >= 1 so the keep radius
    // is at least one gather footprint (a bounce exactly one footprint from a
    // probe can still contribute to that probe's gather). Larger = more permissive
    // keep (more memory, no missing reflections at fast-moving geometry later);
    // smaller = tighter cull. Default 1.5 (a small safety margin over one
    // footprint).
    double probeKeepRadiusScale = 1.5;

    // Probe-pass pixel stride. 1 = a probe per pixel (densest coverage); >1
    // strides the pixel grid to cut probe-pass cost. Adjacent pixels project to
    // overlapping footprints so a modest stride still tiles the visible surface.
    // Default 1.
    size_t probeSubSample = 1;
};
