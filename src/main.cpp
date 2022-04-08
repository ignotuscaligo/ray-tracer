#define TINYOBJLOADER_IMPLEMENTATION

#include "Buffer.h"
#include "Camera.h"
#include "CauchyMaterial.h"
#include "DiffuseMaterial.h"
#include "EnumFlag.h"
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
#include <filesystem>
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

constexpr double verticalFieldOfView = 90.0f;

const std::string outputName = "simple_room";

struct ProjectConfiguration
{
    std::filesystem::path projectFilePath;
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
    std::filesystem::path renderPath;
};

ENUM_FLAG(JsonOption, unsigned int)
enum class JsonOption
{
    None    = 0x00,
    WithLog = 0x01,
};

template<typename T>
void setFromJsonIfPresent(T& output, json jsonContainer, const std::string& key, JsonOption options = JsonOption::None)
{
    if (jsonContainer.contains(key))
    {
        output = jsonContainer[key].get<T>();
        if (checkFlag(options & JsonOption::WithLog))
        {
            std::cout << "  Setting " << key << " to " << output << std::endl;
        }
    }
}

class VectorParseError : public std::runtime_error
{
public:
    VectorParseError()
    : std::runtime_error("Vector object must be an array with 3 or 4 number elements")
    {
    }
};

class RotationParseError : public std::runtime_error
{
public:
    RotationParseError()
    : std::runtime_error("Rotation object must contain a $type string and a Vector $value object")
    {
    }
};

class ColorParseError : public std::runtime_error
{
public:
    ColorParseError()
    : std::runtime_error("Color object must be an array with 1 or 3 number elements")
    {
    }
};

Vector parseVectorFromJson(json jsonContainer)
{
    if (!jsonContainer.is_array())
    {
        throw VectorParseError();
    }

    if (jsonContainer.size() < 3 || jsonContainer.size() > 4)
    {
        throw VectorParseError();
    }

    for (auto& element : jsonContainer)
    {
        if (!element.is_number())
        {
            throw VectorParseError();
        }
    }

    Vector result{
        jsonContainer[0].get<double>(),
        jsonContainer[1].get<double>(),
        jsonContainer[2].get<double>()
    };

    if (jsonContainer.size() == 4)
    {
        result._w =  jsonContainer[3].get<double>();
    }

    return result;
}

template<>
void setFromJsonIfPresent<Vector>(Vector& output, json jsonContainer, const std::string& key, JsonOption options)
{
    if (jsonContainer.contains(key))
    {
        output = parseVectorFromJson(jsonContainer[key]);
        if (checkFlag(options & JsonOption::WithLog))
        {
            std::cout << "  Setting " << key << " to (" << output.x << ", " << output.y << ", " << output.z << ", " << output._w << ")" << std::endl;
        }
    }
}

Quaternion parseRotationFromJson(json jsonContainer)
{
    if (!jsonContainer.is_object())
    {
        throw RotationParseError();
    }

    if (!jsonContainer.contains("$type"))
    {
        throw RotationParseError();
    }

    if (!jsonContainer.contains("$value"))
    {
        throw RotationParseError();
    }

    json typeObject = jsonContainer["$type"];

    if (!typeObject.is_string())
    {
        throw RotationParseError();
    }

    std::string type = typeObject.get<std::string>();
    Vector value = parseVectorFromJson(jsonContainer["$value"]);

    Quaternion result;

    if (type == "PitchYawRollDegrees")
    {
        result = Quaternion::fromPitchYawRoll(Utility::radians(value.x), Utility::radians(value.y), Utility::radians(value.z));
    }
    else if (type == "PitchYawRollRadians")
    {
        result = Quaternion::fromPitchYawRoll(value.x, value.y, value.z);
    }
    else
    {
        throw std::runtime_error(std::string("Unsupported rotation type: ") + type);
    }

    return result;
}

template<>
void setFromJsonIfPresent<Quaternion>(Quaternion& output, json jsonContainer, const std::string& key, JsonOption options)
{
    if (jsonContainer.contains(key))
    {
        output = parseRotationFromJson(jsonContainer[key]);
        if (checkFlag(options & JsonOption::WithLog))
        {
            std::cout << "  Setting " << key << " to (" << output.x << ", " << output.y << ", " << output.z << ", " << output.w << ")" << std::endl;
        }
    }
}

