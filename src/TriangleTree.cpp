#include "TriangleTree.h"

#include "Utility.h"

#include <algorithm>
#include <iostream>
#include <string>

void addTriangleToNode(const Triangle& newTriangle, std::shared_ptr<Node> node);

float axisMedian(const std::vector<Triangle>& triangles, Axis axis)
{
    size_t size = triangles.size();

    if (size == 0)
    {
        return 0.0f;
    }
    else if (size == 1)
    {
        return triangles[0].center.getAxis(axis);
    }

    std::vector<float> centers;

    for (const auto& triangle : triangles)
    {
        centers.push_back(triangle.center.getAxis(axis));
    }

    std::sort(centers.begin(), centers.end());

    size_t middle = size / 2;
    float median = centers[middle];

    if (size % 2 == 0)
    {
        median += centers[middle - 1];
        median /= 2.0f;
    }

    return median;
}

std::shared_ptr<Node> generateTree(const std::vector<Triangle>& triangles, Axis axis)
{
    // if number of triangles < max amount, create node with page and return
    // else, find median center point
    // everything < median goes in left, > median goes right, if equal
    // create Node for left and right

    // std::cout << "generateTree : " << triangles.size() << " tris, axis: " << static_cast<int>(axis) << std::endl;

    std::shared_ptr<Node> node = std::make_shared<Node>();
    node->axis = axis;
    node->bounds = triangles[0].getBounds();

    for (const auto& triangle : triangles)
    {
        node->bounds += triangle.getBounds();
    }

    if (triangles.size() <= 1)
    {
        node->page = std::make_unique<Page>();
        node->page->contents = triangles;
    }
    else
    {
        node->pivot = axisMedian(triangles, axis);

        std::vector<Triangle> leftTriangles, middleTriangles, rightTriangles;

        for (const auto& triangle : triangles)
        {
            float center = triangle.center.getAxis(axis);
            if (center < node->pivot)
            {
                leftTriangles.push_back(triangle);
            }
            else if (center > node-> pivot)
            {
                rightTriangles.push_back(triangle);
            }
            else
            {
                middleTriangles.push_back(triangle);
            }
        }

        if (leftTriangles.size() > 0)
        {
            // std::cout << "generateTree : left" << std::endl;
            node->left = generateTree(leftTriangles, nextAxis(axis));
        }

        if (rightTriangles.size() > 0)
        {
            // std::cout << "generateTree : right" << std::endl;
            node->right = generateTree(rightTriangles, nextAxis(axis));
        }

        if (middleTriangles.size() > 0)
        {
            node->page = std::make_unique<Page>();
            node->page->contents = middleTriangles;
        }
    }

    return node;
}

TriangleTree::TriangleTree()
    : m_root(std::make_shared<Node>())
{
    m_root->page = std::make_unique<Page>();
}

TriangleTree::TriangleTree(std::vector<Triangle> triangles)
{
    m_root = generateTree(triangles, Axis::X);
}

void rebalanceNode(std::shared_ptr<Node> node)
{
    if (node->page->contents.size() >= 2)
    {
        // std::cout << "Overflow, redistribute" << std::endl;

		node->left = std::make_shared<Node>();
		node->left->depth = node->depth + 1;
		node->left->page = std::make_unique<Page>();

		node->right = std::make_shared<Node>();
		node->right->depth = node->depth + 1;
		node->right->page = std::make_unique<Page>();

        std::vector<Triangle> contents = node->page->contents;
		node->page = nullptr;

        // std::cout << "Re-adding triangles" << std::endl;

        for (const auto& triangle : contents)
        {
            addTriangleToNode(triangle, node);
        }
    }
}

void recalculatePivot(std::shared_ptr<Node> node)
{
    if (!node->page)
    {
        return;
    }

    Vector center;

    for (const auto& triangle : node->page->contents)
    {
        center += triangle.center;
    }

    center /= static_cast<float>(node->page->contents.size());

    switch (node->depth % 3)
    {
        case 0:
            node->pivot = center.x;
            break;

        case 1:
            node->pivot = center.y;
            break;

        case 2:
            node->pivot = center.z;
            break;
    }
}

