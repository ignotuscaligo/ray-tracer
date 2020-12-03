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
#include "PhotonQueue.h"

#include <tiny_obj_loader.h>

#include <array>
#include <cmath>
#include <chrono>
#include <iostream>
#include <string>
#include <limits>

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

    std::vector<Photon> dummyQueue(10);

    int index = 0;

    for (auto& photon : dummyQueue)
    {
        photon.color.red = index;
        ++index;
    }

    PhotonBlock testBlock(5, 15, dummyQueue);

    for (auto& photon : testBlock)
    {
        std::cout << "photon iteration: " << photon.color.red << std::endl;
    }

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

    Image image(512, 512);
    Pixel workingPixel;

    camera->transform.position = {0.0f, 0.0f, -70.0f};

    sun->transform.rotation = Quaternion::fromPitchYawRoll(45.0f, 0.0f, 0.0f);

    Vector sunDirection = -sun->transform.forward();

    int startFrame = 0;
    int frameCount = 1;

    for (int frame = startFrame; frame < startFrame + frameCount; ++frame)
    {
        std::cout << "Rendering frame " << frame << std::endl;

        cameraPivot->transform.rotation = Quaternion::fromPitchYawRoll(0, radians(frame * 10.0f), 0);

        Vector cameraPosition = camera->position();
        Quaternion cameraRotation = camera->rotation();
        Vector cameraForward = camera->transform.forward();

        float pixelCount = image.width() * image.height();

        float horizontalFov = 80.0f;
        float verticalFov = 80.0f;

        std::chrono::time_point renderStart = std::chrono::system_clock::now();
        int pixelMinDuration = 999;
        int pixelMaxDuration = 0;

        for (int y = 0; y < image.height(); ++y)
        {
            float angleY = (verticalFov / 2.0f) - ((y / (image.height() - 1.0f)) * verticalFov);

            for (int x = 0; x < image.width(); ++x)
            {
                std::chrono::time_point pixelStart = std::chrono::system_clock::now();
                float angleX = (horizontalFov / 2.0f) - ((x / (image.width() - 1.0f)) * horizontalFov);

                Vector direction{
                    std::sin(radians(angleX)),
                    std::sin(radians(angleY)),
                    std::cos(radians(angleX))
                };

                direction.normalize();

                direction = cameraRotation * direction;

                std::optional<Hit> hit = knotMesh.castRay({cameraPosition, direction});

                if (hit)
                {
                    int sunCross = std::max(0.0f, Vector::dot(sunDirection, hit->normal)) * 255;

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
                }
                else
                {
                    workingPixel.red = 0;
                    workingPixel.green = 0;
                    workingPixel.blue = 0;
                }

                image.setPixel(x, y, workingPixel);

                std::chrono::time_point pixelEnd = std::chrono::system_clock::now();
                int pixelDuration = std::chrono::duration_cast<std::chrono::microseconds>(pixelEnd - pixelStart).count();
                pixelMinDuration = std::min(pixelDuration, pixelMinDuration);
                pixelMaxDuration = std::max(pixelDuration, pixelMaxDuration);
            }
        }

        std::chrono::time_point renderEnd = std::chrono::system_clock::now();
        std::chrono::microseconds renderTime = std::chrono::duration_cast<std::chrono::microseconds>(renderEnd - renderStart);

        std::cout << "Render time:" << std::endl;
        std::cout << "Total:   " << renderTime.count() / 1000 << " ms" << std::endl;
        std::cout << "Average: " << renderTime.count() / pixelCount << " us" << std::endl;
        std::cout << "Minimum: " << pixelMinDuration << " us" << std::endl;
        std::cout << "Maximum: " << pixelMaxDuration << " us" << std::endl;

        writeImage("sun_test_0." + std::to_string(frame) + ".png", image, "test");
    }

    std::cout << "Goodbye!" << std::endl;

    return 0;
}
