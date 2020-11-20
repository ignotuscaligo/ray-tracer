#pragma once

#include "Point.h"
#include "Triangle.h"

#include <memory>
#include <vector>

struct Page
{
    std::vector<Triangle> contents;
};

struct Node
{
    Axis axis;
    float pivot;
    Bounds bounds;
    int depth;
    std::shared_ptr<Node> left;
    std::shared_ptr<Node> right;
    std::unique_ptr<Page> page;
};

class TriangleTree
{
public:
    TriangleTree();
    TriangleTree(std::vector<Triangle> triangles);
    ~TriangleTree() = default;

    void addTriangle(Triangle triangle);
    void print();
    std::shared_ptr<Node> root();
    std::vector<Triangle> fetchTrianglesIntersectingBounds(const Bounds& bounds) const;

private:
    std::shared_ptr<Node> m_root;
};