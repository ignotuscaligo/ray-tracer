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

void writeImage(const std::string& filename, Image& image, const std::string& title)
{
    std::cout << "Write image " << filename << std::endl;
    PngWriter writer(filename);

    if (!writer.valid())
    {
        std::cout << "Failed to initialize png writer" << std::endl;
        return;
    }

    // Set title
    writer.setTitle(title);
    writer.writeImage(image);
}

std::shared_ptr<Object> loadMeshAsObject(const std::string& filename)
{
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

    // std::cout << "--- Vector tests ---" << std::endl;

    // Vector a{10, 0, 0};
    // std::cout << "a: " << a.x << ", " << a.y << ", " << a.z << std::endl;

    // Vector b{ 0.5, 0.5, 0.5 };
    // std::cout << "b: " << b.x << ", " << b.y << ", " << b.z << std::endl;

    // Vector c = (b - a).normalize();
    // std::cout << "c: " << c.x << ", " << c.y << ", " << c.z << std::endl;

    // Vector d = Vector::normalizedSub(b, a);
    // std::cout << "d: " << d.x << ", " << d.y << ", " << d.z << std::endl;

    // a.normalize();
    // b.normalize();
    // c.normalize();

    // std::cout << "post normalize a: " << a.x << ", " << a.y << ", " << a.z << std::endl;
    // std::cout << "post normalize b: " << b.x << ", " << b.y << ", " << b.z << std::endl;
    // std::cout << "post normalize c: " << c.x << ", " << c.y << ", " << c.z << std::endl;

    // Vector d{ 1, 0, 0 };
    // Vector e{ 0, 0, 1 };
    // Vector f = Vector::cross(d, e);

    // std::cout << "d: " << d.x << ", " << d.y << ", " << d.z << std::endl;
    // std::cout << "e: " << e.x << ", " << e.y << ", " << e.z << std::endl;
    // std::cout << "f: " << f.x << ", " << f.y << ", " << f.z << std::endl;

    // std::cout << "Pyramid test" << std::endl;

    // Pyramid directPyramid{{0, 0, -1}, {}, 0.0f, 0.0f, radians(20.0f), radians(20.0f)};
    // Vector outsideMin{-1, -1, -1};
    // Vector outsideMax{1, 1, 1};
    // Vector inside{0, 0, 0};

    // std::cout << "directPyramid.containsPoint(outsideMin):             " << directPyramid.containsPoint(outsideMin) << std::endl;
    // std::cout << "directPyramid.containsPoint(outsideMax):             " << directPyramid.containsPoint(outsideMax) << std::endl;
    // std::cout << "directPyramid.containsPoint(inside):                 " << directPyramid.containsPoint(inside) << std::endl;

    // Bounds outsideMinBounds{outsideMin};
    // Bounds outsideMaxBounds{outsideMin};
    // Bounds insideBounds{inside};
    // Bounds overlapMinBounds{outsideMin, inside};
    // Bounds overlapMaxBounds{inside, outsideMax};
    // Bounds overlapMinMaxBounds{outsideMin, outsideMax};

    // std::cout << "directPyramid.intersectsBounds(outsideMinBounds):    " << directPyramid.intersectsBounds(outsideMinBounds) << std::endl;
    // std::cout << "directPyramid.intersectsBounds(outsideMaxBounds):    " << directPyramid.intersectsBounds(outsideMaxBounds) << std::endl;
    // std::cout << "directPyramid.intersectsBounds(insideBounds):        " << directPyramid.intersectsBounds(insideBounds) << std::endl;
    // std::cout << "directPyramid.intersectsBounds(overlapMinBounds):    " << directPyramid.intersectsBounds(overlapMinBounds) << std::endl;
    // std::cout << "directPyramid.intersectsBounds(overlapMaxBounds):    " << directPyramid.intersectsBounds(overlapMaxBounds) << std::endl;
    // std::cout << "directPyramid.intersectsBounds(overlapMinMaxBounds): " << directPyramid.intersectsBounds(overlapMinMaxBounds) << std::endl;

    // std::cout << "Plane test" << std::endl;

    // Vector farAbovePoint{0, 1.5f, 0};
    // Vector abovePoint{0, 0.5, 0};
    // Vector onPoint{0, 0, 0};
    // Vector belowPoint{0, -0.5, 0};
    // Vector farBelowPoint{0, -1.5f, 0};

    // Plane plane({0, 0, 0}, {1, 0, 0}, {0, 0, -1});

    // PixelSensor sensor({0, 0, -1}, {}, 0, 0, radians(90), radians(90));

    // std::cout << "sensor.negY.normal: " << sensor.negY.normal.x << ", " << sensor.negY.normal.y << ", " << sensor.negY.normal.z << std::endl;
    // std::cout << "sensor.posY.normal: " << sensor.posY.normal.x << ", " << sensor.posY.normal.y << ", " << sensor.posY.normal.z << std::endl;
    // std::cout << "sensor.negX.normal: " << sensor.negX.normal.x << ", " << sensor.negX.normal.y << ", " << sensor.negX.normal.z << std::endl;
    // std::cout << "sensor.posX.normal: " << sensor.posX.normal.x << ", " << sensor.posX.normal.y << ", " << sensor.posX.normal.z << std::endl;

    // std::cout << "plane.pointAbovePlane(abovePoint): " << plane.pointAbovePlane(abovePoint) << std::endl;
    // std::cout << "plane.pointAbovePlane(onPoint):    " << plane.pointAbovePlane(onPoint) << std::endl;
    // std::cout << "plane.pointAbovePlane(belowPoint): " << plane.pointAbovePlane(belowPoint) << std::endl;

    // std::cout << "sensor.containsPoint(farAbovePoint): " << sensor.containsPoint(farAbovePoint) << std::endl;
    // std::cout << "sensor.containsPoint(abovePoint):    " << sensor.containsPoint(abovePoint) << std::endl;
    // std::cout << "sensor.containsPoint(onPoint):       " << sensor.containsPoint(onPoint) << std::endl;
    // std::cout << "sensor.containsPoint(belowPoint):    " << sensor.containsPoint(belowPoint) << std::endl;
    // std::cout << "sensor.containsPoint(farBelowPoint): " << sensor.containsPoint(farBelowPoint) << std::endl;

    try
    {
        std::cout << "Setting up scene for render" << std::endl;

        std::string inputFile = R"(C:\Users\ekleeman\Documents\Cinema 4D\eschers_knot.obj)";

        std::vector<std::shared_ptr<Object>> objects;

        std::shared_ptr<Object> root = objects.emplace_back(std::make_shared<Object>());
        std::shared_ptr<Object> cameraPivot = objects.emplace_back(std::make_shared<Object>());
        std::shared_ptr<Object> camera = objects.emplace_back(std::make_shared<Object>());
        std::shared_ptr<Object> sun = objects.emplace_back(std::make_shared<Object>());
        std::shared_ptr<Object> knotMesh = objects.emplace_back(loadMeshAsObject(inputFile));
        std::shared_ptr<OmniLight> omniLight = std::static_pointer_cast<OmniLight>(objects.emplace_back(std::make_shared<OmniLight>()));

        Object::setParent(cameraPivot, root);
        Object::setParent(camera, cameraPivot);
        Object::setParent(sun, cameraPivot);
        Object::setParent(knotMesh, root);
        Object::setParent(omniLight, root);

        omniLight->transform.position = {40, 40, -40};
        omniLight->color({1.0f, 1.0f, 1.0f});
        omniLight->brightness(200000);

        std::shared_ptr<Image> image = std::make_shared<Image>(512, 512);
        Pixel workingPixel;

        float pixelCount = image->width() * image->height();

        float horizontalFov = 80.0f;
        float verticalFov = 80.0f;

        float pitchStep = verticalFov / static_cast<float>(image->height());
        float yawStep = horizontalFov / static_cast<float>(image->width());

        std::shared_ptr<std::vector<PixelSensor>> pixelSensors = std::make_shared<std::vector<PixelSensor>>(image->width() * image->height());

        std::cout << "Rendering image at " << image->width() << " px by " << image->height() << " px" << std::endl;

        camera->transform.position = {0.0f, 0.0f, -55.0f};

        sun->transform.rotation = Quaternion::fromPitchYawRoll(radians(45.0f), radians(45.0f), 0.0f);

        std::cout << "Creating queues" << std::endl;
        const size_t photonCount = 1000000;
        std::shared_ptr<WorkQueue<Photon>> photonQueue = std::make_shared<WorkQueue<Photon>>(photonCount);
        std::shared_ptr<WorkQueue<PhotonHit>> hitQueue = std::make_shared<WorkQueue<PhotonHit>>(photonCount);

        // Debug w/ batched hits + fetchSize 10k + no logging:
        // 1: 13667 ms
        // 2: 7022 ms
        // 4: 3727 ms
        // 8: 2635 ms
        // 16: 2355 ms

        // Debug w/ batched hits + fetchSize 1k + no logging:
        // 1: 13624 ms
        // 2: 6899 ms
        // 4: 3721 ms
        // 8: 2589 ms
        // 16: 2388 ms

        // Release 1024x1024 + fetchSize 1k:
        // 1: 784 ms
        // 2: 453 ms
        // 4: 252 ms
        // 8: 285 ms
        // 16: 403 ms

        // Release 2048x2048 + fetchSize 100k:
        // 1: 3005 ms
        // 2: 1657 ms
        // 4: 1003 ms
        // 8: 961 ms
        // 16: 702 ms
        // 32: 590 ms

        // Debug 2048x2048 + fetchSize 100k:
        // 1: 54634 ms, 4.8%, 482.8MB
        // 2: 27786 ms, 8.9%, 486.8MB
        // 4: 14556 ms, 16.3%, 495.5MB
        // 8: 9882 ms, 31.1%, 512.5MB
        // 16: 9379 ms, 59.7%, 550.5MB
        // 32: 12538 ms, 98.8%, 549.6MB
        const size_t workerCount = 32;

        std::shared_ptr<Worker> workers[workerCount];

        size_t pixelStep = pixelSensors->size() / workerCount;

        size_t fetchSize = 10000;

        for (int i = 0; i < workerCount; ++i)
        {
            size_t index = i;
            size_t startPixel = i * pixelStep;
            size_t endPixel = startPixel + pixelStep;

            if (i == workerCount - 1)
            {
                endPixel = pixelSensors->size() - 1;
            }

            workers[i] = std::make_shared<Worker>(index, fetchSize, startPixel, endPixel);
            workers[i]->objects = objects;
            workers[i]->photonQueue = photonQueue;
            workers[i]->hitQueue = hitQueue;
            workers[i]->pixelSensors = pixelSensors;
            workers[i]->finalTree = nullptr;
            workers[i]->image = image;
        }

        std::thread threads[workerCount];

        for (int i = 0; i < workerCount; ++i)
        {
            workers[i]->start();

            // TODO: move to Worker
            threads[i] = std::thread([&workers, i]() {
                // std::cout << "running thread" << std::endl;
                workers[i]->exec();
            });
        }

        int startFrame = 0;
        int frameCount = 72;

        for (int frame = startFrame; frame < startFrame + frameCount; ++frame)
        {
            std::cout << "Rendering frame " << frame + 1 << " / " << frameCount << std::endl;

            image->clear();

            cameraPivot->transform.rotation = Quaternion::fromPitchYawRoll(0, radians(frame * 5.0f), 0);

            Vector cameraPosition = camera->position();
            Quaternion cameraRotation = camera->rotation();
            Vector cameraForward = camera->forward();
            Vector sunDirection = -sun->forward();

            // std::cout << "cameraPosition: " << cameraPosition.x << ", " << cameraPosition.y << ", " << cameraPosition.z << std::endl;
            // std::cout << "cameraForward: " << cameraForward.x << ", " << cameraForward.y << ", " << cameraForward.z << std::endl;

            // for (int i = 0; i < workerCount; ++i)
            // {
            //     workers[i]->suspend();
            // }

            std::chrono::time_point renderStart = std::chrono::system_clock::now();
            std::chrono::time_point generatePhotonsStart = std::chrono::system_clock::now();

            std::cout << "Cast photons" << std::endl;
            auto photons = photonQueue->initialize(photonCount);

            std::cout << "Emitting " << photons.size() << " photons" << std::endl;
            omniLight->emit(photons);

            photonQueue->ready(photons);

            std::cout << "photonQueue->available(): " << photonQueue->available() << std::endl;

            std::cout << "Generate sensors" << std::endl;
            for (int y = 0; y < image->height(); ++y)
            {
                float pitch = -((verticalFov / 2.0f) - ((y / (image->height() - 1.0f)) * verticalFov));

                // auto photons = photonQueue->initialize(image->width());

                for (int x = 0; x < image->width(); ++x)
                {
                    size_t index = x + (y * image->width());
                    float yaw = -((horizontalFov / 2.0f) - ((x / (image->width() - 1.0f)) * horizontalFov));

                    // Quaternion pixelAngle = Quaternion::fromPitchYawRoll(radians(pitch), radians(yaw), 0);
                    // Vector pixelDirection = pixelAngle * Vector(0, 0, 1);
                    // Vector direction = cameraRotation * pixelDirection;

                    pixelSensors->at(index) = PixelSensor(cameraPosition, cameraRotation, radians(pitch), radians(yaw), radians(pitchStep), radians(yawStep));
                    pixelSensors->at(index).x = x;
                    pixelSensors->at(index).y = y;
                }

                // photonQueue->ready(photons);
            }

            std::chrono::time_point generatePhotonsEnd = std::chrono::system_clock::now();

            size_t photonsGenerated = photonCount;

            std::chrono::microseconds generatePhotonsDuration = std::chrono::duration_cast<std::chrono::microseconds>(generatePhotonsEnd - generatePhotonsStart);
            float generatePhotonsAverage = 0;

            if (photonsGenerated > 0)
            {
                generatePhotonsAverage = generatePhotonsDuration.count() / static_cast<float>(photonsGenerated);
            }

            std::chrono::time_point processPhotonsStart = std::chrono::system_clock::now();

            // for (int i = 0; i < workerCount; ++i)
            // {
            //     workers[i]->resume();
            // }

            size_t currPhotons = photonQueue->allocated();
            size_t prevPhotons = currPhotons;

            std::cout << "Wait for " << currPhotons << " photons to be processed" << std::endl;
            while (currPhotons > 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                currPhotons = photonQueue->allocated();

                if (currPhotons != prevPhotons)
                {
                    std::cout << currPhotons << " remaining" << std::endl;
                }

                prevPhotons = currPhotons;
            }

            std::chrono::time_point processPhotonsEnd = std::chrono::system_clock::now();

            std::chrono::microseconds processPhotonsDuration = std::chrono::duration_cast<std::chrono::microseconds>(processPhotonsEnd - processPhotonsStart);
            int processPhotonsAverage = 0;

            if (photonsGenerated > 0)
            {
                processPhotonsAverage = processPhotonsDuration.count() / photonsGenerated;
            }

            size_t hitsGenerated = hitQueue->available();

            std::chrono::time_point processHitsStart = std::chrono::system_clock::now();

            std::cout << "Scan sensors" << std::endl;
            auto hitsBlock = hitQueue->fetch(hitQueue->available());

            std::vector<PhotonHit> finalHits{hitsBlock.toVector()};

            std::cout << "Processing " << finalHits.size() << " hits" << std::endl;

            std::shared_ptr<Tree<PhotonHit>> finalTree = std::make_shared<Tree<PhotonHit>>(finalHits, 100);

            hitQueue->release(hitsBlock);

            std::cout << "Tree has " << finalTree->size() << " hits" << std::endl;
            std::cout << "Tree has " << finalTree->nodeCount() << " nodes" << std::endl;
            std::cout << "Tree is " << finalTree->nodeDepth() << " layers deep" << std::endl;


            for (int i = 0; i < workerCount; ++i)
            {
                workers[i]->startWrite(finalTree);
            }

            size_t completedWorkers = 0;
            size_t lastCompleted = 0;

            while (completedWorkers != workerCount)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));

                completedWorkers = 0;

                for (int i = 0; i < workerCount; ++i)
                {
                    if (workers[i]->writeComplete())
                    {
                        ++completedWorkers;
                    }
                }

                if (completedWorkers != lastCompleted)
                {
                    std::cout << completedWorkers << " / " << workerCount << " workers completed with write" << std::endl;
                }

                lastCompleted = completedWorkers;
            }

            // size_t currentSensor = 0;

            // float maxValue = 0;

            // size_t containedPoints = 0;

            // for (const auto& sensor : pixelSensors)
            // {
            //     Color color{};

            //     // if (currentSensor % 500 == 0)
            //     // {
            //     //     std::cout << "sensor " << currentSensor << " / " << pixelSensors->size() << ", points: " << containedPoints << std::endl;
            //     //     containedPoints = 0;
            //     // }

            //     // if (sensor.x == 0 && sensor.y == 32)
            //     // {
            //     //     std::cout << sensor.x << ", " << sensor.y << std::endl;
            //     // }
            //     // else  if (sensor.x == 0 && sensor.y == 96)
            //     // {
            //     //     std::cout << sensor.x << ", " << sensor.y << std::endl;
            //     // }

            //     std::vector<PhotonHit> hits = finalTree->fetchWithinPyramid(sensor.pyramid);

            //     for (auto& photonHit : hits)
            //     {
            //         color += photonHit.photon.color;
            //         ++containedPoints;
            //     }

            //     // for (auto& photonHit : finalHits)
            //     // {
            //     //     if (sensor.pyramid.containsPoint(photonHit.hit.position))
            //     //     {
            //     //         color += photonHit.photon.color;
            //     //         ++containedPoints;
            //     //     }
            //     // }

            //     // if (!hits.empty())
            //     // {
            //     //     workingPixel.red = 255;
            //     //     workingPixel.green = 255;
            //     //     workingPixel.blue = 255;
            //     //     image->setPixel(sensor.x, sensor.y, workingPixel);
            //     // }

            //     // if (color.red > 0)
            //     // {
            //     //     workingPixel.red = 255;
            //     //     workingPixel.green = 255;
            //     //     workingPixel.blue = 255;
            //     //     image->setPixel(sensor.x, sensor.y, workingPixel);
            //     // }

            //     workingPixel.red = std::min(static_cast<int>(color.red * 255), 255);
            //     workingPixel.green = std::min(static_cast<int>(color.green * 255), 255);
            //     workingPixel.blue = std::min(static_cast<int>(color.blue * 255), 255);
            //     image->setPixel(sensor.x, sensor.y, workingPixel);

            //     maxValue = std::max(color.red, maxValue);

            //     ++currentSensor;
            // }

            

            // std::cout << "brightest pixel: " << maxValue * 255 << std::endl;

            // for (auto& photonHit : finalHits)
            // {
            //     if (currentHit % 1000 == 0)
            //     {
            //         std::cout << "hit: " << currentHit << " / " << hitsGenerated << std::endl;
            //     }

            //     for (const auto& sensor : pixelSensors)
            //     {
            //         if (sensor.containsPoint(photonHit.hit.position))
            //         {
            //             auto pixel = image->getPixel(sensor.x, sensor.y);
            //             pixel.red = std::min(pixel.red + static_cast<int>(photonHit.photon.color.red * 255), 255);
            //             pixel.green = std::min(pixel.green + static_cast<int>(photonHit.photon.color.green * 255), 255);
            //             pixel.blue = std::min(pixel.blue + static_cast<int>(photonHit.photon.color.blue * 255), 255);

            //             // std::cout << "set pixel " << sensor.x << ", " << sensor.y << std::endl;

            //             // image->setPixel(sensor.x, sensor.y, workingPixel);
            //             break;
            //         }
            //     }

            //     ++currentHit;
            //     // int sunCross = std::max(0.0f, Vector::dot(sunDirection, photonHit.hit.normal)) * 255;

            //     // workingPixel.red = sunCross;
            //     // workingPixel.green = sunCross;
            //     // workingPixel.blue = sunCross;

            //     // // int distance = std::min(std::max(0.0f, (hit->distance - 30.0f) / 70.0f), 1.0f) * 255;
            //     // // workingPixel.red = distance;
            //     // // workingPixel.green = distance;
            //     // // workingPixel.blue = distance;

            //     // // workingPixel.red = std::abs(hit->normal.x) * 255;
            //     // // workingPixel.green = std::abs(hit->normal.y) * 255;
            //     // // workingPixel.blue = std::abs(hit->normal.z) * 255;

            //     // // workingPixel.red = (0.5f + (hit->normal.x / 2.0f)) * 255;
            //     // // workingPixel.green = (0.5f + (hit->normal.y / 2.0f)) * 255;
            //     // // workingPixel.blue = (0.5f + (hit->normal.z / 2.0f)) * 255;

            //     // // workingPixel.red = hit->coords.x * 255;
            //     // // workingPixel.green = hit->coords.y * 255;
            //     // // workingPixel.blue = hit->coords.z * 255;

            //     // // const Vector& normal = hit->triangle.normal;

            //     // // Vector delta = -direction - normal;
            //     // // workingPixel.red = std::min(std::max(0, static_cast<int>((0.5f + (delta.x * 0.4f)) * 255)), 255);
            //     // // workingPixel.green = std::min(std::max(0, static_cast<int>((0.5f + (delta.y * 0.4f)) * 255)), 255);
            //     // // workingPixel.blue = std::min(std::max(0, static_cast<int>((0.5f + (delta.z * 0.4f)) * 255)), 255);

            //     // // workingPixel.red = (0.5f + (hit->triangle.aNormal.x / 2.0f)) * 255;
            //     // // workingPixel.green = (0.5f + (hit->triangle.aNormal.y / 2.0f)) * 255;
            //     // // workingPixel.blue = (0.5f + (hit->triangle.aNormal.z / 2.0f)) * 255;

            //     // // workingPixel.red = std::abs(delta.x * 0.5f) * 255;
            //     // // workingPixel.green = std::abs(delta.y * 0.5f) * 255;
            //     // // workingPixel.blue = std::abs(delta.z * 0.5f) * 255;

            //     // image->setPixel(photonHit.photon.x, photonHit.photon.y, workingPixel);
            // }

            std::cout << "Finished" << std::endl;

            std::chrono::time_point processHitsEnd = std::chrono::system_clock::now();

            std::chrono::microseconds processHitsDuration = std::chrono::duration_cast<std::chrono::microseconds>(processHitsEnd - processHitsStart);
            int processHitsAverage = 0;

            if (hitsGenerated > 0)
            {
                processHitsAverage = processHitsDuration.count() / hitsGenerated;
            }

            std::chrono::time_point renderEnd = std::chrono::system_clock::now();
            std::chrono::microseconds renderTime = std::chrono::duration_cast<std::chrono::microseconds>(renderEnd - renderStart);

            size_t workDuration = 0;
            size_t pushHitDuration = 0;
            size_t castDuration = 0;

            for (int i = 0; i < workerCount; ++i)
            {
                workDuration += workers[i]->workDuration;
                workers[i]->workDuration = 0;
                pushHitDuration += workers[i]->pushHitDuration;
                workers[i]->pushHitDuration = 0;
                castDuration += workers[i]->castDuration;
                workers[i]->castDuration = 0;
            }

            std::cout << "Render time:" << std::endl;
            std::cout << "|- Total:        " << renderTime.count() / 1000 << " ms" << std::endl;
            std::cout << "|- Average / px: " << renderTime.count() / pixelCount << " us" << std::endl;

            std::cout << "Photons:" << std::endl;
            std::cout << "|- generated:             " << photonsGenerated << std::endl;
            std::cout << "|- generation total time: " << generatePhotonsDuration.count() / 1000.0f << " ms" << std::endl;
            std::cout << "|- generation avg time:   " << generatePhotonsAverage << " us" << std::endl;
            std::cout << "|- process total time:    " << processPhotonsDuration.count() / 1000.0f << " ms" << std::endl;
            std::cout << "|- process avg time:      " << processPhotonsAverage << " us" << std::endl;

            std::cout << "Hits:" << std::endl;
            std::cout << "|- generated:             " << hitsGenerated << std::endl;
            std::cout << "|- generation total time: " << processHitsDuration.count() << " us" << std::endl;
            std::cout << "|- generation avg time:   " << processHitsAverage << " us" << std::endl;

            std::cout << "Workers:" << std::endl;
            std::cout << "|- total duration:    " << workDuration << " us" << std::endl;
            std::cout << "|- push hit duration: " << pushHitDuration << " us" << std::endl;
            std::cout << "|- cast duration:     " << castDuration << " us" << std::endl;

            writeImage("C:\\Users\\ekleeman\\repos\\ray-tracer\\renders\\pipeline_0." + std::to_string(frame) + ".png", *image, "test");
        }

        for (int i = 0; i < workerCount; ++i)
        {
            workers[i]->stop();
        }

        for (int i = 0; i < workerCount; ++i)
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
