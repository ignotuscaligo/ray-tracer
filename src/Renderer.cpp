#include "Renderer.h"

#include "AnimationQuery.h"
#include "Color.h"
#include "EmittingQueue.h"
#include "LightQueue.h"
#include "Light.h"
#include "Photon.h"
#include "Pixel.h"
#include "Vector.h"
#include "WorkQueue.h"
#include "Worker.h"

#include <chrono>
#include <cmath>
#include <thread>

namespace Renderer
{

// Global footprint calibration constant standing in for the per-pixel footprint
// factor (solid angle / projected pixel area) that converts accumulated photon
// flux into physical luminance. SIMPLIFICATION: it is uniform across the frame
// rather than computed per pixel from FOV/depth. Its value is chosen so the
// reference MirrorTest scene, at its default light intensity and the camera's
// default (neutral) exposure, renders at roughly the prior look. See
// luminanceFromBuffer() for how it enters.
//
// raw buffer holds sum_over_hits(Phi); dividing by total photons N gives a
// Monte-Carlo flux estimate; multiplying by this footprint factor yields an
// estimate of physical luminance L (cd/m^2).
constexpr double kFootprintCalibration = 64.0;

// Convert one raw accumulated buffer channel into physical luminance. This is the
// ONE place the photon count enters the pipeline (the single 1/N normalization),
// combined with the per-pixel footprint factor.
//   L = rawChannel * (1 / photonsEmitted) * footprint
static double luminanceFromBuffer(double rawChannel, double photonsEmitted)
{
    if (photonsEmitted <= 0.0)
    {
        return 0.0;
    }
    return rawChannel * (kFootprintCalibration / photonsEmitted);
}

void tonemapBufferToImage(const Buffer& buffer, Image& image, double photonsEmitted, double saturationLuminance)
{
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

            // Step a: raw accumulated energy -> physical luminance (the single
            // divide-by-photon-count + footprint factor).
            const double lumR = luminanceFromBuffer(color.red, photonsEmitted);
            const double lumG = luminanceFromBuffer(color.green, photonsEmitted);
            const double lumB = luminanceFromBuffer(color.blue, photonsEmitted);

            // Step b: luminance -> [0,1] via the photographic saturation exposure.
            const double exposedR = lumR * invLmax;
            const double exposedG = lumG * invLmax;
            const double exposedB = lumB * invLmax;

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

    RenderResult result;
    result.buffer = std::make_shared<Buffer>(settings.imageWidth, settings.imageHeight);
    result.image = std::make_shared<Image>(settings.imageWidth, settings.imageHeight);

    std::shared_ptr<Buffer> buffer = result.buffer;
    std::shared_ptr<Image> image = result.image;

    std::shared_ptr<LightQueue> lightQueue = std::make_shared<LightQueue>();
    std::shared_ptr<WorkQueue<Photon>> photonQueue = std::make_shared<WorkQueue<Photon>>(settings.photonQueueSize);
    std::shared_ptr<WorkQueue<PhotonHit>> hitQueue = std::make_shared<WorkQueue<PhotonHit>>(settings.hitQueueSize);
    std::shared_ptr<EmittingQueue> emittingQueue = std::make_shared<EmittingQueue>(settings.emittingQueueSize);
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

    std::vector<std::shared_ptr<Worker>> workers{settings.workerCount};

    size_t workerIndex = 0;
    for (auto& worker : workers)
    {
        worker = std::make_shared<Worker>(workerIndex, settings.fetchSize);
        worker->camera = scene.camera;
        worker->objects = scene.objects;
        worker->photonQueue = photonQueue;
        worker->hitQueue = hitQueue;
        worker->emittingQueue = emittingQueue;
        worker->finalHitQueue = finalHitQueue;
        worker->buffer = buffer;
        worker->image = image;
        worker->materialLibrary = scene.materialLibrary;
        worker->lightQueue = lightQueue;
        worker->animationQuery = animationQuery;
        worker->setBounceThreshold(settings.bounceThreshold);
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
    size_t emittingAllocated = emittingQueue->allocated();
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
        emittingAllocated = emittingQueue->allocated();
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

    // Wave 2: convert raw energy -> image with the physical pipeline. The single
    // 1/N normalization (N = photonsPerLight) and the camera's saturation
    // exposure L_max both enter here, once.
    const double photonsEmitted = static_cast<double>(settings.photonsPerLight);
    const double saturationLuminance = scene.camera ? scene.camera->saturationLuminance() : 0.0;
    tonemapBufferToImage(*buffer, *image, photonsEmitted, saturationLuminance);

    return result;
}

}
