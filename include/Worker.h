#pragma once

#include "AnimationQuery.h"
#include "Buffer.h"
#include "DensityGrid.h"
#include "Camera.h"
#include "Hit.h"
#include "Image.h"
#include "LightQueue.h"
#include "Material.h"
#include "MaterialLibrary.h"
#include "Object.h"
#include "Photon.h"
#include "RandomGenerator.h"
#include "Volume.h"
#include "WorkQueue.h"

#include <atomic>
#include <exception>
#include <memory>
#include <thread>
#include <vector>

/*

Single-photon trace-to-completion pipeline:

* Lights emit a batch of photons into the bounded photon queue (processLights),
  gated on queue headroom.
* A worker fetches a batch and traces EACH photon to completion (processPhotons):
  intersect the scene, DEPOSIT the bounce energy into the density grid and SPLAT
  it toward each camera, then scatter exactly ONE importance-sampled continuation
  photon and repeat — until the photon decays below the termination floor, hits
  the bounce cap, or escapes. Every bounce is 1-in-1-out, so the population is
  constant and there is no per-bounce requeue / fan-out / overflow.
* After the photon pass the per-camera MirrorGather / EmissiveGather composite
  reflections and emitter visibility over the density grid (src/MirrorGather.cpp,
  src/EmissiveGather.cpp), and the result is tonemapped to the image.

*/

class Worker
{
public:
    Worker(size_t index, size_t fetchSize);

    void start();
    void suspend();
    void resume();
    void stop();
    void exec();
    std::exception_ptr exception();

    // True while the worker's exec() loop is live. Goes false when the worker
    // exits its loop -- on stop(), on a caught exception, or on any internal
    // return-false/break path. The render drain loop uses this as a liveness
    // guard so outstanding work with no live worker can't spin forever.
    bool running() const { return m_running.load(); }

    void setBounceThreshold(size_t bounceThreshold);

    // Single-photon DECAY termination cutoff, as an ABSOLUTE magnitude floor in
    // photon-magnitude (flux / light-count) units. A photon is terminated (the
    // random walk stops) once its current magnitude (max colour channel) falls
    // below terminationThreshold. The bounce cap is the hard depth bound.
    void setTerminationThreshold(double terminationThreshold);

    // Storage pivot M2: photons-per-light N used by the direct splat to normalize
    // each contribution by 1/N (the single count divide the gather applied at
    // lookup). Must be set before the worker starts; default 0 disables the splat.
    void setPhotonsPerLight(double photonsPerLight);

    // Firefly fix: world-space minimum splat-footprint radius. The direct camera
    // splat normalizes each photon by its pixel footprint area (pi * r^2) where
    // r = cameraDistance * tan(pixelHalfAngle). When an indirect photon lands on
    // geometry very close to the camera, r collapses toward zero and 1/(pi r^2)
    // explodes, spiking a single pixel to white (a firefly). Flooring r at this
    // minimum bounds the weight without discarding energy: a too-concentrated
    // splat is spread over the minimum footprint instead. The floor is derived
    // from the scene-depth pixel footprint (the same length that sizes the
    // density grid), so it scales with scene and camera geometry rather than
    // being an arbitrary constant. 0 disables the floor (legacy behavior).
    void setMinSplatRadius(double minSplatRadius);

    // Optional extreme-firefly guard: a generous upper bound on the luminance a
    // single camera splat may add to a pixel. The minimum-radius floor only
    // bounds the geometric 1/(pi r^2) blowup; this clamp also catches fireflies
    // whose energy comes from a degenerate transport path (normal footprint,
    // photon-count-invariant). A splat whose luminance exceeds the clamp is
    // scaled down to it, preserving hue. <= 0 disables (default); intended to be
    // set HIGH so only extreme outliers are touched and the normal image is
    // unchanged.
    void setSplatLuminanceClamp(double clamp);

    std::shared_ptr<Camera> camera;
    std::vector<std::shared_ptr<Object>> objects;
    std::shared_ptr<LightQueue> lightQueue;
    std::shared_ptr<WorkQueue<Photon>> photonQueue;
    std::shared_ptr<MaterialLibrary> materialLibrary;
    // Storage pivot: the QUANTIZED DENSITY GRID. Each non-delta photon bounce is
    // accumulated into the grid CELL it lands in (add(position, power)) instead of
    // stored as a per-photon record. Bounded by occupied cells, not photon count.
    // The mirror gather looks this up at reflected surface points after the pass.
    // May be null (then grid accumulation is skipped).
    std::shared_ptr<DensityGrid> densityGrid;
    // Continuous-time transform oracle (vision doc pillar 1). Default initialization is
    // a StaticAnimationQuery — every transformAt() call returns the scene-load transform
    // regardless of time. Workers currently read object positions through the existing
    // Object::position() path; the animation query is plumbed for future use without
    // requiring a Worker API change.
    std::shared_ptr<AnimationQuery> animationQuery;

private:
    bool processLights();
    bool processPhotons();