Color parseColorFromJson(json jsonContainer)
{
    if (!jsonContainer.is_array())
    {
        throw ColorParseError();
    }

    if (jsonContainer.size() != 1 && jsonContainer.size() != 3)
    {
        throw ColorParseError();
    }

    for (auto& element : jsonContainer)
    {
        if (!element.is_number())
        {
            throw ColorParseError();
        }
    }

    if (jsonContainer.size() == 1)
    {
        const float grey = jsonContainer[0].get<float>();
        return {
            grey,
            grey,
            grey
        };
    }
    else
    {
        return {
            jsonContainer[0].get<float>(),
            jsonContainer[1].get<float>(),
            jsonContainer[2].get<float>()
        };
    }
}

template<>
void setFromJsonIfPresent<Color>(Color& output, json jsonContainer, const std::string& key, JsonOption options)
{
    if (jsonContainer.contains(key))
    {
        output = parseColorFromJson(jsonContainer[key]);
        if (checkFlag(options & JsonOption::WithLog))
        {
            std::cout << "  Setting " << key << " to (" << output.red << ", " << output.green << ", " << output.blue << ")" << std::endl;
        }
    }
}

class ObjectFactory
{
public:
    ObjectFactory(std::shared_ptr<MaterialLibrary> materialLibrary, std::shared_ptr<MeshLibrary> meshLibrary)
    : m_materialLibrary(materialLibrary)
    , m_meshLibrary(meshLibrary)
    {
    }

    void setParametersForObject(std::shared_ptr<Object> object, json jsonContainer)
    {
        setFromJsonIfPresent(object->transform.position, jsonContainer, "$position");
        setFromJsonIfPresent(object->transform.rotation, jsonContainer, "$rotation");
    }

    void setParametersForCamera(std::shared_ptr<Camera> object, json jsonContainer)
    {
        setParametersForObject(object, jsonContainer);

        double verticalFieldOfView = 0;
        setFromJsonIfPresent(verticalFieldOfView, jsonContainer, "$verticalFieldOfView");

        object->verticalFieldOfView(verticalFieldOfView);
    }

    void setParametersForVolume(std::shared_ptr<Volume> object, json jsonContainer)
    {
        setParametersForObject(object, jsonContainer);

        std::string material = "Default";
        setFromJsonIfPresent(material, jsonContainer, "$material");

        object->materialIndex(m_materialLibrary->indexForName(material));
    }

    void setParametersForMeshVolume(std::shared_ptr<MeshVolume> object, json jsonContainer)
    {
        setParametersForVolume(object, jsonContainer);

        std::string mesh = "Default";
        setFromJsonIfPresent(mesh, jsonContainer, "$mesh");

        object->mesh(m_meshLibrary->fetch(mesh));
    }

    void setParametersForPlaneVolume(std::shared_ptr<PlaneVolume> object, json jsonContainer)
    {
        setParametersForVolume(object, jsonContainer);
    }

    void setParametersForLight(std::shared_ptr<Light> object, json jsonContainer)
    {
        setParametersForObject(object, jsonContainer);

        Color color;
        setFromJsonIfPresent(color, jsonContainer, "$color");

        object->color(color);

        double brightness = 0.0;
        setFromJsonIfPresent(brightness, jsonContainer, "$brightness");

        object->brightness(brightness);
    }

    void setParametersForOmniLight(std::shared_ptr<OmniLight> object, json jsonContainer)
    {
        setParametersForLight(object, jsonContainer);

        double innerRadius = 0.0;
        setFromJsonIfPresent(innerRadius, jsonContainer, "$innerRadius");

        object->innerRadius(innerRadius);
    }

    void setParametersForParallelLight(std::shared_ptr<ParallelLight> object, json jsonContainer)
    {
        setParametersForLight(object, jsonContainer);

        double radius = 0.0;
        setFromJsonIfPresent(radius, jsonContainer, "$radius");

        object->radius(radius);
    }

