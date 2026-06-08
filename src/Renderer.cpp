#include "Renderer.h"

#include "AnimationQuery.h"
#include "BounceStore.h"
#include "Color.h"
#include "DensityGrid.h"
#include "EmissiveGather.h"
#include "MirrorGather.h"
#include "ProbeGather.h"
#include "ProbeIndex.h"
#include "LightQueue.h"
#include "Light.h"
#include "Photon.h"
#include "Pixel.h"
#include "Utility.h"
#include "Vector.h"
#include "WorkQueue.h"
#include "Worker.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>

namespace Renderer
{

// Wave 4b: the Wave-2 global footprint calibration constant (kFootprintCalibration)
// is GONE. It was a uniform stand-in for the per-pixel solid-angle / projected-area
// factor that the forward splat could not compute. The gather pass (src/Gather.cpp)
// now computes the REAL per-pixel footprint — pi * r^2 with r = depth * tan(pixelHalfAngle)
// — and the 1/N photon-count normalization, writing physical luminance directly into
// the buffer. The tonemap therefore no longer applies any footprint factor or 1/N
// divide: the buffer already holds luminance L (cd/m^2). The only job left here is
// the photographic exposure (L / L_max) and the gamma curve.

void tonemapBufferToImage(const Buffer& buffer, Image& image, double photonsEmitted, double saturationLuminance)
{
    (void)photonsEmitted;  // 1/N is applied upstream in the gather now.

    const size_t width = image.width();
    const size_t height = image.height();

    // pixel = L / L_max. Guard a degenerate L_max.
    const double invLmax = (saturationLuminance > 0.0) ? (1.0 / saturationLuminance) : 0.0;

    Pixel workingPixel;

    for (size_t y = 0; y < height; ++y)
    {
        for (size_t x = 0; x < width; ++x)
        {
            const Color color = buffer.fetchColor({x, y});

            // The buffer already holds physical luminance (gather applied 1/N and
            // the real per-pixel footprint). Apply only the photographic exposure.
            const double exposedR = color.red * invLmax;
            const double exposedG = color.green * invLmax;
            const double exposedB = color.blue * invLmax;

            // Existing gamma / sRGB-ish tonemap, then clamp to 16-bit.
            const float gammaRed = std::pow(static_cast<float>(exposedR), 1.0f / Color::gamma);
            const float gammaGreen = std::pow(static_cast<float>(exposedG), 1.0f / Color::gamma);
            const float gammaBlue = std::pow(static_cast<float>(exposedB), 1.0f / Color::gamma);

            workingPixel.red = std::min(static_cast<int>(gammaRed * 65535), 65535);
            workingPixel.green = std::min(static_cast<int>(gammaGreen * 65535), 65535);
            workingPixel.blue = std::min(static_cast<int>(gammaBlue * 65535), 65535);

            image.setPixel((width - 1) - x, (height - 1) - y, workingPixel);
        }
    }
}

RenderResult renderFrame(const LoadedScene& scene, ProgressCallback progress,
                         PreviewCallback preview)
{
    const RenderSettings& settings = scene.settings;

    // Wave 6: the shared photon pass starts here. Time it so the per-camera gather
    // cost (Milestone 2) can be reported separately from the one-time lighting solve.
    const std::chrono::time_point photonPassStart = std::chrono::system_clock::now();

    RenderResult result;
    result.buffer = std::make_shared<Buffer>(settings.imageWidth, settings.imageHeight);
    result.image = std::make_shared<Image>(settings.imageWidth, settings.imageHeight);

    std::shared_ptr<Buffer> buffer = result.buffer;
    std::shared_ptr<Image> image = result.image;

    std::shared_ptr<LightQueue> lightQueue = std::make_shared<LightQueue>();
    std::shared_ptr<WorkQueue<Photon>> photonQueue = std::make_shared<WorkQueue<Photon>>(settings.photonQueueSize);

    // Continuous-time animation oracle. The scene loader builds a
    // KeyframedAnimationQuery from per-object $animation blocks; if no object is
    // animated we use a StaticAnimationQuery so a static scene is bit-for-bit the
    // baseline (every transformAt returns the scene-load transform). The workers
    // and gathers evaluate this query at each photon's time, so a keyframed
    // transform smears across the shutter into motion blur.
    std::shared_ptr<AnimationQuery> animationQuery;
    if (scene.animation && !scene.animation->empty())
    {
        animationQuery = scene.animation;
    }
    else
    {
        animationQuery = std::make_shared<StaticAnimationQuery>();
    }

    // Per-frame shutter window. Photons spawn with a uniform random time in
    // [frameTime, frameTime + shutterTime) and the scene is evaluated at that
    // time per-photon -> distributed (stochastic-time) motion blur. A zero
    // shutter collapses the window to the instant frameTime: all photons share
    // one time, no blur, and a static scene is unchanged. The window is set on
    // EVERY camera so each camera's emission-time sampling + splat gate agree.
    // [INVARIANT] Spreading photons over the shutter does NOT change total light
    // energy: emit() still produces the same photon COUNT carrying the same
    // per-photon flux (Phi/N, baked at emission, DESIGN.md §3). The shutter only
    // re-tags each photon's TIME; it does not scale magnitude or drop photons
    // (times are sampled inside [start,end) so the splat's exposure-window gate,
    // Worker.cpp, always passes). Hence a static scene rendered with a shutter has
    // the SAME brightness as one rendered without (verified: test_ShutterBrightness).
    {
        Camera::ExposureWindow window;  // default = infinite [-inf, +inf).
        const float start = static_cast<float>(settings.frameTime);
        const float shutter = static_cast<float>(settings.shutterTime);
        if (shutter > 0.0f)
        {
            window.start = start;
            window.end = start + shutter;
        }
        else
        {
            // Zero shutter: a half-infinite window [frameTime, +inf). Emission sees
            // a finite start but non-finite end, so it stamps EVERY photon at exactly
            // frameTime (no jitter) — the scene is sampled at the frame's instant
            // (correct for a no-blur animated frame), and for the default static
            // frameTime=0 that is just time=0. The splat gate contains(frameTime)
            // over [frameTime,+inf) is always true, so NO photon is dropped — the
            // static render is the exact baseline brightness. We deliberately do NOT
            // use a tiny [t, t+eps) window: sub-ulp emission times round to the
            // exclusive end and get dropped, silently halving image energy.
            window.start = start;
            window.end = std::numeric_limits<float>::infinity();
        }
        for (auto& cam : scene.cameras)
        {
            if (cam)
            {
                cam->setGlobalExposureWindow(window);
            }
        }
        if (scene.camera)
        {
            scene.camera->setGlobalExposureWindow(window);
        }
    }

    // Storage pivot: the per-photon BounceCloud + HashGrid are GONE. The direct
    // image now comes from the forward SPLAT (no per-photon storage) and mirror
    // reflections from a compact QUANTIZED DENSITY GRID accumulated during the
    // pass. Storage is bounded by occupied grid cells, not photon count.

    // Resolve the scene cameras up front (declaration order) so each gets its own
    // splat buffer the workers accumulate into during the photon pass. A single-
    // camera scene loops once; the primary is cameras[0]. Fall back to the
    // back-compat scene.camera handle.
    std::vector<std::shared_ptr<Camera>> cameras = scene.cameras;
    if (cameras.empty() && scene.camera)
    {
        cameras.push_back(scene.camera);
    }

    // One splat buffer per camera, at that camera's own resolution. The workers
    // splat every camera-visible non-delta bounce into each camera's buffer
    // (applying that camera's debug bounce/light filter). The mirror gather later
    // composites reflected radiance into the same buffers.
    std::vector<std::shared_ptr<Buffer>> splatBuffers;
    std::vector<Worker::SplatTarget> splatTargets;
    splatBuffers.reserve(cameras.size());
    splatTargets.reserve(cameras.size());
    for (const auto& cam : cameras)
    {
        if (!cam)
        {
            continue;
        }
        auto camBuffer = std::make_shared<Buffer>(cam->width(), cam->height());
        camBuffer->clear();
        splatBuffers.push_back(camBuffer);
        splatTargets.push_back(Worker::SplatTarget{
            cam, camBuffer, cam->bounceFilter(), cam->lightFilter()});
    }

    // Density grid cell size: tied to the gather footprint scale (the world-space
    // size a pixel projects to at scene depth), NOT microscopic — a microscopic
    // cell defeats the compression. Computed below from the primary camera; the
    // configured hashGridCellSize is the fallback. The same scale that sized the
    // old hash grid sizes the cells here.
    double cellSize = settings.hashGridCellSize;
    // The scene-depth pixel footprint radius. Sizes the density grid below AND
    // seeds the firefly floor (minimum splat radius). 0 = could not be derived
    // (no volumes / no camera), in which case both fall back to their configured
    // defaults and the floor is left disabled.
    double sceneDepthFootprint = 0.0;
    {
        const std::shared_ptr<Camera>& primaryCam =
            cameras.empty() ? scene.camera : cameras.front();
        if (primaryCam && settings.imageHeight > 0)
        {
            Vector centroid{0.0, 0.0, 0.0};
            size_t volumeCount = 0;
            for (const auto& object : scene.objects)
            {
                if (object->hasType<Volume>())
                {
                    centroid = centroid + object->position();
                    ++volumeCount;
                }
            }
            if (volumeCount > 0)
            {
                centroid = centroid / static_cast<double>(volumeCount);
                const double depth = (centroid - primaryCam->position()).magnitude();
                const double pixelHalfAngle =
                    0.5 * Utility::radians(primaryCam->verticalFieldOfView()) /
                    static_cast<double>(primaryCam->height() > 0 ? primaryCam->height()
                                                                 : settings.imageHeight);
                const double representativeRadius = depth * std::tan(pixelHalfAngle);
                if (representativeRadius > 0.0)
                {
                    cellSize = representativeRadius;
                    sceneDepthFootprint = representativeRadius;
                }
            }
        }
    }

    // Firefly fix: world-space minimum splat-footprint radius. Tied to the scene-
    // depth pixel footprint so it scales with scene/camera geometry. If the
    // footprint couldn't be derived, leave it 0 (floor disabled). Independent of
    // densityCellScale (a memory knob) on purpose — this is a quality floor.
    const double minSplatRadius =
        (sceneDepthFootprint > 0.0 && settings.splatMinRadiusScale > 0.0)
            ? settings.splatMinRadiusScale * sceneDepthFootprint
            : 0.0;
    // Tunable cell-size scale ($densityCellScale): coarser cells = less memory +
    // blurrier reflections, finer = more memory + sharper. Clamp the scale so the
    // cell can never go microscopic (which would defeat the compression).
    const double cellScale = std::max(0.05, settings.densityCellScale);
    cellSize *= cellScale;
    std::shared_ptr<DensityGrid> densityGrid = std::make_shared<DensityGrid>(cellSize);

    // ===== Phase 2a: probe pass + raw-bounce store =====
    //
    // When probe-guided gather is enabled, run the PROBE PASS before the photon
    // pass: cast camera rays, extend each through delta surfaces to its first
    // non-delta hit, and index those points. During the photon pass the workers
    // keep a non-delta bounce raw only if a probe is within the keep-radius
    // (bounding memory by visible-surface-area), and the post-pass gather renders
    // both direct and reflected diffuse from those raw bounces — retiring the
    // density grid + splat. The keep-radius and probe-index cell size are tied to
    // the same scene-depth pixel footprint that sizes everything else.
    std::shared_ptr<ProbeIndex> probeIndex;
    std::shared_ptr<BounceStore> bounceStore;
    ProbeGather::ProbeResult probeResult;
    double probeGatherMinRadius = 0.0;
    if (settings.useProbeGather)
    {
        const std::shared_ptr<Camera>& primaryCam =
            cameras.empty() ? scene.camera : cameras.front();
        // The gather footprint scale: a pixel's world radius at scene depth. This
        // is sceneDepthFootprint when derivable, else the configured cell size.
        const double footprint =
            (sceneDepthFootprint > 0.0) ? sceneDepthFootprint : cellSize;
        const double keepRadius =
            std::max(footprint, settings.probeKeepRadiusScale * footprint);
        probeGatherMinRadius =
            (sceneDepthFootprint > 0.0 && settings.splatMinRadiusScale > 0.0)
                ? settings.splatMinRadiusScale * sceneDepthFootprint
                : 0.0;

        if (primaryCam)
        {
            probeResult = ProbeGather::collectProbes(
                scene.objects, *primaryCam, *scene.materialLibrary,
                animationQuery.get(), settings.probeSubSample);
        }
        // The probe-index cell size = keepRadius so a keep query touches a 3x3x3
        // neighborhood. The gather's own bounce-index cell size is set later.
        probeIndex = std::make_shared<ProbeIndex>(
            probeResult.probes, keepRadius, keepRadius);

        // Size the raw-bounce store from the VISIBLE-SURFACE-AREA proxy (the probe
        // count) rather than always reserving the full configured capacity: the
        // keep-test bounds the kept bounces by visible area, so the store only
        // needs ~ (photons per visible surface region) slots. Reserve a generous
        // per-probe budget so a bright, heavily-deposited visible surface doesn't
        // overflow, but cap at the configured ceiling so a pathological probe
        // count can't request unbounded RAM. This is what turns the "bounded by
        // visible area" property into an actual smaller ALLOCATION, not just a
        // smaller used-prefix. (At 36 B/slot the default ceiling is ~1.3 GiB.)
        constexpr std::size_t kSlotsPerProbe = 256;
        const std::size_t probeSized =
            probeResult.probes.empty()
                ? settings.bounceStoreCapacity
                : probeResult.probes.size() * kSlotsPerProbe;
        const std::size_t capacity =
            std::min(settings.bounceStoreCapacity,
                     std::max<std::size_t>(1, probeSized));
        bounceStore = std::make_shared<BounceStore>(capacity);

        WorkerDebug::resetBounceCounters();
    }

    std::vector<std::shared_ptr<Worker>> workers{settings.workerCount};

    size_t workerIndex = 0;
    for (auto& worker : workers)
    {
        worker = std::make_shared<Worker>(workerIndex, settings.fetchSize);
        worker->camera = scene.camera;
        worker->objects = scene.objects;
        worker->photonQueue = photonQueue;
        worker->materialLibrary = scene.materialLibrary;
        worker->lightQueue = lightQueue;
        worker->animationQuery = animationQuery;
        worker->setBounceThreshold(settings.bounceThreshold);
        worker->setTerminationThreshold(settings.terminationThreshold);
        worker->setPhotonsPerLight(static_cast<double>(settings.photonsPerLight));
        worker->setMinSplatRadius(minSplatRadius);
        worker->setSplatLuminanceClamp(settings.splatLuminanceClamp);
        if (settings.useProbeGather)
        {
            // Probe mode: keep raw bounces near probes; NO density grid, NO splat.
            worker->bounceStore = bounceStore;
            worker->probeIndex = probeIndex;
        }
        else
        {
            worker->densityGrid = densityGrid;
            worker->setSplatTargets(splatTargets);
        }
        ++workerIndex;
    }

    for (size_t i = 0; i < settings.workerCount; ++i)
    {
        workers[i]->start();
    }

    buffer->clear();
    image->clear();

    // Seed the light queue. Wave 2: each light registers its total luminous flux
    // Phi (lumens), computed from its physical intensity (candela) and emission
    // solid angle. The per-photon carried weight is Phi (count-independent); the
    // divide by photonsPerLight happens once at conversion below.
    for (const auto& object : scene.objects)
    {
        if (object->hasType<Light>())
        {
            const double flux = std::static_pointer_cast<Light>(object)->luminousFlux();
            lightQueue->registerLight(object->name(), settings.photonsPerLight, flux);
        }
    }

    size_t photonsToEmit = lightQueue->remainingPhotons();
    size_t photonsAllocated = photonQueue->allocated();

    // Progressive preview wiring: the PRIMARY camera's splat buffer (the live
    // direct-lighting accumulator) and its exposure, plus the total photon budget
    // so the preview callback can report the emitted fraction for stable-brightness
    // tonemapping. Captured once; the loop below taps them while photons land.
    const size_t totalPhotonsToEmit = photonsToEmit;
    std::shared_ptr<Buffer> previewBuffer = splatBuffers.empty() ? nullptr : splatBuffers.front();
    std::shared_ptr<Camera> previewCamera =
        cameras.empty() ? scene.camera : cameras.front();

    std::exception_ptr workerException;
    bool aborted = false;
    bool drainStalled = false;

    // Completion test for the single-photon trace-to-completion pipeline: work is
    // done when the lights owe no more photons AND no photons remain allocated in
    // the queue. A batch a worker has fetched but not finished tracing keeps the
    // queue's allocation non-zero (the source slots are released only after the
    // whole batch is traced to completion), so in-flight bounce work is covered by
    // photonsAllocated — there is no separate emitter queue or overflow to track.
    while (photonsAllocated > 0 || photonsToEmit > 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        photonsToEmit = lightQueue->remainingPhotons();
        photonsAllocated = photonQueue->allocated();

        for (auto& worker : workers)
        {
            if (worker->exception())
            {
                workerException = worker->exception();
                break;
            }
        }

        if (workerException)
        {
            break;
        }

        // Liveness guard (safety net for an abnormal exit, not the normal path).
        // start() sets m_running synchronously, so every worker is live on the
        // first iteration; this only fires if EVERY worker has left its exec()
        // loop while work is still outstanding -- e.g. a future non-exception
        // return-false/break path. Without it the loop would spin forever with no
        // worker draining the queue and no exception to surface. The normal case
        // (at least one worker still running) is completely unaffected.
        if (photonsAllocated > 0 || photonsToEmit > 0)
        {
            bool anyWorkerRunning = false;
            for (const auto& worker : workers)
            {
                if (worker->running())
                {
                    anyWorkerRunning = true;
                    break;
                }
            }

            if (!anyWorkerRunning)
            {
                drainStalled = true;
                break;
            }
        }

        if (progress)
        {
            const size_t remainingWork = photonsToEmit + photonsAllocated;
            if (!progress(remainingWork))
            {
                aborted = true;
                break;
            }
        }

        // Progressive preview tap: hand the live splat buffer + emitted fraction to
        // the UI so it can snapshot the converging image. emittedFraction is the
        // share of the photon budget that has been emitted so far (in (0,1]); the
        // single-photon buffer is normalized by the TOTAL count, so the consumer
        // scales by 1/emittedFraction for stable brightness. Reads of the buffer
        // are atomic per pixel (see PreviewCallback contract).
        if (preview && previewBuffer && previewCamera)
        {
            const size_t emitted =
                (totalPhotonsToEmit > photonsToEmit) ? (totalPhotonsToEmit - photonsToEmit) : 0;
            const double emittedFraction =
                (totalPhotonsToEmit > 0)
                    ? std::max(1e-6, static_cast<double>(emitted) / static_cast<double>(totalPhotonsToEmit))
                    : 1.0;
            preview(*previewBuffer, emittedFraction, previewCamera->saturationLuminance());
        }
    }

    for (size_t i = 0; i < settings.workerCount; ++i)
    {
        workers[i]->stop();
    }

    if (workerException)
    {
        std::rethrow_exception(workerException);
    }

    if (drainStalled)
    {
        throw std::runtime_error(
            "Render drain stalled: all workers exited with photon work still "
            "outstanding. This indicates a worker terminated abnormally without "
            "surfacing an exception.");
    }

    // Even on a caller-requested abort, tonemap whatever has accumulated so the
    // partial result is usable (the editor's progressive/preview path relies on
    // this).
    (void)aborted;

    // Storage pivot: the photon pass produced the DIRECT image via the forward
    // SPLAT (into each camera's splat buffer — no per-photon storage) and filled
    // the compact DENSITY GRID with non-delta bounce energy for reflections. We
    // keep exposure parameters for the per-camera tonemap.
    const double photonsEmitted = static_cast<double>(settings.photonsPerLight);

    // Memory evidence: high-water-mark occupancy of the (now sole) photon queue.
    result.peakPhotonQueue = photonQueue->largestAllocated();

    // The compact reflection store (bounded by occupied cells, not photon count).
    result.densityGrid = densityGrid;

    // The shared photon pass (emit + splat + grid fill) is complete. Everything
    // below is per-camera mirror-gather work, so freeze the shared time here.
    const std::chrono::time_point photonPassEnd = std::chrono::system_clock::now();
    result.photonPassSeconds =
        std::chrono::duration<double>(photonPassEnd - photonPassStart).count();

    result.cameras.reserve(cameras.size());

    // Phase 2a: build the raw-bounce spatial index ONCE after the photon pass
    // drains (single-threaded). The unified gather queries it per camera.
    if (settings.useProbeGather && bounceStore)
    {
        const std::shared_ptr<Camera>& primaryCam =
            cameras.empty() ? scene.camera : cameras.front();
        // Bounce-index cell size = the gather footprint scale, so a radius-r
        // query touches a 3x3x3 neighborhood. Reuse the scene-depth footprint.
        double gatherCell = cellSize;
        if (sceneDepthFootprint > 0.0)
        {
            gatherCell = sceneDepthFootprint;
        }
        (void)primaryCam;

        // Unified fixture visibility: deposit each area light's own surface
        // radiance as raw bounces on its surface (kept-near-probe like every other
        // bounce), so the probe gather renders the lit fixture exactly like any
        // other surface — visible directly AND in mirrors — with no special-case
        // pass. Done BEFORE buildIndex so the deposits enter the spatial index.
        // Spacing is tied to the gather footprint so several deposits land in each
        // gather disc.
        if (probeIndex)
        {
            const double depositSpacing = std::max(gatherCell * 0.5, 1e-6);
            const ProbeGather::EmitterDepositResult emit =
                ProbeGather::depositEmitters(scene.objects, *probeIndex,
                                             depositSpacing, *bounceStore);
            result.emitterDepositsKept = emit.kept;
        }

        bounceStore->buildIndex(gatherCell);
        result.bounceStore = bounceStore;
    }

    // MULTI-CAMERA: the photon pass / splat / grid above are a SINGLE shared solve.
    // Each camera already has its own splat buffer (direct image). Now composite
    // the MIRROR GATHER into that buffer's black delta pixels — reflected radiance
    // looked up from the shared density grid — and tonemap into the camera image.
    size_t splatIndex = 0;
    for (const auto& cam : cameras)
    {
        if (!cam)
        {
            continue;
        }

        std::shared_ptr<Buffer> imageBuffer =
            (splatIndex < splatBuffers.size()) ? splatBuffers[splatIndex] : nullptr;
        ++splatIndex;

        CameraRender cr;
        cr.camera = cam;
        cr.outputName = cam->outputName();

        // Mirror gather: fill the delta (mirror) pixels by reflecting the camera
        // ray through specular surfaces and looking up the density grid at the
        // first non-delta point. Composites directly into the splat buffer (only
        // touches currently-black delta pixels). Skipped for debug cameras with a
        // bounce/light filter — those isolate direct deposits, not reflections.
        const bool debugCamera = (cam->bounceFilter() >= 0) || (cam->lightFilter() >= 0);

        const std::chrono::time_point gatherStart = std::chrono::system_clock::now();
        if (settings.useProbeGather && bounceStore)
        {
            // Phase 2a UNIFIED GATHER: one path renders both directly-visible
            // diffuse (extension depth 0) AND reflected/refracted diffuse
            // (extension depth > 0) from the retained raw bounces. Replaces the
            // splat (direct) + MirrorGather/density-grid (reflections). The gather
            // OWNS the whole image, so it writes into the (cleared) buffer.
            if (imageBuffer && !debugCamera)
            {
                cr.probe = ProbeGather::run(
                    scene.objects,
                    cam,
                    *bounceStore,
                    *scene.materialLibrary,
                    animationQuery.get(),
                    settings.workerCount,
                    probeGatherMinRadius,
                    *imageBuffer);
            }
            // Light fixtures are NOT a separate pass in probe mode: each emitter
            // deposited its own surface radiance as raw bounces (depositEmitters
            // above), so ProbeGather::run renders the fixture — directly AND in
            // mirrors — like any other gathered surface. The legacy EmissiveGather
            // path is used only by the $probeGather false branch below.
        }
        else
        {
            if (imageBuffer && !debugCamera)
            {
                cr.mirror = MirrorGather::run(
                    scene.objects,
                    cam,
                    *densityGrid,
                    *scene.materialLibrary,
                    animationQuery.get(),
                    static_cast<double>(settings.photonsPerLight),
                    settings.workerCount,
                    *imageBuffer);
            }
            // Emissive gather: make light fixtures camera-visible at their true
            // surface radiance L = M/pi. Treats each emitter as a surface whose
            // outgoing radiance the camera reads (no primary-ray-vs-light special
            // case in the tracer) and writes it into the pixels the fixture is
            // visible in. Composites into the same buffer; skipped for debug
            // cameras (which isolate deposits, not direct emitter visibility).
            if (imageBuffer && !debugCamera)
            {
                cr.emissive = EmissiveGather::run(
                    scene.objects,
                    cam,
                    animationQuery.get(),
                    settings.workerCount,
                    *imageBuffer);
            }
        }
        const std::chrono::time_point gatherEnd = std::chrono::system_clock::now();

        cr.gatherSeconds = std::chrono::duration<double>(gatherEnd - gatherStart).count();
        cr.buffer = imageBuffer;

        // Tonemap the composited buffer (direct splat + mirror reflections) through
        // THIS camera's exposure into a fresh image at the camera's resolution.
        cr.image = std::make_shared<Image>(cam->width(), cam->height());
        cr.image->clear();
        if (imageBuffer)
        {
            tonemapBufferToImage(*imageBuffer, *cr.image, photonsEmitted,
                                 cam->saturationLuminance());

            double sum = 0.0;
            const size_t w = cam->width();
            const size_t h = cam->height();
            for (size_t y = 0; y < h; ++y)
            {
                for (size_t x = 0; x < w; ++x)
                {
                    const Color c = imageBuffer->fetchColor({x, y});
                    sum += (static_cast<double>(c.red) + c.green + c.blue) / 3.0;
                }
            }
            const double pixels = static_cast<double>(w) * static_cast<double>(h);
            cr.meanLuminance = (pixels > 0.0) ? (sum / pixels) : 0.0;
        }

        result.cameras.push_back(std::move(cr));
    }

    // Back-compat: surface the PRIMARY (first) camera's buffer/image + mirror
    // diagnostics on the top-level RenderResult fields existing callers read.
    if (!result.cameras.empty())
    {
        const CameraRender& primary = result.cameras.front();
        result.mirror = primary.mirror;
        if (primary.buffer)
        {
            result.buffer = primary.buffer;
        }
        if (primary.image)
        {
            result.image = primary.image;
        }
    }

    return result;
}

}
