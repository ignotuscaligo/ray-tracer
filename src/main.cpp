#define TINYOBJLOADER_IMPLEMENTATION

#include "Buffer.h"
#include "Camera.h"
#include "DiffuseMaterial.h"
#include "Image.h"
#include "LightQueue.h"
#include "Material.h"
#include "MeshVolume.h"
#include "Object.h"
#include "ObjReader.h"
#include "OmniLight.h"
#include "Photon.h"
#include "Pixel.h"
#include "Plane.h"
#include "PlaneVolume.h"
#include "PngWriter.h"
#include "Pyramid.h"
#include "Quaternion.h"
#include "Utility.h"
#include "Worker.h"
#include "WorkQueue.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <string>
#include <thread>

namespace
{

constexpr size_t million = 1000000;

constexpr size_t queueSize = 5 * million;
constexpr size_t photonsPerLight = 1 * million;
constexpr size_t workerCount = 32;
constexpr size_t fetchSize = 100000;

constexpr size_t startFrame = 0;
constexpr size_t frameCount = 24 * 10;
constexpr size_t renderFrameCount = 1;

constexpr size_t imageWidth = 512;
constexpr size_t imageHeight = 512;
constexpr float verticalFieldOfView = 80.0f;

const std::string renderPath = "C:\\Users\\ekleeman\\repos\\ray-tracer\\renders";
const std::string outputName = "cleanup_0";

}

