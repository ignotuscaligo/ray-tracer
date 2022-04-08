#define TINYOBJLOADER_IMPLEMENTATION

#include "Buffer.h"
#include "Camera.h"
#include "DiffuseMaterial.h"
#include "Image.h"
#include "LightQueue.h"
#include "Material.h"
#include "MaterialLibrary.h"
#include "MeshLibrary.h"
#include "MeshVolume.h"
#include "Object.h"
#include "ObjReader.h"
#include "OmniLight.h"
#include "ParallelLight.h"
#include "Photon.h"
#include "Pixel.h"
#include "Plane.h"
#include "PlaneVolume.h"
#include "PngWriter.h"
#include "Pyramid.h"
#include "Quaternion.h"
#include "SpotLight.h"
#include "Utility.h"
#include "Worker.h"
#include "WorkQueue.h"

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <thread>

using namespace nlohmann;

namespace
{

constexpr size_t million = 1000000;
constexpr size_t thousand = 1000;

constexpr size_t photonQueueSize = 20 * million;
constexpr size_t hitQueueSize = 5 * million;
constexpr size_t finalQueueSize = 100 * thousand;
constexpr size_t photonsPerLight = 20 * million;
constexpr size_t workerCount = 32;
constexpr size_t fetchSize = 100000;

constexpr size_t frameCount = 24 * 10;

constexpr size_t imageWidth = 1080;
constexpr size_t imageHeight = 1080;
constexpr double verticalFieldOfView = 90.0f;

const std::string renderPath = "C:\\Users\\ekleeman\\repos\\ray-tracer\\renders";
const std::string outputName = "simple_room";

struct ProjectConfiguration
{
    size_t photonQueueSize = 20 * million;
    size_t hitQueueSize = 5 * million;
    size_t finalQueueSize = 100 * thousand;
    size_t photonsPerLight = 20 * million;
    size_t workerCount = 32;
    size_t fetchSize = 100000;
    size_t imageWidth = 1080;
    size_t imageHeight = 1080;
    size_t startFrame = 0;
    size_t endFrame = 0;
};

template<typename T>
void setFromJsonIfPresent(T& output, json jsonContainer, const std::string& key)
{
    if (jsonContainer.contains(key))
    {
        output = jsonContainer[key].get<T>();
        std::cout << "  Setting " << key << " to " << output << std::endl;
    }
}

}

