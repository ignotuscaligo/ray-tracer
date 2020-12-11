#define TINYOBJLOADER_IMPLEMENTATION

#include "Image.h"
#include "MeshVolume.h"
#include "Object.h"
#include "OmniLight.h"
#include "Photon.h"
#include "Pixel.h"
#include "PixelSensor.h"
#include "Plane.h"
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
constexpr size_t frameCount = 1;

constexpr size_t imageWidth = 512;
constexpr size_t imageHeight = 512;
constexpr float aspectRatio = static_cast<float>(imageWidth) / static_cast<float>(imageHeight);
constexpr float verticalFov = 80.0f;
constexpr float horizontalFov = verticalFov * aspectRatio;

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
        std::shared_ptr<Object> camera = objects.emplace_back(std::make_shared<Object>());
        std::shared_ptr<Object> objectPivot = objects.emplace_back(std::make_shared<Object>());
        std::shared_ptr<Object> sun = objects.emplace_back(std::make_shared<Object>());
        std::shared_ptr<Object> knotMesh = objects.emplace_back(loadMeshAsObject(inputFile));
        std::shared_ptr<OmniLight> omniLight0 = std::static_pointer_cast<OmniLight>(objects.emplace_back(std::make_shared<OmniLight>()));
        std::shared_ptr<OmniLight> omniLight1 = std::static_pointer_cast<OmniLight>(objects.emplace_back(std::make_shared<OmniLight>()));

        Object::setParent(cameraPivot, root);
        Object::setParent(camera, cameraPivot);
        Object::setParent(sun, cameraPivot);
        Object::setParent(objectPivot, root);
        Object::setParent(knotMesh, objectPivot);
        Object::setParent(omniLight0, objectPivot);
        Object::setParent(omniLight1, objectPivot);

        omniLight0->transform.position = {40, 40, 40};
        omniLight0->color({1.0f, 0.95f, 0.87f});
        omniLight0->brightness(200000);

        omniLight1->transform.position = {-40, -40, -40};
        omniLight1->color({0.7f, 0.7f, 1.0f});
        omniLight1->brightness(200000);

        size_t lightCount = 0;

        for (const auto& object : objects)
        {
            if (object->hasType<Light>())
            {
                ++lightCount;
            }
        }

        size_t photonsPerLight = photonCount / lightCount;

        std::shared_ptr<Image> image = std::make_shared<Image>(imageWidth, imageHeight);
        Pixel workingPixel;

        const size_t pixelCount = image->width() * image->height();

        float pitchStep = verticalFov / static_cast<float>(image->height());
        float yawStep = horizontalFov / static_cast<float>(image->width());

        std::shared_ptr<std::vector<PixelSensor>> pixelSensors = std::make_shared<std::vector<PixelSensor>>(pixelCount);

        std::cout << "---" << std::endl;
        std::cout << "Rendering image at " << image->width() << " px by " << image->height() << " px" << std::endl;

        camera->transform.position = {0.0f, 0.0f, 55.0f};
        camera->transform.rotation = Quaternion::fromPitchYawRoll(0, radians(180), 0);

        sun->transform.rotation = Quaternion::fromPitchYawRoll(radians(45.0f), radians(45.0f), 0.0f);

        std::shared_ptr<WorkQueue<Photon>> photonQueue = std::make_shared<WorkQueue<Photon>>(photonCount);
        std::shared_ptr<WorkQueue<PhotonHit>> hitQueue = std::make_shared<WorkQueue<PhotonHit>>(photonCount);
        std::shared_ptr<WorkQueue<PhotonHit>> finalHitQueue = std::make_shared<WorkQueue<PhotonHit>>(photonCount);

        std::shared_ptr<Worker> workers[workerCount];

        size_t pixelStep = pixelSensors->size() / workerCount;

        for (size_t i = 0; i < workerCount; ++i)
        {
            size_t index = i;
            size_t startPixel = i * pixelStep;
            size_t endPixel = startPixel + pixelStep;

            if (i == workerCount - 1)
            {
                endPixel = pixelSensors->size() - 1;
            }

            workers[i] = std::make_shared<Worker>(index, fetchSize, startPixel, endPixel);
            workers[i]->camera = camera;
            workers[i]->objects = objects;
            workers[i]->photonQueue = photonQueue;
            workers[i]->hitQueue = hitQueue;
            workers[i]->finalHitQueue = finalHitQueue;
            workers[i]->pixelSensors = pixelSensors;
            workers[i]->finalTree = nullptr;
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

        for (size_t frame = startFrame; frame < startFrame + frameCount; ++frame)
        {
            std::cout << "---" << std::endl;
            std::cout << "Rendering frame " << frame + 1 << " / " << frameCount << std::endl;

            image->clear();

            objectPivot->transform.rotation = Quaternion::fromPitchYawRoll(0, radians(frame * rotationStep), 0);
            knotMesh->transform.rotation = Quaternion::fromPitchYawRoll(0, 0, radians(frame * rotationStep * 2));

            Vector cameraPosition = camera->position();
            Quaternion cameraRotation = camera->rotation();
            Vector cameraForward = camera->forward();
            Vector sunDirection = -sun->forward();

            std::chrono::time_point renderStart = std::chrono::system_clock::now();
            std::chrono::time_point generatePhotonsStart = std::chrono::system_clock::now();

            std::cout << "Emitting " << lightCount * photonsPerLight << " photons from " << lightCount << " lights" << std::endl;
            for (const auto& object : objects)
            {
                if (object->hasType<Light>())
                {
                    auto photons = photonQueue->initialize(photonsPerLight);

                    std::static_pointer_cast<Light>(object)->emit(photons);

                    photonQueue->ready(photons);
                }
            }

            std::cout << "---" << std::endl;
            std::cout << "Generate sensors" << std::endl;
            for (int y = 0; y < image->height(); ++y)
            {
                float pitch = -((verticalFov / 2.0f) - ((y / (image->height() - 1.0f)) * verticalFov));

                for (int x = 0; x < image->width(); ++x)
                {
                    size_t index = x + (y * image->width());
                    float yaw = ((horizontalFov / 2.0f) - ((x / (image->width() - 1.0f)) * horizontalFov));

                    pixelSensors->at(index) = PixelSensor(cameraPosition, cameraRotation, radians(pitch), radians(yaw), radians(pitchStep), radians(yawStep));
                    pixelSensors->at(index).x = x;
                    pixelSensors->at(index).y = y;
                }
            }

            std::chrono::time_point generatePhotonsEnd = std::chrono::system_clock::now();
            std::chrono::microseconds generatePhotonsDuration = std::chrono::duration_cast<std::chrono::microseconds>(generatePhotonsEnd - generatePhotonsStart);

            std::cout << "---" << std::endl;
            std::cout << "Cast photons into scene" << std::endl;

            size_t photonsAllocated = photonQueue->allocated();
            size_t hitsAllocated = hitQueue->allocated();

            // std::cout << "---" << std::endl;
            // std::cout << "remaining photons:    " << photonsAllocated << std::endl;
            // std::cout << "remaining hits:       " << hitsAllocated << std::endl;

            size_t lastPhotons = photonsAllocated;
            size_t lastHits = hitsAllocated;

            while (photonsAllocated > 0 || hitsAllocated > 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));

                photonsAllocated = photonQueue->allocated();
                hitsAllocated = hitQueue->allocated();

                // if (photonsAllocated != lastPhotons || hitsAllocated != lastHits)
                // {
                //     std::cout << "---" << std::endl;
                //     std::cout << "remaining photons:    " << photonsAllocated << std::endl;
                //     std::cout << "remaining hits:       " << hitsAllocated << std::endl;
                // }

                lastPhotons = photonsAllocated;
                lastHits = hitsAllocated;
            }

            std::cout << "---" << std::endl;
            std::cout << "Collecting photons into pixels" << std::endl;

            auto hitsBlock = finalHitQueue->fetch(finalHitQueue->available());

            std::vector<PhotonHit> finalHits{hitsBlock.toVector()};

            std::shared_ptr<Tree<PhotonHit>> finalTree = std::make_shared<Tree<PhotonHit>>(finalHits, 100);

            finalHitQueue->release(hitsBlock);

            for (size_t i = 0; i < workerCount; ++i)
            {
                workers[i]->startWrite(finalTree);
            }

            size_t workersCompleted = 0;
            size_t lastCompleted = workersCompleted;

            // std::cout << "---" << std::endl;
            // std::cout << "workers finished with write: " << workersCompleted << " / " << workerCount << std::endl;

            while (workersCompleted < workerCount)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));

                workersCompleted = 0;

                for (size_t i = 0; i < workerCount; ++i)
                {
                    if (workers[i]->writeComplete())
                    {
                        ++workersCompleted;
                    }
                }

                // if (lastCompleted != workersCompleted)
                // {
                //     std::cout << "---" << std::endl;
                //     std::cout << "workers finished with write: " << workersCompleted << " / " << workerCount << std::endl;
                // }

                lastCompleted = workersCompleted;
            }

            std::cout << "---" << std::endl;
            std::cout << "Finished" << std::endl;

            size_t photonsProcessed = 0;
            size_t hitsProcessed = 0;
            size_t finalHitsProcessed = finalHits.size();

            for (size_t i = 0; i < workerCount; ++i)
            {
                photonsProcessed += workers[i]->photonsProcessed;
                hitsProcessed += workers[i]->hitsProcessed;
                workers[i]->photonsProcessed = 0;
                workers[i]->hitsProcessed = 0;
            }

            std::chrono::time_point renderEnd = std::chrono::system_clock::now();
            std::chrono::microseconds renderDuration = std::chrono::duration_cast<std::chrono::microseconds>(renderEnd - renderStart);

            size_t photonDuration = 0;
            size_t hitDuration = 0;
            size_t writeDuration = 0;

            for (size_t i = 0; i < workerCount; ++i)
            {
                photonDuration += workers[i]->photonDuration;
                workers[i]->photonDuration = 0;
                hitDuration += workers[i]->hitDuration;
                workers[i]->hitDuration = 0;
                writeDuration += workers[i]->writeDuration;
                workers[i]->writeDuration = 0;
            }

            size_t totalDuration = photonDuration + hitDuration + writeDuration;

            std::cout << "---" << std::endl;
            std::cout << "Render time:" << std::endl;
            std::cout << "|- total:        " << renderDuration.count() / 1000 << " ms" << std::endl;
            std::cout << "|- average / px: " << renderDuration.count() / pixelCount << " us" << std::endl;

            std::cout << "Photons:" << std::endl;
            std::cout << "|- processed:             " << photonsProcessed << std::endl;
            std::cout << "|- generation total time: " << generatePhotonsDuration.count() << " us" << std::endl;
            // std::cout << "|- generation avg time:   " << generatePhotonsAverage << " us" << std::endl;
            // std::cout << "|- process total time:    " << processPhotonsDuration.count() / 1000.0f << " ms" << std::endl;
            // std::cout << "|- process avg time:      " << processPhotonsAverage << " us" << std::endl;

            std::cout << "Hits:" << std::endl;
            std::cout << "|- processed: " << hitsProcessed << std::endl;
            // std::cout << "|- generation total time: " << processHitsDuration.count() << " us" << std::endl;
            // std::cout << "|- generation avg time:   " << processHitsAverage << " us" << std::endl;

            std::cout << "Final hits:" << std::endl;
            std::cout << "|- processed: " << finalHitsProcessed << std::endl;

            std::cout << "Workers:" << std::endl;
            std::cout << "|- total duration:  " << totalDuration << " us" << std::endl;
            std::cout << "|- photon duration: " << photonDuration << " us" << std::endl;
            std::cout << "|- hit duration:    " << hitDuration << " us" << std::endl;
            std::cout << "|- write duration:  " << writeDuration << " us" << std::endl;

            PngWriter::writeImage("C:\\Users\\ekleeman\\repos\\ray-tracer\\renders\\transform_ray_test_1." + std::to_string(frame) + ".png", *image, "test");
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