    void setParametersForSpotLight(std::shared_ptr<SpotLight> object, json jsonContainer)
    {
        setParametersForLight(object, jsonContainer);

        double innerRadius = 0.0;
        setFromJsonIfPresent(innerRadius, jsonContainer, "$innerRadius");

        object->innerRadius(innerRadius);

        double angle = 0.0;
        setFromJsonIfPresent(angle, jsonContainer, "$angle");

        object->angle(angle);
    }

    std::shared_ptr<Object> createObjectFromJson(json jsonContainer)
    {
        std::string type = "Object";
        setFromJsonIfPresent(type, jsonContainer, "$type");

        if (type == "Object")
        {
            std::shared_ptr<Object> object = std::make_shared<Object>();
            setParametersForObject(object, jsonContainer);
            return object;
        }
        else if (type == "Camera")
        {
            std::shared_ptr<Camera> object = std::make_shared<Camera>();
            setParametersForCamera(object, jsonContainer);
            return object;
        }
        else if (type == "MeshVolume")
        {
            std::shared_ptr<MeshVolume> object = std::make_shared<MeshVolume>();
            setParametersForMeshVolume(object, jsonContainer);
            return object;
        }
        else if (type == "PlaneVolume")
        {
            std::shared_ptr<PlaneVolume> object = std::make_shared<PlaneVolume>();
            setParametersForPlaneVolume(object, jsonContainer);
            return object;
        }
        else if (type == "OmniLight")
        {
            std::shared_ptr<OmniLight> object = std::make_shared<OmniLight>();
            setParametersForOmniLight(object, jsonContainer);
            return object;
        }
        else if (type == "ParallelLight")
        {
            std::shared_ptr<ParallelLight> object = std::make_shared<ParallelLight>();
            setParametersForParallelLight(object, jsonContainer);
            return object;
        }
        else if (type == "SpotLight")
        {
            std::shared_ptr<SpotLight> object = std::make_shared<SpotLight>();
            setParametersForSpotLight(object, jsonContainer);
            return object;
        }
        else
        {
            throw std::runtime_error(std::string("Unrecognized object type: ") + type);
        }
    }

private:
    std::shared_ptr<MaterialLibrary> m_materialLibrary;
    std::shared_ptr<MeshLibrary> m_meshLibrary;
};

std::vector<std::shared_ptr<Object>> parseObjectFromJson(const std::string& name, json jsonContainer, std::shared_ptr<Object> parent, ObjectFactory& objectFactory)
{
    std::vector<std::shared_ptr<Object>> parsedObjects;
    std::shared_ptr<Object> parsedObject = objectFactory.createObjectFromJson(jsonContainer);
    parsedObjects.push_back(parsedObject);
    parsedObject->name(name);
    Object::setParent(parsedObject, parent);

    for (const auto& [childName, childObject] : jsonContainer.items())
    {
        if (childName[0] != '$')
        {
            std::vector<std::shared_ptr<Object>> childObjects = parseObjectFromJson(childName, childObject, parsedObject, objectFactory);
            parsedObjects.insert(parsedObjects.end(), childObjects.begin(), childObjects.end());
        }
    }

    return parsedObjects;
}

}

