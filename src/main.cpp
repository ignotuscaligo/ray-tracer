#define TINYOBJLOADER_IMPLEMENTATION

#include "Buffer.h"
#include "Camera.h"
#include "Image.h"
#include "MeshVolume.h"
#include "Object.h"
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
#include <tiny_obj_loader.h>

namespace
{

constexpr size_t photonCount = 1000000;
constexpr size_t workerCount = 32;
constexpr size_t fetchSize = 10000;

constexpr size_t startFrame = 0;
constexpr size_t frameCount = 24 * 10;

constexpr size_t imageWidth = 512;
constexpr size_t imageHeight = 512;
constexpr float verticalFieldOfView = 80.0f;

const std::string renderPath = "C:\\Users\\ekleeman\\repos\\ray-tracer\\renders";
const std::string outputName = "plane_test_1";

}

std::shared_ptr<Object> loadMeshAsObject(const std::string& filename)
{
    std::cout << "---" << std::endl;
    std::cout << "Loading OBJ " << filename << std::endl;

    tinyobj::attrib_t attrib;

    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;

    std::string warn;
    std::string err;

    bool result = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str());

    if (!warn.empty())
    {
        std::cout << "warn: " << warn << std::endl;
    }

    if (!err.empty())
    {
        std::cout << "err: " << err << std::endl;
    }

    std::vector<Triangle> objTriangles;
    std::array<Vector, 3> points;
    std::array<Vector, 3> normals;

    if (result)
    {
        std::cout << "Loaded obj successfully" << std::endl;

        std::cout << "Found " << shapes.size() << " shapes" << std::endl;

        for (const auto& shape : shapes)
        {
            size_t indexOffset = 0;

            for (const size_t vertexCount : shape.mesh.num_face_vertices)
            {
                for (size_t v = 0; v < vertexCount; ++v)
                {
                    tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];
                    size_t vertexIndex = 3 * idx.vertex_index;
                    size_t normalIndex = 3 * idx.normal_index;
                    points[v].x = attrib.vertices[vertexIndex + 0];
                    points[v].y = attrib.vertices[vertexIndex + 1];
                    points[v].z = attrib.vertices[vertexIndex + 2];

                    normals[v].x = attrib.normals[normalIndex + 0];
                    normals[v].y = attrib.normals[normalIndex + 1];
                    normals[v].z = attrib.normals[normalIndex + 2];
                }

                Triangle triangle{points[0], points[1], points[2]};
                triangle.aNormal = normals[0];
                triangle.bNormal = normals[1];
                triangle.cNormal = normals[2];

                objTriangles.push_back(triangle);

                indexOffset += vertexCount;
            }
        }
    }
    else
    {
        std::cout << "Failed to load obj" << std::endl;
    }

    std::cout << "Loaded " << objTriangles.size() << " triangles" << std::endl;

    std::cout << "Generating mesh from OBJ" << std::endl;
    return std::make_shared<MeshVolume>(objTriangles);
}

