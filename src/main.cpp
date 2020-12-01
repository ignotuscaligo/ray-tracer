#define TINYOBJLOADER_IMPLEMENTATION

#include "Image.h"
#include "Pixel.h"
#include "PngWriter.h"
#include "TriangleTree.h"
#include "Utility.h"
#include "Object.h"
#include "Quaternion.h"

#include <tiny_obj_loader.h>

#include <array>
#include <cmath>
#include <chrono>
#include <iostream>
#include <string>
#include <limits>

void writeImage(const std::string& filename, Image& image, const std::string& title)
{
    std::cout << "writeImage " << filename << std::endl;
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

    std::shared_ptr<Object> root = std::make_unique<Object>("Root");
    std::shared_ptr<Object> knot = std::make_unique<Object>("Knot");
    std::shared_ptr<Object> camera = std::make_unique<Object>("Camera");

    Object::setParent(knot, root);
    Object::setParent(camera, root);

    Quaternion quat0 = Quaternion::fromPitchYawRoll(1.5707963267f, 0, 0);
    Quaternion quat1 = Quaternion::fromPitchYawRoll(0, 1.5707963267f, 0);
    Quaternion quat2 = Quaternion::fromPitchYawRoll(0, 0, 1.5707963267f);

    std::cout << "quat0: " << quat0.x << ", " << quat0.y << ", " << quat0.z << ", " << quat0.w << std::endl;
    std::cout << "quat1: " << quat1.x << ", " << quat1.y << ", " << quat1.z << ", " << quat1.w << std::endl;
    std::cout << "quat2: " << quat2.x << ", " << quat2.y << ", " << quat2.z << ", " << quat2.w << std::endl;

    std::cout << "OBJ loader test" << std::endl;

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

    if (result)
    {
        std::cout << "Loaded obj successfully" << std::endl;

        std::cout << "Found " << shapes.size() << " shapes" << std::endl;

        for (const auto& shape : shapes)
        {
            std::cout << "num_face_vertices: " << shape.mesh.num_face_vertices.size() << std::endl;

            int indexOffset = 0;

            for (const int vertexCount : shape.mesh.num_face_vertices)
            {
                for (int v = 0; v < vertexCount; ++v)
                {
                    tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];
                    int vertexIndex = 3 * idx.vertex_index;
                    points[v].x = attrib.vertices[vertexIndex + 0];
                    points[v].y = attrib.vertices[vertexIndex + 1];
                    points[v].z = attrib.vertices[vertexIndex + 2];
                }

                objTriangles.emplace_back(points[0], points[1], points[2]);

                indexOffset += vertexCount;
            }
        }
    }
    else
    {
        std::cout << "Failed to load obj" << std::endl;
    }

    std::cout << "Loaded " << objTriangles.size() << " triangles" << std::endl;

    std::cout << "Generating triangle tree from OBJ" << std::endl;

    TriangleTree objTree = TriangleTree(objTriangles);

    Bounds bounds = objTree.root()->bounds;

    std::cout << "root bounds:" << std::endl;
    std::cout << "X: " << bounds.x.min << ", " << bounds.x.max << std::endl;
    std::cout << "Y: " << bounds.y.min << ", " << bounds.y.max << std::endl;
    std::cout << "Z: " << bounds.z.min << ", " << bounds.z.max << std::endl;

    std::cout << "Running Image test" << std::endl;

    Image image(1024, 1024);
    Pixel workingPixel;
    Vector origin{0.0f, 0.0f, -70.0f};

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

            std::vector<Triangle> hitTriangles = objTree.fetchTrianglesIntersectingRay({origin, direction});

            if (hitTriangles.size() > 0)
            {
                Triangle closestTriangle;
                float lowestDistance = std::numeric_limits<float>::max();

                for (const auto& triangle : hitTriangles)
                {
                    float distance = (triangle.center - origin).magnitude();

                    if (distance < lowestDistance)
                    {
                        lowestDistance = distance;
                        closestTriangle = triangle;
                    }
                }

                const Vector& normal = closestTriangle.normal;
                workingPixel.red = std::abs(normal.x) * 255;
                workingPixel.green = std::abs(normal.y) * 255;
                workingPixel.blue = std::abs(normal.z) * 255;
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

    writeImage("test.png", image, "test");

    std::cout << "Goodbye!" << std::endl;

    return 0;
}
