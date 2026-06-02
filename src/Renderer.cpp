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

void tonemapBufferToImage(const Buffer& buffer, Image& image)
{
    const size_t width = image.width();
    const size_t height = image.height();

    Pixel workingPixel;

    for (size_t y = 0; y < height; ++y)
    {
        for (size_t x = 0; x < width; ++x)
        {
            const Color color = buffer.fetchColor({x, y});

            const float gammaRed = std::pow(color.red, 1.0f / Color::gamma);
            const float gammaGreen = std::pow(color.green, 1.0f / Color::gamma);
            const float gammaBlue = std::pow(color.blue, 1.0f / Color::gamma);

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

    // Seed the light queue.
    for (const auto& object : scene.objects)
    {
        if (object->hasType<Light>())
        {
            lightQueue->setPhotonCount(object->name(), settings.photonsPerLight);
        }
    }

    size_t photonsToEmit = lightQueue->remainingPhotons();
    size_t photonsAllocated = photonQueue->allocated();
    size_t hitsAllocated = hitQueue->allocated();
    size_t emittingAllocated = emittingQueue->allocated();
    size_t finalHitsAllocated = finalHitQueue->allocated();

    std::exception_ptr workerException;
    bool aborted = false;

    while (photonsAllocated > 0 || hitsAllocated > 0 || emittingAllocated > 0 || finalHitsAllocated > 0 || photonsToEmit > 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        photonsToEmit = lightQueue->remainingPhotons();
        photonsAllocated = photonQueue->allocated();
        hitsAllocated = hitQueue->allocated();
        emittingAllocated = emittingQueue->allocated();
        finalHitsAllocated = finalHitQueue->allocated();

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

    tonemapBufferToImage(*buffer, *image);

    return result;
}

}