int main(int argc, char** argv)
{
    std::cout << "Hello!" << std::endl;

    try
    {
        std::cout << "---" << std::endl;
        std::cout << "Setting up scene for render" << std::endl;

        std::string inputFile = R"(C:\Users\ekleeman\Documents\Cinema 4D\eschers_knot.obj)";

        std::vector<std::shared_ptr<Object>> objects;

        std::shared_ptr<Object> root = objects.emplace_back(std::make_shared<Object>());
        std::shared_ptr<Object> cameraPivot = objects.emplace_back(std::make_shared<Object>());
        std::shared_ptr<Camera> camera = std::static_pointer_cast<Camera>(objects.emplace_back(std::make_shared<Camera>(imageWidth, imageHeight, verticalFieldOfView)));
        std::shared_ptr<Object> objectPivot = objects.emplace_back(std::make_shared<Object>());
        std::shared_ptr<Object> sun = objects.emplace_back(std::make_shared<Object>());
        std::shared_ptr<Object> knotMesh = objects.emplace_back(loadMeshAsObject(inputFile));
        std::shared_ptr<Object> ground = objects.emplace_back(std::make_shared<PlaneVolume>());
        // std::shared_ptr<OmniLight> omniLight0 = std::static_pointer_cast<OmniLight>(objects.emplace_back(std::make_shared<OmniLight>()));
        std::shared_ptr<OmniLight> omniLight1 = std::static_pointer_cast<OmniLight>(objects.emplace_back(std::make_shared<OmniLight>()));

        Object::setParent(cameraPivot, root);
        Object::setParent(camera, cameraPivot);
        Object::setParent(sun, cameraPivot);
        Object::setParent(objectPivot, root);
        Object::setParent(ground, root);
        Object::setParent(knotMesh, objectPivot);
        // Object::setParent(omniLight0, root);
        Object::setParent(omniLight1, objectPivot);

        ground->transform.position = {0, -70, 0};

        // omniLight0->transform.position = {0, 50, 0};
        // omniLight0->color(Color::fromRGB(255, 241, 224));
        // omniLight0->brightness(70000);
        // omniLight0->innerRadius(5.0f);

        omniLight1->transform.position = {0, 0, 0};
        omniLight1->color(Color::fromRGB(201, 226, 255));
        omniLight1->brightness(80000);
        omniLight1->innerRadius(5.0f);

        camera->transform.position = {0.0f, 0.0f, 100.0f};
        camera->transform.rotation = Quaternion::fromPitchYawRoll(Utility::radians(-10), Utility::radians(180), 0);

        sun->transform.rotation = Quaternion::fromPitchYawRoll(Utility::radians(45.0f), Utility::radians(45.0f), 0.0f);

        size_t lightCount = 0;

        for (const auto& object : objects)
        {
            if (object->hasType<Light>())
            {
                ++lightCount;
            }
        }

        size_t photonsPerLight = photonCount / lightCount;

        std::shared_ptr<Buffer> buffer = std::make_shared<Buffer>(imageWidth, imageHeight);

        std::shared_ptr<Image> image = std::make_shared<Image>(imageWidth, imageHeight);
        Pixel workingPixel;

        const size_t pixelCount = image->width() * image->height();

        float pitchStep = camera->verticalFieldOfView() / static_cast<float>(image->height());
        float yawStep = camera->horizontalFieldOfView() / static_cast<float>(image->width());

        std::cout << "---" << std::endl;
        std::cout << "Rendering image at " << image->width() << " px by " << image->height() << " px" << std::endl;

        std::shared_ptr<WorkQueue<Photon>> photonQueue = std::make_shared<WorkQueue<Photon>>(photonCount);
        std::shared_ptr<WorkQueue<PhotonHit>> hitQueue = std::make_shared<WorkQueue<PhotonHit>>(photonCount);
        std::shared_ptr<WorkQueue<PhotonHit>> finalHitQueue = std::make_shared<WorkQueue<PhotonHit>>(photonCount);

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

        for (size_t frame = startFrame; frame < frameCount; ++frame)
        {
            std::cout << "---" << std::endl;
            std::cout << "Rendering frame " << frame + 1 << " / " << frameCount << std::endl;

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
            std::cout << "Emitting " << lightCount * photonsPerLight << " photons from " << lightCount << " lights" << std::endl;

            std::chrono::time_point generatePhotonsStart = std::chrono::system_clock::now();

            for (const auto& object : objects)
            {
                if (object->hasType<Light>())
                {
                    auto photons = photonQueue->initialize(photonsPerLight);

                    std::static_pointer_cast<Light>(object)->emit(photons);

                    photonQueue->ready(photons);
                }
            }

            std::chrono::time_point generatePhotonsEnd = std::chrono::system_clock::now();
            std::chrono::microseconds generatePhotonsDuration = std::chrono::duration_cast<std::chrono::microseconds>(generatePhotonsEnd - generatePhotonsStart);

            std::cout << "---" << std::endl;
            std::cout << "Processing photons" << std::endl;

            size_t photonsAllocated = photonQueue->allocated();
            size_t hitsAllocated = hitQueue->allocated();
            size_t finalHitsAllocated = finalHitQueue->allocated();

            std::cout << "---" << std::endl;
            std::cout << "remaining photons:    " << photonsAllocated << std::endl;
            std::cout << "remaining hits:       " << hitsAllocated << std::endl;
            std::cout << "remaining final hits: " << finalHitsAllocated << std::endl;

            size_t lastPhotons = photonsAllocated;
            size_t lastHits = hitsAllocated;
            size_t lastFinalHits = finalHitsAllocated;

            while (photonsAllocated > 0 || hitsAllocated > 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));

                photonsAllocated = photonQueue->allocated();
                hitsAllocated = hitQueue->allocated();
                finalHitsAllocated = finalHitQueue->allocated();

                if (photonsAllocated != lastPhotons || hitsAllocated != lastHits || finalHitsAllocated != lastFinalHits)
                {
                    std::cout << "---" << std::endl;
                    std::cout << "remaining photons:    " << photonsAllocated << std::endl;
                    std::cout << "remaining hits:       " << hitsAllocated << std::endl;
                    std::cout << "remaining final hits: " << finalHitsAllocated << std::endl;
                }

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

            size_t photonsProcessed = 0;
            size_t hitsProcessed = 0;
            size_t finalHitsProcessed = 0;

            size_t photonDuration = 0;
            size_t hitDuration = 0;
            size_t writeDuration = 0;

            for (size_t i = 0; i < workerCount; ++i)
            {
                photonsProcessed += workers[i]->photonsProcessed;
                hitsProcessed += workers[i]->hitsProcessed;
                finalHitsProcessed += workers[i]->finalHitsProcessed;
                workers[i]->photonsProcessed = 0;
                workers[i]->hitsProcessed = 0;
                workers[i]->finalHitsProcessed = 0;

                photonDuration += workers[i]->photonDuration;
                hitDuration += workers[i]->hitDuration;
                writeDuration += workers[i]->writeDuration;
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
            std::cout << "|- emitted:       " << photonCount << std::endl;
            std::cout << "|- emission time: " << generatePhotonsDuration.count() << " us" << std::endl;

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