int main(int argc, char** argv)
{
    std::cout << "Hello!" << std::endl;

    ProjectConfiguration config;
    config.projectFilePath = std::filesystem::absolute("C:\\Users\\ekleeman\\repos\\ray-tracer\\test.json");

    try
    {
        std::cout << "Loading project file " << config.projectFilePath << std::endl;
        std::ifstream jsonFile(config.projectFilePath);
        json jsonData = json::parse(jsonFile);
        jsonFile.close();

        if (jsonData.contains("$workerConfiguration"))
        {
            std::cout << "---" << std::endl;
            std::cout << "Parsing $workerConfiguration" << std::endl;
            json& workerConfiguration = jsonData["$workerConfiguration"];
            setFromJsonIfPresent(config.workerCount, workerConfiguration, "$workerCount", JsonOption::WithLog);
            setFromJsonIfPresent(config.fetchSize, workerConfiguration, "$fetchSize", JsonOption::WithLog);
            setFromJsonIfPresent(config.photonQueueSize, workerConfiguration, "$photonQueueSize", JsonOption::WithLog);
            setFromJsonIfPresent(config.hitQueueSize, workerConfiguration, "$hitQueueSize", JsonOption::WithLog);
            setFromJsonIfPresent(config.finalQueueSize, workerConfiguration, "$finalQueueSize", JsonOption::WithLog);
        }

        if (jsonData.contains("$renderConfiguration"))
        {
            std::cout << "---" << std::endl;
            std::cout << "Parsing $renderConfiguration" << std::endl;
            json& renderConfiguration = jsonData["$renderConfiguration"];
            setFromJsonIfPresent(config.imageWidth, renderConfiguration, "$width", JsonOption::WithLog);
            setFromJsonIfPresent(config.imageHeight, renderConfiguration, "$height", JsonOption::WithLog);
            setFromJsonIfPresent(config.photonsPerLight, renderConfiguration, "$photonsPerLight", JsonOption::WithLog);
            setFromJsonIfPresent(config.startFrame, renderConfiguration, "$startFrame", JsonOption::WithLog);
            setFromJsonIfPresent(config.endFrame, renderConfiguration, "$endFrame", JsonOption::WithLog);

            if (renderConfiguration.contains("$renderPath"))
            {
                config.renderPath = renderConfiguration["$renderPath"].get<std::string>();
            }

            if (config.renderPath.is_relative())
            {
                config.renderPath = std::filesystem::absolute(config.projectFilePath.parent_path() / config.renderPath);
            }
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

        std::vector<std::shared_ptr<Object>> objects;
        ObjectFactory objectFactory{materialLibrary, meshLibrary};

        if (jsonData.contains("$scene"))
        {
            std::cout << "---" << std::endl;
            std::cout << "Parsing $scene" << std::endl;
            json& scene = jsonData["$scene"];

            for (const auto& [name, object] : scene.items())
            {
                std::vector<std::shared_ptr<Object>> parsedObjects = parseObjectFromJson(name, object, nullptr, objectFactory);
                objects.insert(objects.end(), parsedObjects.begin(), parsedObjects.end());
            }
        }

        for (auto& object : objects)
        {
            if (object->hasType<Camera>())
            {
                std::shared_ptr<Camera> camera = std::static_pointer_cast<Camera>(object);
                camera->setFromRenderConfiguration(config.imageWidth, config.imageHeight);
            }
        }

        std::cout << "Parsed " << objects.size() << " object(s) from json" << std::endl;

        std::shared_ptr<Camera> camera;
        std::shared_ptr<MeshVolume> knotMesh;
        std::shared_ptr<Object> sunPivot;

        for (auto& object : objects)
        {
            if (object->name() == "camera")
            {
                camera = std::static_pointer_cast<Camera>(object);
            }
            else if (object->name() == "knotMesh")
            {
                knotMesh = std::static_pointer_cast<MeshVolume>(object);
            }
            else if (object->name() == "sunPivot")
            {
                sunPivot = object;
            }
        }

        std::shared_ptr<Buffer> buffer = std::make_shared<Buffer>(config.imageWidth, config.imageHeight);

        std::shared_ptr<Image> image = std::make_shared<Image>(config.imageWidth, config.imageHeight);
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

            for (size_t y = 0; y < config.imageHeight; ++y)
            {
                for (size_t x = 0; x < config.imageWidth; ++x)
                {
                    const Color color = buffer->fetchColor({x, y});

                    const float gammaRed = std::pow(color.red, 1.0f / Color::gamma);
                    const float gammaGreen = std::pow(color.green, 1.0f / Color::gamma);
                    const float gammaBlue = std::pow(color.blue, 1.0f / Color::gamma);

                    workingPixel.red = std::min(static_cast<int>(gammaRed * 65535), 65535);
                    workingPixel.green = std::min(static_cast<int>(gammaGreen * 65535), 65535);
                    workingPixel.blue = std::min(static_cast<int>(gammaBlue * 65535), 65535);

                    image->setPixel((config.imageWidth - 1) - x, (config.imageHeight - 1) - y, workingPixel);
                }
            }

            PngWriter::writeImage(config.renderPath.string() + "\\" + outputName + "." + std::to_string(frame) + ".png", *image, outputName);

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
