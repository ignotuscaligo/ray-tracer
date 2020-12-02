#include "TriangleTree.h"

#include "Utility.h"

#include <algorithm>
#include <iostream>
#include <string>

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

TriangleTree::TriangleTree(const std::vector<Triangle>& triangles)
{
    m_root = generateTree(triangles, Axis::X);
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

void castRayIntoNode(const Ray& ray, std::shared_ptr<Node> node, std::vector<Hit>& hits)
{
    std::optional<Hit> hit;

    if (rayIntersectsBounds(ray, node->bounds))
    {
        if (node->page)
        {
            for (const auto& triangle : node->page->contents)
            {
                hit = rayIntersectsTriangle(ray, triangle);

                if (hit)
                {
                    hits.push_back(*hit);
                }
            }
        }

        if (node->left)
        {
            castRayIntoNode(ray, node->left, hits);
        }

        if (node->right)
        {
            castRayIntoNode(ray, node->right, hits);
        }
    }
}

std::optional<Hit> TriangleTree::castRay(const Ray& ray) const
{
    std::vector<Hit> hits;

    castRayIntoNode(ray, m_root, hits);

    float minDistance;
    std::optional<Hit> result;

    for (const auto& hit : hits)
    {
        if (!result || hit.distance < minDistance)
        {
            result = hit;
            minDistance = hit.distance;
        }
    }

    return result;
}
