#include "Renderer.h"

#include "AnimationQuery.h"
#include "Color.h"
#include "DensityGrid.h"
#include "EmissiveGather.h"
#include "EmitterQueue.h"
#include "MirrorGather.h"
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

RenderResult renderFrame(const LoadedScene& scene, ProgressCallback progress)
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
    std::shared_ptr<WorkQueue<PhotonHit>> hitQueue = std::make_shared<WorkQueue<PhotonHit>>(settings.hitQueueSize);
    std::shared_ptr<EmitterQueue> emitterQueue = std::make_shared<EmitterQueue>(settings.emittingQueueSize);
    std::shared_ptr<WorkQueue<PhotonHit>> finalHitQueue = std::make_shared<WorkQueue<PhotonHit>>(settings.finalQueueSize);

    // Continuous-time animation oracle. Default is static; if the scene contains
    // an object named "MirrorSphere", attach a translation animation so the
    // motion-blur pipeline can be exercised end-to-end. This wiring is preserved
    // verbatim from the pre-refactor executable to keep render output identical.
    std::shared_ptr<AnimationQuery> animationQuery = std::make_shared<StaticAnimationQuery>();
    for (auto& object : scene.objects)
    {
        if (object->name() == "MirrorSphere")
        {
            animationQuery = std::make_shared<TranslatingAnimationQuery>(
                object->name(),
                object->position(),
                object->rotation(),
                Vector{40.0, 0.0, 0.0});  // 40 world-units per second along +X
            break;
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

    std::vector<std::shared_ptr<Worker>> workers{settings.workerCount};

    size_t workerIndex = 0;
    for (auto& worker : workers)
    {
        worker = std::make_shared<Worker>(workerIndex, settings.fetchSize);
        worker->camera = scene.camera;
        worker->objects = scene.objects;
        worker->photonQueue = photonQueue;
        worker->hitQueue = hitQueue;
        worker->emitterQueue = emitterQueue;
        worker->finalHitQueue = finalHitQueue;
        worker->buffer = buffer;
        worker->image = image;
        worker->materialLibrary = scene.materialLibrary;
        worker->lightQueue = lightQueue;
        worker->animationQuery = animationQuery;
        worker->densityGrid = densityGrid;
        worker->setBounceThreshold(settings.bounceThreshold);
        worker->setTerminationFraction(settings.terminationFraction);
        worker->setRussianRoulette(Worker::RussianRouletteConfig{
            settings.russianRoulette,
            settings.russianRouletteMinBounces,
            settings.russianRouletteMinProbability,
            settings.russianRouletteReferenceEnergy,
        });
        worker->setDaughterCount(settings.daughterCountOverride, settings.daughterCountScale);
        worker->setPhotonsPerLight(static_cast<double>(settings.photonsPerLight));
        worker->setMinSplatRadius(minSplatRadius);
        worker->setSplatLuminanceClamp(settings.splatLuminanceClamp);
        worker->setSplatTargets(splatTargets);
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
    size_t hitsAllocated = hitQueue->allocated();
    size_t emittingAllocated = emitterQueue->allocated();
    size_t finalHitsAllocated = finalHitQueue->allocated();

    std::exception_ptr workerException;
    bool aborted = false;

    // Per-worker claim-output-first overflow: hits a worker has dequeued/produced
    // but not yet been able to enqueue downstream. This work lives outside the
    // shared queues, so it MUST be part of the completion test — otherwise the
    // loop could observe every queue empty while a worker still holds parked hits
    // and stop the pipeline before those hits are splatted.
    auto pendingOverflowTotal = [&workers]() {
        size_t total = 0;
        for (auto& worker : workers)
        {
            total += worker->pendingOverflow();
        }
        return total;
    };
    size_t overflowPending = pendingOverflowTotal();

    while (photonsAllocated > 0 || hitsAllocated > 0 || emittingAllocated > 0 || finalHitsAllocated > 0 || photonsToEmit > 0 || overflowPending > 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        photonsToEmit = lightQueue->remainingPhotons();
        photonsAllocated = photonQueue->allocated();
        hitsAllocated = hitQueue->allocated();
        emittingAllocated = emitterQueue->allocated();
        finalHitsAllocated = finalHitQueue->allocated();
        overflowPending = pendingOverflowTotal();

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

        if (progress)
        {
            const size_t remainingWork = photonsToEmit + photonsAllocated + hitsAllocated + emittingAllocated + finalHitsAllocated;
            if (!progress(remainingWork))
            {
                aborted = true;
                break;
            }
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

    // Even on a caller-requested abort, tonemap whatever has accumulated so the
    // partial result is usable (the editor's progressive/preview path relies on
    // this).
    (void)aborted;

    // Storage pivot: the photon pass produced the DIRECT image via the forward
    // SPLAT (into each camera's splat buffer — no per-photon storage) and filled
    // the compact DENSITY GRID with non-delta bounce energy for reflections. We
    // keep exposure parameters for the per-camera tonemap.
    const double photonsEmitted = static_cast<double>(settings.photonsPerLight);

    // Wave 3 memory evidence: high-water-mark occupancy of each queue.
    result.peakPhotonQueue = photonQueue->largestAllocated();
    result.peakHitQueue = hitQueue->largestAllocated();
    result.peakEmitterQueue = emitterQueue->largestAllocated();
    result.peakFinalQueue = finalHitQueue->largestAllocated();

    // The compact reflection store (bounded by occupied cells, not photon count).
    result.densityGrid = densityGrid;

    // The shared photon pass (emit + splat + grid fill) is complete. Everything
    // below is per-camera mirror-gather work, so freeze the shared time here.
    const std::chrono::time_point photonPassEnd = std::chrono::system_clock::now();
    result.photonPassSeconds =
        std::chrono::duration<double>(photonPassEnd - photonPassStart).count();

    result.cameras.reserve(cameras.size());

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
        // visible in. Composites into the same buffer; skipped for debug cameras
        // (which isolate deposits, not direct emitter visibility).
        if (imageBuffer && !debugCamera)
        {
            cr.emissive = EmissiveGather::run(
                scene.objects,
                cam,
                animationQuery.get(),
                settings.workerCount,
                *imageBuffer);
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