void addTriangleToNode(const Triangle& newTriangle, std::shared_ptr<Node> node)
{
    std::shared_ptr<Node> currentNode = node;

    while (currentNode->page == nullptr && currentNode->left && currentNode->right)
    {
        float test = 0.0f;

        switch (currentNode->depth % 3)
        {
            case 0:
                test = newTriangle.center.x;
                break;

            case 1:
                test = newTriangle.center.y;
                break;

            case 2:
                test = newTriangle.center.z;
                break;
        }

        // std::cout << test << " vs " << currentNode->pivot << std::endl;

        if (test <= currentNode->pivot)
        {
            // std::cout << "Go down left node" << std::endl;
            currentNode = currentNode->left;
        }
        else
        {
            // std::cout << "Go down right node" << std::endl;
            currentNode = currentNode->right;
        }
    }

    if (currentNode->page == nullptr)
    {
        // Error, reached malformed node
        return;
    }

    // std::cout << "Add triangle to page" << std::endl;
    currentNode->page->contents.push_back(newTriangle);

    recalculatePivot(currentNode);
    rebalanceNode(currentNode);
}

void TriangleTree::addTriangle(Triangle newTriangle)
{
    addTriangleToNode(newTriangle, m_root);
}

void printNode(const std::string& name, std::shared_ptr<Node> node)
{
    if (node->left)
    {
        printNode(name + ".left", node->left);
    }

    if (node->page)
    {
        Limits limits = node->bounds.getLimits(node->axis);

        if (node->page->contents.size() == 0)
        {
            std::cout << name << "(" << limits.min << ", " << limits.max << "): empty" << std::endl;
        }
        else
        {
            for (const auto& triangle : node->page->contents)
            {
				std::cout << name << "(" << limits.min << ", " << limits.max << "): ";
				printVector(triangle.center);
				std::cout << std::endl;
            }
        }
    }

    if (node->right)
    {
        printNode(name + ".right", node->right);
    }
}

void TriangleTree::print()
{
    printNode("root", m_root);
}

std::shared_ptr<Node> TriangleTree::root()
{
    return m_root;
}

void fetchTrianglesIntersectingBoundsFromNode(const Bounds& bounds, std::shared_ptr<Node> node, std::vector<Triangle>& results)
{
    if (node->bounds.intersects(bounds))
    {

        if (node->page)
        {
            results.insert(results.end(), node->page->contents.begin(), node->page->contents.end());
        }

        if (node->left)
        {
            fetchTrianglesIntersectingBoundsFromNode(bounds, node->left, results);
        }

        if (node->right)
        {
            fetchTrianglesIntersectingBoundsFromNode(bounds, node->right, results);
        }
    }
}

std::vector<Triangle> TriangleTree::fetchTrianglesIntersectingBounds(const Bounds& bounds) const
{
    std::vector<Triangle> results;

    fetchTrianglesIntersectingBoundsFromNode(bounds, m_root, results);

    return results;
}

void fetchTrianglesIntersectingRayFromNode(const Ray& ray, std::shared_ptr<Node> node, std::vector<Triangle>& results)
{
    if (rayIntersectsBounds(ray, node->bounds))
    {
        if (node->page)
        {
            for (const auto& triangle : node->page->contents)
            {
                if (rayIntersectsTriangle(ray, triangle))
                {
                    results.push_back(triangle);
                }
            }
        }

        if (node->left)
        {
            fetchTrianglesIntersectingRayFromNode(ray, node->left, results);
        }

        if (node->right)
        {
            fetchTrianglesIntersectingRayFromNode(ray, node->right, results);
        }
    }
}

std::vector<Triangle> TriangleTree::fetchTrianglesIntersectingRay(const Ray& ray) const
{
    std::vector<Triangle> results;

    fetchTrianglesIntersectingRayFromNode(ray, m_root, results);

    return results;
}
