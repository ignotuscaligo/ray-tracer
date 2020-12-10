#pragma once

#include "Bounds.h"
#include "Vector.h"

#include <memory>
#include <optional>
#include <vector>

struct Hit;
struct Pyramid;
struct Ray;

template<typename T>
class Tree
{
public:
    struct Node
    {
        struct Page
        {
            std::vector<T> contents;
        };

        size_t size() const;
        size_t nodeCount() const;
        size_t nodeDepth() const;

        Axis axis = Axis::X;
        float pivot = 0;
        Bounds bounds;
        int depth = 0;
        std::shared_ptr<Node> left;
        std::shared_ptr<Node> right;
        std::unique_ptr<Page> page;
    };

    Tree() = delete;
    Tree(const std::vector<T>& objects, size_t pageSize = 1);
    ~Tree() = default;

    std::shared_ptr<Node> root();
    size_t size() const;
    size_t nodeCount() const;
    size_t nodeDepth() const;
    std::optional<Hit> castRay(const Ray& ray) const;
    std::vector<T> fetchWithinPyramid(const Pyramid& pyramid) const;

private:
    static const Vector& getPivot(const T& object);
    static float getPivot(const T& object, Axis axis);
    static Bounds getBounds(const T& object);
    static float axisMedian(const std::vector<T>& objects, Axis axis);
    static void castRayIntoNode(const Ray& ray, std::shared_ptr<Node> node, std::vector<Hit>& hits);
    static void fetchWithinPyramidFromNode(const Pyramid& pyramid, std::shared_ptr<Node> node, std::vector<T>& objects);
    static std::optional<Hit> rayIntersectsObject(const Ray& ray, const T& object);

    std::shared_ptr<Node> generateTree(const std::vector<T>& objects, Axis axis);

    const size_t m_pageSize;
    std::shared_ptr<Node> m_root;
};