int main(int argc, char** argv)
{
    std::cout << "Hello!" << std::endl;

    try
    {
        std::cout << "---" << std::endl;
        std::cout << "Setting up scene for render" << std::endl;

        std::string inputFile = R"(C:\Users\ekleeman\Documents\Cinema 4D\eschers_knot.obj)";

        std::shared_ptr<MaterialLibrary> materialLibrary = std::make_shared<MaterialLibrary>();

        materialLibrary->addMaterial(std::make_shared<DiffuseMaterial>("Default"));
        materialLibrary->addMaterial(std::make_shared<DiffuseMaterial>("Knot", Color(1.0f, 1.0f, 1.0f)));
        materialLibrary->addMaterial(std::make_shared<DiffuseMaterial>("Ground", Color(1.0f, 1.0f, 1.0f)));

        std::vector<std::shared_ptr<Object>> objects;

        std::shared_ptr<Object> root = objects.emplace_back(std::make_shared<Object>());
        std::shared_ptr<Object> cameraPivot = objects.emplace_back(std::make_shared<Object>());
        std::shared_ptr<Camera> camera = std::static_pointer_cast<Camera>(objects.emplace_back(std::make_shared<Camera>(imageWidth, imageHeight, verticalFieldOfView)));
        std::shared_ptr<Object> objectPivot = objects.emplace_back(std::make_shared<Object>());
        std::shared_ptr<Object> sun = objects.emplace_back(std::make_shared<Object>());
        std::shared_ptr<Object> knotMesh = objects.emplace_back(std::make_shared<MeshVolume>(materialLibrary->indexForName("Knot"), ObjReader::loadMesh(inputFile)));
        std::shared_ptr<Object> ground = objects.emplace_back(std::make_shared<PlaneVolume>(materialLibrary->indexForName("Ground")));
        std::shared_ptr<OmniLight> omniLight0 = std::static_pointer_cast<OmniLight>(objects.emplace_back(std::make_shared<OmniLight>()));
        std::shared_ptr<OmniLight> omniLight1 = std::static_pointer_cast<OmniLight>(objects.emplace_back(std::make_shared<OmniLight>()));
        std::shared_ptr<OmniLight> omniLight2 = std::static_pointer_cast<OmniLight>(objects.emplace_back(std::make_shared<OmniLight>()));

        Object::setParent(cameraPivot, root);
        Object::setParent(camera, cameraPivot);
        Object::setParent(sun, cameraPivot);
        Object::setParent(objectPivot, root);
        Object::setParent(ground, root);
        Object::setParent(knotMesh, objectPivot);
        Object::setParent(omniLight0, root);
        Object::setParent(omniLight1, root);
        Object::setParent(omniLight2, root);

        ground->transform.position = {0, -70, 0};

        omniLight0->transform.position = {0, 70, 50};
        omniLight0->color(Color::fromRGB(255, 0, 0));
        omniLight0->brightness(300000);
        omniLight0->innerRadius(5.0f);

        omniLight1->transform.position = {43.3f, 70, -25};
        omniLight1->color(Color::fromRGB(0, 255, 0));
        omniLight1->brightness(300000);
        omniLight1->innerRadius(5.0f);

        omniLight2->transform.position = {-43.3f, 70, -25};
        omniLight2->color(Color::fromRGB(0, 0, 255));
        omniLight2->brightness(300000);
        omniLight2->innerRadius(5.0f);

        camera->transform.position = {0.0f, 0.0f, 100.0f};
        camera->transform.rotation = Quaternion::fromPitchYawRoll(Utility::radians(-10), Utility::radians(180), 0);

        sun->transform.rotation = Quaternion::fromPitchYawRoll(Utility::radians(45.0f), Utility::radians(45.0f), 0.0f);

        std::shared_ptr<Buffer> buffer = std::make_shared<Buffer>(imageWidth, imageHeight);

        std::shared_ptr<Image> image = std::make_shared<Image>(imageWidth, imageHeight);
        Pixel workingPixel;

        const size_t pixelCount = image->width() * image->height();

        float pitchStep = camera->verticalFieldOfView() / static_cast<float>(image->height());
        float yawStep = camera->horizontalFieldOfView() / static_cast<float>(image->width());

        std::cout << "---" << std::endl;
        std::cout << "Rendering image at " << image->width() << " px by " << image->height() << " px" << std::endl;

        std::shared_ptr<LightQueue> lightQueue = std::make_shared<LightQueue>();
        std::shared_ptr<WorkQueue<Photon>> photonQueue = std::make_shared<WorkQueue<Photon>>(queueSize);
        std::shared_ptr<WorkQueue<PhotonHit>> hitQueue = std::make_shared<WorkQueue<PhotonHit>>(queueSize);
        std::shared_ptr<WorkQueue<PhotonHit>> finalHitQueue = std::make_shared<WorkQueue<PhotonHit>>(queueSize);

        std::shared_ptr<Worker> workers[workerCount];

        for (size_t i = 0; i < workerCount; ++i)
        {
            workers[i] = std::make_shared<Worker>(i, fetchSize);
            workers[i]->camera = camera;
            workers[i]->objects = objects;
            workers[i]->photonQueue = photonQueue;
            workers[i]->hitQueue = hitQueue;
            workers[i]->finalHitQueue = finalHitQueue;
            workers[i]->buffer = buffer;
            workers[i]->image = image;
            workers[i]->materialLibrary = materialLibrary;
            workers[i]->lightQueue = lightQueue;
        }

        std::thread threads[workerCount];

        for (size_t i = 0; i < workerCount; ++i)
        {
            workers[i]->start();

            // TODO: move to Worker
            threads[i] = std::thread([&workers, i]() {
                // std::cout << "running thread" << std::endl;
                workers[i]->exec();
            });
        }

        float rotationStep = 360.0f / static_cast<float>(frameCount);

        for (size_t frame = startFrame; frame < startFrame + renderFrameCount; ++frame)
        {
            std::cout << "---" << std::endl;
            std::cout << "Rendering frame " << frame + 1 << " / " << startFrame + renderFrameCount << std::endl;

            std::chrono::time_point renderStart = std::chrono::system_clock::now();

            std::cout << "---" << std::endl;
            std::cout << "Clearing buffer and image" << std::endl;

            buffer->clear();
            image->clear();

            std::cout << "---" << std::endl;
            std::cout << "Animating objects" << std::endl;

            // objectPivot->transform.rotation = Quaternion::fromPitchYawRoll(0, Utility::radians(frame * rotationStep), 0);

            knotMesh->transform.rotation = Quaternion::fromPitchYawRoll(Utility::radians(frame * -rotationStep), Utility::radians(frame * rotationStep), Utility::radians(frame * rotationStep * 2));
            // knotMesh->transform.rotation = Quaternion::fromPitchYawRoll(0, 0, Utility::radians(frame * rotationStep * 2));

            // omniLight1->transform.position = {-40, frame * (-18.0f / static_cast<float>(frameCount)), -40};

            std::cout << "---" << std::endl;
            std::cout << "Initializing lights" << std::endl;

            size_t lightCount = 0;

            for (const auto& object : objects)
            {
                if (object->hasType<Light>())
                {
                    lightQueue->setPhotonCount(object->name(), photonsPerLight);
                    ++lightCount;
                }
            }

            std::cout << "---" << std::endl;
            std::cout << "Processing photons" << std::endl;

            size_t photonsToEmit = lightQueue->remainingPhotons();
            size_t photonsAllocated = photonQueue->allocated();
            size_t hitsAllocated = hitQueue->allocated();
            size_t finalHitsAllocated = finalHitQueue->allocated();

            std::cout << "---" << std::endl;
            std::cout << "remaining emissions:  " << photonsToEmit << std::endl;
            std::cout << "remaining photons:    " << photonsAllocated << std::endl;
            std::cout << "remaining hits:       " << hitsAllocated << std::endl;
            std::cout << "remaining final hits: " << finalHitsAllocated << std::endl;

            size_t lastEmit = photonsToEmit;
            size_t lastPhotons = photonsAllocated;
            size_t lastHits = hitsAllocated;
            size_t lastFinalHits = finalHitsAllocated;

            while (photonsAllocated > 0 || hitsAllocated > 0 || finalHitsAllocated > 0 || photonsToEmit > 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));

                photonsToEmit = lightQueue->remainingPhotons();
                photonsAllocated = photonQueue->allocated();
                hitsAllocated = hitQueue->allocated();
                finalHitsAllocated = finalHitQueue->allocated();

                if (photonsAllocated != lastPhotons || hitsAllocated != lastHits || finalHitsAllocated != lastFinalHits || photonsToEmit != lastEmit)
                {
                    std::cout << "---" << std::endl;
                    std::cout << "remaining emissions:  " << photonsToEmit << std::endl;
                    std::cout << "remaining photons:    " << photonsAllocated << std::endl;
                    std::cout << "remaining hits:       " << hitsAllocated << std::endl;
                    std::cout << "remaining final hits: " << finalHitsAllocated << std::endl;
                }

                lastEmit = photonsToEmit;
                lastPhotons = photonsAllocated;
                lastHits = hitsAllocated;
                lastFinalHits = finalHitsAllocated;
            }

            std::cout << "---" << std::endl;
            std::cout << "Writing buffer to image" << std::endl;

            std::chrono::time_point writeImageStart = std::chrono::system_clock::now();

            for (size_t y = 0; y < imageHeight; ++y)
            {
                for (size_t x = 0; x < imageWidth; ++x)
                {
                    Color color = buffer->fetchColor({x, y});

                    float gammaRed = std::pow(color.red, 1.0f / Color::gamma);
                    float gammaGreen = std::pow(color.green, 1.0f / Color::gamma);
                    float gammaBlue = std::pow(color.blue, 1.0f / Color::gamma);

                    workingPixel.red = std::min(static_cast<int>(gammaRed * 65535), 65535);
                    workingPixel.green = std::min(static_cast<int>(gammaGreen * 65535), 65535);
                    workingPixel.blue = std::min(static_cast<int>(gammaBlue * 65535), 65535);

                    image->setPixel((imageWidth - 1) - x, (imageHeight - 1) - y, workingPixel);
                }
            }

            PngWriter::writeImage(renderPath + "\\" + outputName + "." + std::to_string(frame) + ".png", *image, outputName);

            std::chrono::time_point writeImageEnd = std::chrono::system_clock::now();
            std::chrono::microseconds writeImageDuration = std::chrono::duration_cast<std::chrono::microseconds>(writeImageEnd - writeImageStart);

            std::cout << "---" << std::endl;
            std::cout << "Collecting metrics" << std::endl;

            size_t emitProcessed = 0;
            size_t photonsProcessed = 0;
            size_t hitsProcessed = 0;
            size_t finalHitsProcessed = 0;

            size_t emitDuration = 0;
            size_t photonDuration = 0;
            size_t hitDuration = 0;
            size_t writeDuration = 0;

            for (size_t i = 0; i < workerCount; ++i)
            {
                emitProcessed += workers[i]->emitProcessed;
                photonsProcessed += workers[i]->photonsProcessed;
                hitsProcessed += workers[i]->hitsProcessed;
                finalHitsProcessed += workers[i]->finalHitsProcessed;
                workers[i]->emitProcessed = 0;
                workers[i]->photonsProcessed = 0;
                workers[i]->hitsProcessed = 0;
                workers[i]->finalHitsProcessed = 0;

                emitDuration += workers[i]->emitDuration;
                photonDuration += workers[i]->photonDuration;
                hitDuration += workers[i]->hitDuration;
                writeDuration += workers[i]->writeDuration;
                workers[i]->emitDuration = 0;
                workers[i]->photonDuration = 0;
                workers[i]->hitDuration = 0;
                workers[i]->writeDuration = 0;
            }

            size_t totalDuration = photonDuration + hitDuration + writeDuration;

            std::cout << "---" << std::endl;
            std::cout << "Finished" << std::endl;

            std::chrono::time_point renderEnd = std::chrono::system_clock::now();
            std::chrono::microseconds renderDuration = std::chrono::duration_cast<std::chrono::microseconds>(renderEnd - renderStart);

            std::cout << "---" << std::endl;
            std::cout << "Render time:" << std::endl;
            std::cout << "|- total:        " << renderDuration.count() / 1000 << " ms" << std::endl;
            std::cout << "|- average / px: " << renderDuration.count() / pixelCount << " us" << std::endl;

            std::cout << "Lights:" << std::endl;
            std::cout << "|- processed:             " << emitProcessed << std::endl;
            std::cout << "|- process total time:    " << emitDuration << " us" << std::endl;
            std::cout << "|- process time / worker: " << emitDuration / workerCount << " us" << std::endl;

            std::cout << "Photons:" << std::endl;
            std::cout << "|- processed:             " << photonsProcessed << std::endl;
            std::cout << "|- process total time:    " << photonDuration << " us" << std::endl;
            std::cout << "|- process time / worker: " << photonDuration / workerCount << " us" << std::endl;

            std::cout << "Hits:" << std::endl;
            std::cout << "|- processed:             " << hitsProcessed << std::endl;
            std::cout << "|- process total time:    " << hitDuration << " us" << std::endl;
            std::cout << "|- process time / worker: " << hitDuration / workerCount << " us" << std::endl;

            std::cout << "Final hits:" << std::endl;
            std::cout << "|- processed:             " << finalHitsProcessed << std::endl;
            std::cout << "|- process total time:    " << writeDuration << " us" << std::endl;
            std::cout << "|- process time / worker: " << writeDuration / workerCount << " us" << std::endl;

            std::cout << "Image write:" << std::endl;
            std::cout << "|- duration: " << writeImageDuration.count() << " us" << std::endl;
        }

        for (size_t i = 0; i < workerCount; ++i)
        {
            workers[i]->stop();
        }

        for (size_t i = 0; i < workerCount; ++i)
        {
            threads[i].join();
        }
    }
    catch (const std::exception& e)
    {
        std::cout << "ERROR: " << e.what() << std::endl;
    }

    std::cout << "Goodbye!" << std::endl;

    return 0;
}
