#pragma once

#include "Hit.h"
#include "Ray.h"
#include "Triangle.h"

#include <memory>
#include <vector>

class TriangleTree
{
public:
    struct Node
    {
        struct Page
        {
            std::vector<Triangle> contents;
        };

        Axis axis;
        float pivot;
        Bounds bounds;
        int depth;
        std::shared_ptr<Node> left;
        std::shared_ptr<Node> right;
        std::unique_ptr<Page> page;
    };

    TriangleTree() = delete;
    TriangleTree(const std::vector<Triangle>& triangles);
    ~TriangleTree() = default;

    void print();
    std::shared_ptr<Node> root();
    std::optional<Hit> castRay(const Ray& ray) const;

private:
    static float getPivot(const Triangle& triangle, Axis axis);
    static float axisMedian(const std::vector<Triangle>& triangles, Axis axis);
    static void castRayIntoNode(const Ray& ray, std::shared_ptr<Node> node, std::vector<Hit>& hits);

    std::shared_ptr<TriangleTree::Node> generateTree(const std::vector<Triangle>& triangles, Axis axis);

    std::shared_ptr<Node> m_root;
};
