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

        size_t size() const noexcept;
        size_t nodeCount() const noexcept;
        size_t nodeDepth() const noexcept;

        Axis axis = Axis::X;
        double pivot = 0;
        Bounds bounds;
        int depth = 0;
        std::shared_ptr<Node> left;
        std::shared_ptr<Node> right;
        std::unique_ptr<Page> page;
    };

    Tree(const std::vector<T>& objects, size_t pageSize = 1);

    std::shared_ptr<Node> root() noexcept;
    size_t size() const noexcept;
    size_t nodeCount() const noexcept;
    size_t nodeDepth() const noexcept;
    std::optional<Hit> castRay(const Ray& ray) const;
    std::vector<T> fetchWithinPyramid(const Pyramid& pyramid) const noexcept;

private:
    static const Vector& getPivot(const T& object) noexcept;
    static double getPivot(const T& object, Axis axis) noexcept;
    static Bounds getBounds(const T& object) noexcept;
    static double axisMedian(const std::vector<T>& objects, Axis axis);
    static void castRayIntoNode(const Ray& ray, Node& node, std::vector<Hit>& hits);
    static void fetchWithinPyramidFromNode(const Pyramid& pyramid, std::shared_ptr<Node> node, std::vector<T>& objects) noexcept;
    static std::optional<Hit> rayIntersectsObject(const Ray& ray, const T& object) noexcept;

    std::shared_ptr<Node> generateTree(const std::vector<T>& objects, Axis axis);

    const size_t m_pageSize;
    std::shared_ptr<Node> m_root;
};
