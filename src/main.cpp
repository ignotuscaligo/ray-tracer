#include "Image.h"
#include "Pixel.h"
#include "PngWriter.h"
#include "TriangleTree.h"
#include "Utility.h"

#include <iostream>
#include <string>

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

    std::cout << "Running Image test" << std::endl;

    Image image(640, 480);
    Pixel workingPixel;

    float pixelCount = image.width() * image.height();

    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            int index = (y * image.width()) + x;
            workingPixel.red = ((1.0f / pixelCount) * index) * 255;
            workingPixel.green = y % 4 < 2 ? 255 : 0;
            workingPixel.blue = x % 4 >= 2 ? 255 : 0;

            image.setPixel(x, y, workingPixel);
        }
    }

    writeImage("test.png", image, "test");

    std::cout << "Running TriangleTree test" << std::endl;

    Triangle xyTriangle{
        {-0.5f, -0.5f, 0.0f},
        {0.5f, -0.5f, 0.0f},
        {0.0f, 0.5f, 0.0f}
    };

    Triangle zyTriangle{
        {0.0f, -0.5f, -0.5f},
        {0.0f, -0.5f, 0.5f},
        {0.0f, 0.5f, 0.0f}
    };

    Triangle xzTriangle{
        {0.0f, -0.5f, -0.5f},
        {0.0f, 0.5f, -0.5f},
        {0.0f, 0.0f, 0.5f}
    };

    std::vector<Triangle> triangles = {{
        xyTriangle + Point(-2, 2, 0),
        xyTriangle + Point(-2, -2, 0),
        xyTriangle + Point(0, 0, 0),
        xyTriangle + Point(2, 2, 0),
        xyTriangle + Point(2, -2, 0)
    }};

    std::cout << "input triangles:" << std::endl;
    for (const auto& triangle : triangles)
    {
        printTriangle(triangle);
        std::cout << std::endl;
    }

    TriangleTree directTree = TriangleTree(triangles);
    directTree.print();

    Bounds bounds = directTree.root()->bounds;

    std::cout << "root bounds:" << std::endl;
    std::cout << "X: " << bounds.x.min << ", " << bounds.x.max << std::endl;
    std::cout << "Y: " << bounds.y.min << ", " << bounds.y.max << std::endl;
    std::cout << "Z: " << bounds.z.min << ", " << bounds.z.max << std::endl;

    Bounds testBounds{
        {0.0f, 2.0f},
        {-2.0f, 2.0f},
        {-1.0f, 1.0f},
    };

    std::vector<Triangle> fetchedTriangles = directTree.fetchTrianglesIntersectingBounds(testBounds);

    std::cout << "fetched triangles:" << std::endl;
    for (const auto& triangle : fetchedTriangles)
    {
        printTriangle(triangle);
        std::cout << std::endl;
    }


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

    std::cout << "Goodbye!" << std::endl;

    return 0;
}