int main(int argc, char** argv)
{
    std::cout << "Hello!" << std::endl;

    ProjectConfiguration config;

    try
    {
        std::cout << "Loading project file test.json" << std::endl;
        std::ifstream jsonFile("C:\\Users\\ekleeman\\repos\\ray-tracer\\test.json");
        json jsonData = json::parse(jsonFile);
        jsonFile.close();

        if (jsonData.contains("$workerConfiguration"))
        {
            std::cout << "---" << std::endl;
            std::cout << "Parsing $workerConfiguration" << std::endl;
            json& workerConfiguration = jsonData["$workerConfiguration"];
            setFromJsonIfPresent(config.workerCount, workerConfiguration, "$workerCount");
            setFromJsonIfPresent(config.fetchSize, workerConfiguration, "$fetchSize");
            setFromJsonIfPresent(config.photonQueueSize, workerConfiguration, "$photonQueueSize");
            setFromJsonIfPresent(config.hitQueueSize, workerConfiguration, "$hitQueueSize");
            setFromJsonIfPresent(config.finalQueueSize, workerConfiguration, "$finalQueueSize");
        }

        if (jsonData.contains("$renderConfiguration"))
        {
            std::cout << "---" << std::endl;
            std::cout << "Parsing $renderConfiguration" << std::endl;
            json& renderConfiguration = jsonData["$renderConfiguration"];
            setFromJsonIfPresent(config.imageWidth, renderConfiguration, "$width");
            setFromJsonIfPresent(config.imageHeight, renderConfiguration, "$height");
            setFromJsonIfPresent(config.photonsPerLight, renderConfiguration, "$photonsPerLight");
            setFromJsonIfPresent(config.startFrame, renderConfiguration, "$startFrame");
            setFromJsonIfPresent(config.endFrame, renderConfiguration, "$endFrame");
        }

        std::shared_ptr<MaterialLibrary> materialLibrary = std::make_shared<MaterialLibrary>();

        if (jsonData.contains("$materials"))
        {
            std::cout << "---" << std::endl;
            std::cout << "Parsing $materials" << std::endl;
            json& materials = jsonData["$materials"];

            for (const auto& [name, material] : materials.items())
            {
                if (!material.contains("$type"))
                {
                    throw std::runtime_error(std::string("Material \"") + name + std::string("\" requires $type"));
                }

                const std::string& type = material["$type"].get<std::string>();
                Color color;

                if (material.contains("$color"))
                {
                    json& colorData = material["$color"];

                    if (colorData.size() == 1)
                    {
                        color = Color(colorData[0].get<float>());
                    }
                    else if (colorData.size() == 3)
                    {
                        color = Color(colorData[0].get<float>(), colorData[1].get<float>(), colorData[2].get<float>());
                    }
                }

                std::cout << "  Creating " << type << " material named " << name << " with color (" << color.red << ", " << color.green << ", " << color.blue << ")" << std::endl;

                if (type == "Diffuse")
                {
                    materialLibrary->add(std::make_shared<DiffuseMaterial>(name, color));
                }
                else if (type == "Cauchy")
                {
                    float sigma = CauchyMaterial::kDefaultSigma;

                    if (material.contains("$sigma"))
                    {
                        sigma = material["$sigma"].get<double>();
                    }

                    materialLibrary->add(std::make_shared<CauchyMaterial>(name, color, sigma));
                }
                else
                {
                    std::cout << "WARNING: Unsupported material type \"" << type << "\"" << std::endl;
                }
            }
        }

        std::cout << "materialLibrary contains " << materialLibrary->size() << " materials" << std::endl;
        for (size_t i = 0; i < materialLibrary->size(); ++i)
        {
            std::cout << i << ": " << materialLibrary->fetchByIndex(i)->name() << std::endl;
        }

        std::shared_ptr<MeshLibrary> meshLibrary = std::make_shared<MeshLibrary>();

        if (jsonData.contains("$meshes"))
        {
            std::cout << "---" << std::endl;
            std::cout << "Parsing $meshes" << std::endl;
            json& meshes = jsonData["$meshes"];

            for (const auto& meshFilename : meshes)
            {
                meshLibrary->addFromFile(meshFilename.get<std::string>());
            }
        }

        std::cout << "meshLibrary contains " << meshLibrary->size() << " meshes" << std::endl;
        for (size_t i = 0; i < meshLibrary->size(); ++i)
        {
            std::cout << i << ": " << meshLibrary->fetchByIndex(i)->name() << std::endl;
        }

        std::cout << "---" << std::endl;
        std::cout << "Setting up scene for render" << std::endl;

        std::vector<std::shared_ptr<Object>> objects;

        std::shared_ptr<Object> root = objects.emplace_back(std::make_shared<Object>());
        std::shared_ptr<Object> cameraPivot = objects.emplace_back(std::make_shared<Object>());
        std::shared_ptr<Camera> camera = std::static_pointer_cast<Camera>(objects.emplace_back(std::make_shared<Camera>(imageWidth, imageHeight, verticalFieldOfView)));
        std::shared_ptr<Object> knotMesh = objects.emplace_back(std::make_shared<MeshVolume>(materialLibrary->indexForName("Knot"), meshLibrary->fetch("Knot")));
        // std::shared_ptr<Object> cubeMesh = objects.emplace_back(std::make_shared<MeshVolume>(materialLibrary->indexForName("Cyan"), ObjReader::loadMesh(cubeMeshFile)));
        // std::shared_ptr<OmniLight> omniLight0 = std::static_pointer_cast<OmniLight>(objects.emplace_back(std::make_shared<OmniLight>()));
        // std::shared_ptr<OmniLight> omniLight1 = std::static_pointer_cast<OmniLight>(objects.emplace_back(std::make_shared<OmniLight>()));
        // std::shared_ptr<OmniLight> omniLight2 = std::static_pointer_cast<OmniLight>(objects.emplace_back(std::make_shared<OmniLight>()));
        // std::shared_ptr<OmniLight> omniLight3 = std::static_pointer_cast<OmniLight>(objects.emplace_back(std::make_shared<OmniLight>()));
        // std::shared_ptr<SpotLight> spotLight0 = std::static_pointer_cast<SpotLight>(objects.emplace_back(std::make_shared<SpotLight>()));
        // std::shared_ptr<SpotLight> spotLight1 = std::static_pointer_cast<SpotLight>(objects.emplace_back(std::make_shared<SpotLight>()));

        std::shared_ptr<Object> ground = objects.emplace_back(std::make_shared<PlaneVolume>(materialLibrary->indexForName("Ground")));

        std::shared_ptr<Object> sunPivot = objects.emplace_back(std::make_shared<Object>());
        std::shared_ptr<ParallelLight> sun = std::static_pointer_cast<ParallelLight>(objects.emplace_back(std::make_shared<ParallelLight>()));

        std::shared_ptr<Object> roomContainer = objects.emplace_back(std::make_shared<Object>());
        std::shared_ptr<Object> roomFloor = objects.emplace_back(std::make_shared<MeshVolume>(materialLibrary->indexForName("Default"), meshLibrary->fetch("Floor")));
        std::shared_ptr<Object> roomCeiling = objects.emplace_back(std::make_shared<MeshVolume>(materialLibrary->indexForName("Default"), meshLibrary->fetch("Ceiling")));
        std::shared_ptr<Object> roomNorthWall = objects.emplace_back(std::make_shared<MeshVolume>(materialLibrary->indexForName("Default"), meshLibrary->fetch("NorthWall")));
        std::shared_ptr<Object> roomSouthWall = objects.emplace_back(std::make_shared<MeshVolume>(materialLibrary->indexForName("Default"), meshLibrary->fetch("SouthWall")));
        std::shared_ptr<Object> roomEastWall = objects.emplace_back(std::make_shared<MeshVolume>(materialLibrary->indexForName("Default"), meshLibrary->fetch("EastWall")));
        std::shared_ptr<Object> roomWestWall = objects.emplace_back(std::make_shared<MeshVolume>(materialLibrary->indexForName("Default"), meshLibrary->fetch("WestWall")));
        std::shared_ptr<Object> roomWindowFrame = objects.emplace_back(std::make_shared<MeshVolume>(materialLibrary->indexForName("Default"), meshLibrary->fetch("WindowFrame")));

        Object::setParent(cameraPivot, root);
        Object::setParent(camera, cameraPivot);
        Object::setParent(sunPivot, root);
        Object::setParent(sun, sunPivot);
        Object::setParent(ground, root);
        Object::setParent(knotMesh, root);
        // Object::setParent(omniLight0, root);
        // Object::setParent(omniLight1, root);
        // Object::setParent(omniLight2, root);
        // Object::setParent(omniLight3, root);
        // Object::setParent(cubeMesh, root);
        // Object::setParent(spotLight0, root);
        // Object::setParent(spotLight1, root);

        Object::setParent(roomContainer, root);
        Object::setParent(roomFloor, roomContainer);
        Object::setParent(roomCeiling, roomContainer);
        Object::setParent(roomNorthWall, roomContainer);
        Object::setParent(roomSouthWall, roomContainer);
        Object::setParent(roomEastWall, roomContainer);
        Object::setParent(roomWestWall, roomContainer);
        Object::setParent(roomWindowFrame, roomContainer);

        // roomFloor->transform.position = {0, -7.62, 0};
        // roomSouthWall->transform.position = {0, 0, -236.22};

        ground->transform.position = {0, -7.62, 0};

        // cubeMesh->transform.position = {0, -70, 0};

        // omniLight0->name("OmniLight0");
        // omniLight0->transform.position = {50, 50, 30};
        // omniLight0->color(Color::fromRGB(255, 255, 255));
        // omniLight0->brightness(10000000);
        // omniLight0->innerRadius(5.0f);

        // omniLight1->name("OmniLight1");
        // omniLight1->transform.position = {43.3f, 50, -25};
        // omniLight1->color(Color::fromRGB(0, 255, 0));
        // omniLight1->brightness(1000000);
        // omniLight1->innerRadius(5.0f);

        // omniLight2->name("OmniLight2");
        // omniLight2->transform.position = {-43.3f, 50, -25};
        // omniLight2->color(Color::fromRGB(0, 0, 255));
        // omniLight2->brightness(1000000);
        // omniLight2->innerRadius(5.0f);

        // omniLight3->name("OmniLight3");
        // omniLight3->transform.position = {0, 0, 0};
        // omniLight3->color(Color::fromRGB(255, 255, 255));
        // omniLight3->brightness(80000);
        // omniLight3->innerRadius(5.0f);

        // spotLight0->name("SpotLight0");
        // spotLight0->transform.position = {70, 70, 0};
        // spotLight0->transform.rotation = Quaternion::fromPitchYawRoll(0, Utility::radians(-90.0), 0) * Quaternion::fromPitchYawRoll(Utility::radians(80.0), 0, 0);
        // spotLight0->color({1, 1, 0});
        // spotLight0->brightness(10000000);
        // spotLight0->angle(Utility::radians(10.0));

        // spotLight1->name("SpotLight1");
        // spotLight1->transform.position = {-70, 70, 0};
        // spotLight1->transform.rotation = Quaternion::fromPitchYawRoll(0, Utility::radians(90.0), 0) * Quaternion::fromPitchYawRoll(Utility::radians(80.0), 0, 0);
        // spotLight1->color({0, 1, 1});
        // spotLight1->brightness(10000000);
        // spotLight1->angle(Utility::radians(10.0));

        sun->name("ParallelLight0");
        sun->transform.position = {0.0, 1000.0, 0.0};
        sun->transform.rotation = Quaternion::fromPitchYawRoll(Utility::radians(90), 0, 0);
        sun->color({1, 1, 1});
        sun->brightness(10000000);
        sun->radius(1000.0);

        sunPivot->transform.rotation = Quaternion::fromPitchYawRoll(Utility::radians(0.0), 0.0, 0.0);

        camera->transform.position = {274.32, 172.72, 0.0};
        camera->transform.rotation = Quaternion::fromPitchYawRoll(Utility::radians(0), Utility::radians(-90), 0);

        knotMesh->transform.position = {0.0, 121.92, 0.0};

        std::shared_ptr<Buffer> buffer = std::make_shared<Buffer>(imageWidth, imageHeight);

        std::shared_ptr<Image> image = std::make_shared<Image>(imageWidth, imageHeight);
        Pixel workingPixel;

        const size_t pixelCount = image->width() * image->height();

        const double pitchStep = camera->verticalFieldOfView() / static_cast<double>(image->height());
        const double yawStep = camera->horizontalFieldOfView() / static_cast<double>(image->width());

        std::cout << "---" << std::endl;
        std::cout << "Rendering image at " << image->width() << " px by " << image->height() << " px" << std::endl;

        std::shared_ptr<LightQueue> lightQueue = std::make_shared<LightQueue>();
        std::shared_ptr<WorkQueue<Photon>> photonQueue = std::make_shared<WorkQueue<Photon>>(photonQueueSize);
        std::shared_ptr<WorkQueue<PhotonHit>> hitQueue = std::make_shared<WorkQueue<PhotonHit>>(hitQueueSize);
        std::shared_ptr<WorkQueue<PhotonHit>> finalHitQueue = std::make_shared<WorkQueue<PhotonHit>>(finalQueueSize);

        std::vector<std::shared_ptr<Worker>> workers{workerCount};

        size_t workerIndex = 0;
        for (auto& worker : workers)
        {
            worker = std::make_shared<Worker>(workerIndex, fetchSize);
            worker->camera = camera;
            worker->objects = objects;
            worker->photonQueue = photonQueue;
            worker->hitQueue = hitQueue;
            worker->finalHitQueue = finalHitQueue;
            worker->buffer = buffer;
            worker->image = image;
            worker->materialLibrary = materialLibrary;
            worker->lightQueue = lightQueue;
            ++workerIndex;
        }

        for (size_t i = 0; i < workerCount; ++i)
        {
            workers[i]->start();
        }

        const double rotationStep = 360.0 / static_cast<double>(frameCount);
        const double sunPivotStep = 180.0 / static_cast<double>(frameCount);

        const size_t configFrameCount = (config.endFrame - config.startFrame) + 1;
        for (size_t frame = config.startFrame; frame <= config.endFrame; ++frame)
        {
            std::cout << "---" << std::endl;
            std::cout << "Rendering frame " << frame + 1 << " / " << configFrameCount << std::endl;

            const std::chrono::time_point renderStart = std::chrono::system_clock::now();

            std::cout << "---" << std::endl;
            std::cout << "Clearing buffer and image" << std::endl;

            buffer->clear();
            image->clear();

            std::cout << "---" << std::endl;
            std::cout << "Animating objects" << std::endl;

            // TODO: Move all animation to new system

            const double animTime = static_cast<double>(frame) / static_cast<double>(frameCount);

            double sunIntro = std::min(1.0, animTime * 5.0);
            double sunOutro = std::min(1.0, (1.0 - animTime) * 5.0);
            double sunAngle = -20.0 + ((std::sin(Utility::pi2 * animTime) + 1.0) / 2.0) * -70.0;

            knotMesh->transform.rotation = Quaternion::fromPitchYawRoll(Utility::radians(frame * -rotationStep), Utility::radians(frame * rotationStep), Utility::radians(frame * rotationStep * 2));

            sunPivot->transform.rotation = Quaternion::fromPitchYawRoll(0, Utility::radians(-10.0), 0) * Quaternion::fromPitchYawRoll(Utility::radians(sunAngle), 0.0, 0.0);

            // sun->brightness(100000000 * sunIntro * sunOutro);

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

            const std::chrono::time_point writeImageStart = std::chrono::system_clock::now();

            for (size_t y = 0; y < imageHeight; ++y)
            {
                for (size_t x = 0; x < imageWidth; ++x)
                {
                    const Color color = buffer->fetchColor({x, y});

                    const float gammaRed = std::pow(color.red, 1.0f / Color::gamma);
                    const float gammaGreen = std::pow(color.green, 1.0f / Color::gamma);
                    const float gammaBlue = std::pow(color.blue, 1.0f / Color::gamma);

                    workingPixel.red = std::min(static_cast<int>(gammaRed * 65535), 65535);
                    workingPixel.green = std::min(static_cast<int>(gammaGreen * 65535), 65535);
                    workingPixel.blue = std::min(static_cast<int>(gammaBlue * 65535), 65535);

                    image->setPixel((imageWidth - 1) - x, (imageHeight - 1) - y, workingPixel);
                }
            }

            PngWriter::writeImage(renderPath + "\\" + outputName + "." + std::to_string(frame) + ".png", *image, outputName);

            const std::chrono::time_point writeImageEnd = std::chrono::system_clock::now();
            const std::chrono::microseconds writeImageDuration = std::chrono::duration_cast<std::chrono::microseconds>(writeImageEnd - writeImageStart);

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

            for (auto& worker : workers)
            {
                emitProcessed += worker->emitProcessed;
                photonsProcessed += worker->photonsProcessed;
                hitsProcessed += worker->hitsProcessed;
                finalHitsProcessed += worker->finalHitsProcessed;
                worker->emitProcessed = 0;
                worker->photonsProcessed = 0;
                worker->hitsProcessed = 0;
                worker->finalHitsProcessed = 0;

                emitDuration += worker->emitDuration;
                photonDuration += worker->photonDuration;
                hitDuration += worker->hitDuration;
                writeDuration += worker->writeDuration;
                worker->emitDuration = 0;
                worker->photonDuration = 0;
                worker->hitDuration = 0;
                worker->writeDuration = 0;
            }

            const size_t totalDuration = photonDuration + hitDuration + writeDuration;

            std::cout << "---" << std::endl;
            std::cout << "Finished" << std::endl;

            const std::chrono::time_point renderEnd = std::chrono::system_clock::now();
            const std::chrono::microseconds renderDuration = std::chrono::duration_cast<std::chrono::microseconds>(renderEnd - renderStart);

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

            std::cout << "Work queues:" << std::endl;
            std::cout << "|- photon queue maximum allocated:    " << photonQueue->largestAllocated() << std::endl;
            std::cout << "|- hit queue maximum allocated:       " << hitQueue->largestAllocated() << std::endl;
            std::cout << "|- final hit queue maximum allocated: " << finalHitQueue->largestAllocated() << std::endl;
        }

        for (size_t i = 0; i < workerCount; ++i)
        {
            workers[i]->stop();
        }
    }
    catch (const std::exception& e)
    {
        std::cout << "ERROR: " << e.what() << std::endl;
    }

    std::cout << "Goodbye!" << std::endl;

    return 0;
}
