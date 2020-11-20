#define TINYOBJLOADER_IMPLEMENTATION

#include "Image.h"
#include "Pixel.h"
#include "PngWriter.h"
#include "TriangleTree.h"
#include "Utility.h"

#include <tiny_obj_loader.h>

#include <array>
#include <cmath>
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

    // std::cout << "Running TriangleTree test" << std::endl;

    // Triangle xyTriangle{
    //     {-0.5f, -0.5f, 0.0f},
    //     {0.5f, -0.5f, 0.0f},
    //     {0.0f, 0.5f, 0.0f}
    // };

    // Triangle zyTriangle{
    //     {0.0f, -0.5f, -0.5f},
    //     {0.0f, -0.5f, 0.5f},
    //     {0.0f, 0.5f, 0.0f}
    // };

    // Triangle xzTriangle{
    //     {0.0f, -0.5f, -0.5f},
    //     {0.0f, 0.5f, -0.5f},
    //     {0.0f, 0.0f, 0.5f}
    // };

    // std::vector<Triangle> triangles = {{
    //     xyTriangle + Point(-2, 2, 0),
    //     xyTriangle + Point(-2, -2, 0),
    //     xyTriangle + Point(0, 0, 0),
    //     xyTriangle + Point(2, 2, 0),
    //     xyTriangle + Point(2, -2, 0)
    // }};

    // std::cout << "input triangles:" << std::endl;
    // for (const auto& triangle : triangles)
    // {
    //     printTriangle(triangle);
    //     std::cout << std::endl;
    // }

    // TriangleTree directTree = TriangleTree(triangles);
    // directTree.print();

    // Bounds bounds = directTree.root()->bounds;

    // std::cout << "root bounds:" << std::endl;
    // std::cout << "X: " << bounds.x.min << ", " << bounds.x.max << std::endl;
    // std::cout << "Y: " << bounds.y.min << ", " << bounds.y.max << std::endl;
    // std::cout << "Z: " << bounds.z.min << ", " << bounds.z.max << std::endl;

    // Bounds testBounds{
    //     {0.0f, 2.0f},
    //     {-2.0f, 2.0f},
    //     {-1.0f, 1.0f},
    // };

    // std::vector<Triangle> fetchedTriangles = directTree.fetchTrianglesIntersectingBounds(testBounds);

    // std::cout << "fetched triangles:" << std::endl;
    // for (const auto& triangle : fetchedTriangles)
    // {
    //     printTriangle(triangle);
    //     std::cout << std::endl;
    // }

    // TriangleTree tree;

    // tree.addTriangle(xyTriangle + Point(-2, 0, 0));

    // tree.print();

    // tree.addTriangle(xyTriangle + Point(-1, 0, 0));

    // tree.print();

    // tree.addTriangle(xyTriangle + Point(0, 0, 0));

    // tree.print();

    // tree.addTriangle(xyTriangle + Point(1, 0, 0));

    // tree.print();

    // tree.addTriangle(xyTriangle + Point(2, 0, 0));

    // tree.print();

    // tree.addTriangle(xyTriangle + Point(-1, -2, 0));
    // tree.addTriangle(xyTriangle + Point(-1, -1, 0));
    // tree.addTriangle(xyTriangle + Point(-1, 0, 0));
    // tree.addTriangle(xyTriangle + Point(-1, 1, 0));
    // tree.addTriangle(xyTriangle + Point(-1, 2, 0));

    // tree.print();

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
    std::array<Point, 3> points;

    if (result)
    {
        std::cout << "Loaded obj successfully" << std::endl;

        std::cout << "Found " << shapes.size() << " shapes" << std::endl;

        for (const auto& shape : shapes)
        {
            std::cout << "num_face_vertices: " << shape.mesh.num_face_vertices.size() << std::endl;

            size_t indexOffset = 0;

            for (const int vertexCount : shape.mesh.num_face_vertices)
            {
                for (size_t v = 0; v < vertexCount; ++v)
                {
                    tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];
                    size_t vertexIndex = 3 * idx.vertex_index;
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
    Point origin{0.0f, 0.0f, -70.0f};

    float pixelCount = image.width() * image.height();

    float horizontalFov = 80.0f;
    float verticalFov = 80.0f;

    for (int y = 0; y < image.height(); ++y)
    {
        float angleY = (verticalFov / 2.0f) - ((y / (image.height() - 1.0f)) * verticalFov);

        for (int x = 0; x < image.width(); ++x)
        {
            float angleX = (horizontalFov / 2.0f) - ((x / (image.width() - 1.0f)) * horizontalFov);

            Point direction{
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

                const Point& normal = closestTriangle.normal;
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
        }
    }

    writeImage("test.png", image, "test");

    std::cout << "Goodbye!" << std::endl;

    return 0;
}
