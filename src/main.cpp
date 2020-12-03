#define TINYOBJLOADER_IMPLEMENTATION

#include "Image.h"
#include "MeshVolume.h"
#include "Object.h"
#include "Pixel.h"
#include "PngWriter.h"
#include "Quaternion.h"
#include "TriangleTree.h"
#include "Utility.h"
#include "Photon.h"
#include "WorkQueue.h"

#include <tiny_obj_loader.h>

#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <string>

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

int main(int argc, char** argv)
{
    std::cout << "Hello!" << std::endl;

    try
    {
        std::cout << "Setting up scene for render" << std::endl;

        std::shared_ptr<Object> root = std::make_unique<Object>("Root");
        std::shared_ptr<Object> knot = std::make_unique<Object>("Knot");
        std::shared_ptr<Object> cameraPivot = std::make_unique<Object>("CameraPivot");
        std::shared_ptr<Object> camera = std::make_unique<Object>("Camera");
        std::shared_ptr<Object> sun = std::make_unique<Object>("Sun");

        Object::setParent(knot, root);
        Object::setParent(cameraPivot, root);
        Object::setParent(camera, cameraPivot);
        Object::setParent(sun, root);

        std::cout << "Loading OBJ" << std::endl;

        std::string inputFile = R"(C:\Users\ekleeman\Documents\Cinema 4D\eschers_knot.obj)";

        tinyobj::attrib_t attrib;

        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;

        std::string warn;
        std::string err;

        bool result = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, inputFile.c_str());

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
                int indexOffset = 0;

                for (const int vertexCount : shape.mesh.num_face_vertices)
                {
                    for (int v = 0; v < vertexCount; ++v)
                    {
                        tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];
                        int vertexIndex = 3 * idx.vertex_index;
                        int normalIndex = 3 * idx.normal_index;
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
        MeshVolume knotMesh{objTriangles};

        Image image(1024, 1024);
        Pixel workingPixel;

        std::cout << "Rendering image at " << image.width() << " px by " << image.height() << " px" << std::endl;

        camera->transform.position = {0.0f, 0.0f, -70.0f};

        sun->transform.rotation = Quaternion::fromPitchYawRoll(45.0f, 0.0f, 0.0f);

        Vector sunDirection = -sun->transform.forward();

        std::cout << "Creating queues" << std::endl;
        WorkQueue<Photon> photonQueue((image.width() * image.height()));
        WorkQueue<PhotonHit> hitQueue((image.width() * image.height()));

        int startFrame = 0;
        int frameCount = 1;

        for (int frame = startFrame; frame < startFrame + frameCount; ++frame)
        {
            std::cout << "Rendering frame " << frame << std::endl;

            image.clear();

            cameraPivot->transform.rotation = Quaternion::fromPitchYawRoll(0, radians(frame * 10.0f), 0);

            Vector cameraPosition = camera->position();
            Quaternion cameraRotation = camera->rotation();
            Vector cameraForward = camera->transform.forward();

            float pixelCount = image.width() * image.height();

            float horizontalFov = 80.0f;
            float verticalFov = 80.0f;

            std::chrono::time_point renderStart = std::chrono::system_clock::now();
            int rowMinDuration = INT_MAX;
            int rowMaxDuration = 0;

            std::chrono::time_point generatePhotonsStart = std::chrono::system_clock::now();

            std::cout << "Generating photons" << std::endl;
            for (int y = 0; y < image.height(); ++y)
            {
                float pitch = -((verticalFov / 2.0f) - ((y / (image.height() - 1.0f)) * verticalFov));

                std::chrono::time_point rowStart = std::chrono::system_clock::now();

                auto photons = photonQueue.initialize(image.width());

                for (int x = 0; x < image.width(); ++x)
                {
                    float yaw = (horizontalFov / 2.0f) - ((x / (image.width() - 1.0f)) * horizontalFov);

                    Vector manualDirection{
                        std::sin(radians(yaw)),
                        std::sin(radians(pitch)),
                        std::cos(radians(yaw))
                    };

                    manualDirection.normalize();

                    Quaternion pixelAngle = Quaternion::fromPitchYawRoll(radians(pitch), radians(yaw), 0);
                    Vector direction = pixelAngle * cameraForward;

                    manualDirection = cameraRotation * manualDirection;

                    photons[x].ray = {cameraPosition, direction};
                    photons[x].x = x;
                    photons[x].y = y;

                    // std::cout << "pitch / yaw: " << pitch << ", " << yaw << std::endl;
                    // std::cout << "cameraForward: " << cameraForward.x << ", " << cameraForward.y << ", " << cameraForward.z << std::endl;
                    // std::cout << "direction: " << direction.x << ", " << direction.y << ", " << direction.z << std::endl;
                    // std::cout << "manualDirection: " << manualDirection.x << ", " << manualDirection.y << ", " << manualDirection.z << std::endl;

                    // std::cout << "available: " << photonQueue.available() << std::endl;
                }

                photonQueue.ready(photons);

                std::chrono::time_point rowEnd = std::chrono::system_clock::now();

                std::chrono::microseconds rowDuration = std::chrono::duration_cast<std::chrono::microseconds>(rowEnd - rowStart);

                rowMinDuration = std::min(static_cast<int>(rowDuration.count()), rowMinDuration);
                rowMaxDuration = std::max(static_cast<int>(rowDuration.count()), rowMaxDuration);

                // std::cout << "row " << y + 1 << " / " << image.height() << " took " << std::chrono::duration_cast<std::chrono::microseconds>(rowEnd - rowStart).count() << " us" << std::endl;
            }

            std::chrono::time_point generatePhotonsEnd = std::chrono::system_clock::now();

            size_t photonsGenerated = photonQueue.available();

            std::cout << "Cast " << photonsGenerated << " photons" << std::endl;

            std::cout << "Calculating average" << std::endl;
            std::chrono::microseconds generatePhotonsDuration = std::chrono::duration_cast<std::chrono::microseconds>(generatePhotonsEnd - generatePhotonsStart);
            float generatePhotonsAverage = 0;

            if (photonsGenerated > 0)
            {
                generatePhotonsAverage = generatePhotonsDuration.count() / static_cast<float>(photonsGenerated);
            }

            std::chrono::time_point processPhotonsStart = std::chrono::system_clock::now();

            std::cout << "Casting " << photonQueue.available() << " photons" << std::endl;

            auto photons = photonQueue.fetch(photonQueue.available());

            std::cout << "photons.startIndex: " << photons.startIndex << std::endl;
            std::cout << "photons.endIndex:   " << photons.endIndex << std::endl;

            for (auto& photon : photons)
            {
                // std::cout << "photon.ray: " << photon.ray.origin.x << ", " << photon.ray.origin.y << ", " << photon.ray.origin.z << std::endl;
                std::optional<Hit> hit = knotMesh.castRay(photon.ray);

                if (hit)
                {
                    auto hits = hitQueue.initialize(1);
                    hits[0].photon = photon;
                    hits[0].hit = *hit;
                    hitQueue.ready(hits);
                }
            }

            photonQueue.release(photons);

            std::chrono::time_point processPhotonsEnd = std::chrono::system_clock::now();

            std::chrono::microseconds processPhotonsDuration = std::chrono::duration_cast<std::chrono::microseconds>(processPhotonsEnd - processPhotonsStart);
            int processPhotonsAverage = 0;

            if (photonsGenerated > 0)
            {
                processPhotonsAverage = processPhotonsDuration.count() / photonsGenerated;
            }

            size_t hitsGenerated = hitQueue.available();

            std::cout << hitsGenerated << " photons hit" << std::endl;

            std::chrono::time_point processHitsStart = std::chrono::system_clock::now();

            auto hits = hitQueue.fetch(hitQueue.available());

            for (auto& photonHit : hits)
            {
                int sunCross = std::max(0.0f, Vector::dot(sunDirection, photonHit.hit.normal)) * 255;

                workingPixel.red = sunCross;
                workingPixel.green = sunCross;
                workingPixel.blue = sunCross;

                // int distance = std::min(std::max(0.0f, (hit->distance - 30.0f) / 70.0f), 1.0f) * 255;
                // workingPixel.red = distance;
                // workingPixel.green = distance;
                // workingPixel.blue = distance;

                // workingPixel.red = std::abs(hit->normal.x) * 255;
                // workingPixel.green = std::abs(hit->normal.y) * 255;
                // workingPixel.blue = std::abs(hit->normal.z) * 255;

                // workingPixel.red = (0.5f + (hit->normal.x / 2.0f)) * 255;
                // workingPixel.green = (0.5f + (hit->normal.y / 2.0f)) * 255;
                // workingPixel.blue = (0.5f + (hit->normal.z / 2.0f)) * 255;

                // workingPixel.red = hit->coords.x * 255;
                // workingPixel.green = hit->coords.y * 255;
                // workingPixel.blue = hit->coords.z * 255;

                // const Vector& normal = hit->triangle.normal;

                // Vector delta = -direction - normal;
                // workingPixel.red = std::min(std::max(0, static_cast<int>((0.5f + (delta.x * 0.4f)) * 255)), 255);
                // workingPixel.green = std::min(std::max(0, static_cast<int>((0.5f + (delta.y * 0.4f)) * 255)), 255);
                // workingPixel.blue = std::min(std::max(0, static_cast<int>((0.5f + (delta.z * 0.4f)) * 255)), 255);

                // workingPixel.red = (0.5f + (hit->triangle.aNormal.x / 2.0f)) * 255;
                // workingPixel.green = (0.5f + (hit->triangle.aNormal.y / 2.0f)) * 255;
                // workingPixel.blue = (0.5f + (hit->triangle.aNormal.z / 2.0f)) * 255;

                // workingPixel.red = std::abs(delta.x * 0.5f) * 255;
                // workingPixel.green = std::abs(delta.y * 0.5f) * 255;
                // workingPixel.blue = std::abs(delta.z * 0.5f) * 255;

                image.setPixel(photonHit.photon.x, photonHit.photon.y, workingPixel);
            }

            hitQueue.release(hits);

            std::chrono::time_point processHitsEnd = std::chrono::system_clock::now();

            std::chrono::microseconds processHitsDuration = std::chrono::duration_cast<std::chrono::microseconds>(processHitsEnd - processHitsStart);
            int processHitsAverage = 0;

            if (hitsGenerated > 0)
            {
                processHitsAverage = processHitsDuration.count() / hitsGenerated;
            }

            std::chrono::time_point renderEnd = std::chrono::system_clock::now();
            std::chrono::microseconds renderTime = std::chrono::duration_cast<std::chrono::microseconds>(renderEnd - renderStart);

            std::cout << "Render time:" << std::endl;
            std::cout << "Total:   " << renderTime.count() / 1000 << " ms" << std::endl;
            std::cout << "Average: " << renderTime.count() / pixelCount << " us" << std::endl;
            std::cout << "Row Minimum: " << rowMinDuration << " us" << std::endl;
            std::cout << "Row Maximum: " << rowMaxDuration << " us" << std::endl;
            std::cout << "Photons generated:             " << photonsGenerated << std::endl;
            std::cout << "Photons generation total time: " << generatePhotonsDuration.count() << " us" << std::endl;
            std::cout << "Photons generation avg time:   " << generatePhotonsAverage << " us" << std::endl;
            std::cout << "Photons process total time:    " << processPhotonsDuration.count() << " us" << std::endl;
            std::cout << "Photons process avg time:      " << processPhotonsAverage << " us" << std::endl;
            std::cout << "Hits generated:                " << hitsGenerated << std::endl;
            std::cout << "Hits generation total time:    " << processHitsDuration.count() << " us" << std::endl;
            std::cout << "Hits generation avg time:      " << processHitsAverage << " us" << std::endl;

            writeImage("sun_test_0." + std::to_string(frame) + ".png", image, "test");
        }
    }
    catch (const std::exception& e)
    {
        std::cout << "ERROR: " << e.what() << std::endl;
    }

    std::cout << "Goodbye!" << std::endl;

    return 0;
}
