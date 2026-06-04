#include "Renderer.h"

#include "AnimationQuery.h"
#include "Color.h"
#include "EmitterQueue.h"
#include "Gather.h"
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

    // Wave 4a: allocate the persistent deposit cloud before the workers start.
    // Capacity = photonsPerLight * budgetFactor, clamped to the configured max.
    // The whole buffer is preallocated so worker appends are a lock-free atomic
    // fetch-add into a buffer that never reallocates during the pass.
    const size_t cloudBudget = std::min(
        settings.bounceCloudMaxRecords,
        static_cast<size_t>(static_cast<double>(settings.photonsPerLight) * settings.bounceCloudBudgetFactor));
    std::shared_ptr<BounceCloud> bounceCloud = std::make_shared<BounceCloud>(cloudBudget);

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
        worker->bounceCloud = bounceCloud;
        worker->setBounceThreshold(settings.bounceThreshold);
        worker->setRussianRoulette(Worker::RussianRouletteConfig{
            settings.russianRoulette,
            settings.russianRouletteMinBounces,
            settings.russianRouletteMinProbability,
            settings.russianRouletteReferenceEnergy,
        });
        worker->setDaughterCount(settings.daughterCountOverride, settings.daughterCountScale);
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

    // Wave 4b: the forward splat is GONE — `buffer` (the camera-as-accumulator
    // sink the workers used to splat into) is no longer the image source and is
    // not tonemapped here. The photon pass still ran, but only to fill the cloud.
    // The image is produced by the gather below. We keep the exposure parameters
    // for that tonemap.
    const double photonsEmitted = static_cast<double>(settings.photonsPerLight);
    const double saturationLuminance = scene.camera ? scene.camera->saturationLuminance() : 0.0;

    // Wave 3 memory evidence: high-water-mark occupancy of each queue.
    result.peakPhotonQueue = photonQueue->largestAllocated();
    result.peakHitQueue = hitQueue->largestAllocated();
    result.peakEmitterQueue = emitterQueue->largestAllocated();
    result.peakFinalQueue = finalHitQueue->largestAllocated();

    // Wave 4a/4b: the photon pass has fully drained and all worker threads are
    // joined (workers[i]->stop() above), so every deposit append has completed
    // and is visible here. Build the spatial hash grid over the deposited points,
    // then run the GATHER pass — which is now the SOLE image source (the forward
    // splat is removed from the Worker pipeline).
    result.bounceCloud = bounceCloud;

    // Hash-grid cell size: a radiusSearch(p, r) should touch roughly a 3x3x3
    // neighborhood, i.e. cellSize ~= the typical gather radius. The gather radius
    // r = depth * tan(pixelHalfAngle) varies per pixel (depth-dependent), so we
    // pick a REPRESENTATIVE radius: the camera-to-scene-center depth times the
    // pixel half-angle. Scene center is the centroid of the scene Volumes; depth
    // is its distance from the camera. This scales correctly with resolution
    // (finer pixels -> smaller cells) and roughly tracks the per-pixel radius for
    // the bulk of the frame. Pixels closer than the centroid query a sub-cell
    // radius (touch ~1 cell — still correct, just fewer candidates); pixels deeper
    // query a slightly-larger-than-cell radius (touch ~3x3x3). Falls back to the
    // configured hashGridCellSize when the scene has no resolvable depth.
    double cellSize = settings.hashGridCellSize;
    if (scene.camera && settings.imageHeight > 0)
    {
        Vector centroid{0.0, 0.0, 0.0};
        size_t volumeCount = 0;
        for (const auto& object : scene.objects)
        {
            if (object->hasType<Volume>())
            {
                const Vector p = object->position();
                centroid = centroid + p;
                ++volumeCount;
            }
        }
        if (volumeCount > 0)
        {
            centroid = centroid / static_cast<double>(volumeCount);
            const double depth = (centroid - scene.camera->position()).magnitude();
            const double pixelHalfAngle =
                0.5 * Utility::radians(scene.camera->verticalFieldOfView()) /
                static_cast<double>(settings.imageHeight);
            const double representativeRadius = depth * std::tan(pixelHalfAngle);
            if (representativeRadius > 0.0)
            {
                cellSize = representativeRadius;
            }
        }
    }

    result.hashGrid = std::make_shared<HashGrid>(*bounceCloud, cellSize);

    // The shared photon pass + cloud + grid build is now complete. Everything below
    // is per-camera gather work (amortized across cameras), so freeze the shared
    // time here.
    const std::chrono::time_point photonPassEnd = std::chrono::system_clock::now();
    result.photonPassSeconds =
        std::chrono::duration<double>(photonPassEnd - photonPassStart).count();

    // Wave 6 MULTI-CAMERA: the photon pass / bounce cloud / hash grid above are a
    // SINGLE shared lighting solve. Now run the GATHER once per camera — each camera
    // produces its own image at its own resolution/exposure (and, for debug cameras,
    // its own deposit filter). A single-camera scene loops exactly once, matching the
    // Wave 4b/4c behaviour. Fall back to the primary camera if the scene only set the
    // back-compat `scene.camera` handle.
    std::vector<std::shared_ptr<Camera>> cameras = scene.cameras;
    if (cameras.empty() && scene.camera)
    {
        cameras.push_back(scene.camera);
    }

    result.cameras.reserve(cameras.size());

    for (const auto& cam : cameras)
    {
        if (!cam)
        {
            continue;
        }

        CameraRender cr;
        cr.camera = cam;
        cr.outputName = cam->outputName();

        const Gather::GatherFilters filters{cam->bounceFilter(), cam->lightFilter()};

        const std::chrono::time_point gatherStart = std::chrono::system_clock::now();
        Gather::GatherResult gather = Gather::run(
            scene.objects,
            cam,
            *bounceCloud,
            *result.hashGrid,
            *scene.materialLibrary,
            animationQuery.get(),
            static_cast<double>(settings.photonsPerLight),
            settings.workerCount,
            filters);
        const std::chrono::time_point gatherEnd = std::chrono::system_clock::now();

        cr.gatherSeconds = std::chrono::duration<double>(gatherEnd - gatherStart).count();
        cr.gather = gather;
        cr.buffer = gather.buffer;

        // Tonemap THIS camera's gather buffer through THIS camera's exposure into a
        // fresh image at the camera's own resolution.
        cr.image = std::make_shared<Image>(cam->width(), cam->height());
        cr.image->clear();
        if (gather.buffer)
        {
            tonemapBufferToImage(*gather.buffer, *cr.image, photonsEmitted,
                                 cam->saturationLuminance());

            // Mean pre-exposure luminance over the buffer — the Milestone 2
            // brightness-stability check (should be ~constant across resolutions as
            // the per-pixel footprint shrinks, validating the 1/(pi r^2) normalization).
            double sum = 0.0;
            const size_t w = cam->width();
            const size_t h = cam->height();
            for (size_t y = 0; y < h; ++y)
            {
                for (size_t x = 0; x < w; ++x)
                {
                    const Color c = gather.buffer->fetchColor({x, y});
                    sum += (static_cast<double>(c.red) + c.green + c.blue) / 3.0;
                }
            }
            const double pixels = static_cast<double>(w) * static_cast<double>(h);
            cr.meanLuminance = (pixels > 0.0) ? (sum / pixels) : 0.0;
        }

        result.cameras.push_back(std::move(cr));
    }

    // Back-compat: surface the PRIMARY (first) camera's gather/buffer/image on the
    // top-level RenderResult fields existing callers (editor, single-cam CLI) read.
    if (!result.cameras.empty())
    {
        const CameraRender& primary = result.cameras.front();
        result.gather = primary.gather;
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