    // Storage pivot M2: restored DIRECT CAMERA SPLAT. When a photon hits a
    // NON-DELTA surface, project the hit into camera pixel space; if it is
    // in-frustum, the surface faces the camera, and nothing occludes the line of
    // sight, accumulate the bounce's outgoing radiance toward the camera into that
    // pixel's buffer, then discard the photon. This is the cheap
    // "camera-consumes-the-bounce" model — no per-photon storage — and it gives
    // SHARP direct/diffuse visibility (each photon lands in its exact projected
    // pixel, not smeared over a gather radius).
    //
    // The accumulated value matches the gather's physical-luminance normalization
    // so the existing tonemap is unchanged: each photon contributes
    //   BRDF(wi -> toCamera) * power / (N * pi * r^2)
    // where r = cameraDistance * tan(pixelHalfAngle) is the pixel's world-space
    // footprint radius at the hit depth (same footprint the gather computes), and
    // N = photonsPerLight. Summed over all photons landing in a pixel's footprint
    // this equals the gather's invN/(pi r^2) * sum(BRDF*power) estimate.
    //
    // TESTABILITY (renderer-single-photon test net): declaration moved from private
    // to public so the foreshortening / facing / occlusion behavior can be unit-
    // tested directly (tests/test_SplatToCamera.cpp). No logic change — the body and
    // all call sites are unchanged; this only widens visibility.
public:
    void splatToCamera(const PhotonHit& photonHit, const std::shared_ptr<Material>& material);

private:

public:
    // Storage pivot M3 (multi-camera): a camera the splat targets, paired with the
    // buffer it accumulates into and that camera's debug-deposit filters. The
    // worker splats every camera-visible non-delta bounce into EVERY target whose
    // frustum/filters admit it, so multi-camera and debug cameras (bounce/light
    // filters) keep working under the splat model — each camera's direct image is
    // produced in the photon pass instead of by a per-photon gather.
    struct SplatTarget
    {
        std::shared_ptr<Camera> camera;
        std::shared_ptr<Buffer> buffer;
        int bounceFilter = -1;  // -1 = admit all bounce depths
        int lightFilter = -1;   // -1 = admit all light ids
    };
    void setSplatTargets(const std::vector<SplatTarget>& targets);

private:
    size_t m_index = 0;
    size_t m_fetchSize = 0;

    // Hard safety CAP on bounce depth (decay is the real terminator). Kept large
    // by config so a near-unity-albedo path cannot loop forever.
    size_t m_bounceThreshold = 1;

    // Single-photon decay-termination ABSOLUTE floor (see setTerminationThreshold).
    // Stored as Flux so the decay comparison is unit-checked (Flux > Flux); the
    // public setter still takes a bare double from the scene config boundary.
    Flux m_terminationThreshold{1.0};

    // Storage pivot M2: photons-per-light N for the direct splat's 1/N divide.
    // 0 disables the splat (no normalization possible).
    double m_photonsPerLight = 0.0;

    // Firefly fix: world-space minimum splat-footprint radius (see setter doc).
    // 0 disables the floor.
    double m_minSplatRadius = 0.0;

    // Optional extreme-firefly guard: per-splat luminance clamp (see setter doc).
    // <= 0 disables (default).
    double m_splatLuminanceClamp = 0.0;

    // Storage pivot M3: the cameras + buffers the splat accumulates into (one per
    // scene camera). Set by the Renderer before the worker starts.
    std::vector<SplatTarget> m_splatTargets;

    std::thread m_thread;

    std::atomic_bool m_running;
    std::atomic_bool m_suspend;

    RandomGenerator m_generator;

    std::vector<Hit> m_castBuffer;
    std::vector<PhotonHit> m_volumeHitBuffer;

    std::exception_ptr m_exception;
};

// Firefly-fix splat diagnostics, reset between frames and queried after the
// render completes.
namespace WorkerDebug
{
// Firefly-fix splat counters: total splat contributions vs those whose
// footprint radius was floored to the minimum-radius (the would-be fireflies).
size_t splatTotal();
size_t splatRadiusClamped();
// Optional luminance-clamp guard: splats whose contributed luminance exceeded
// the per-splat clamp and were scaled down. 0 when the clamp is disabled.
size_t splatLuminanceClamped();
void resetSplatCounters();
}
