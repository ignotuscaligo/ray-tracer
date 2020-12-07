#include "Tree.h"

#include "Hit.h"
#include "Pyramid.h"
#include "Ray.h"
#include "Triangle.h"
#include "Utility.h"
#include "Photon.h"

#include <algorithm>
#include <iostream>
#include <string>

template<typename T>
size_t Tree<T>::Node::size() const
{
    size_t count = 0;

    if (page)
    {
        count += page->contents.size();
    }

    if (left)
    {
        count += left->size();
    }

    if (right)
    {
        count += right->size();
    }

    return count;
}

template<typename T>
size_t Tree<T>::Node::nodeCount() const
{
    size_t count = 1;

    if (left)
    {
        count += left->nodeCount();
    }

    if (right)
    {
        count += right->nodeCount();
    }

    return count;
}

template<typename T>
size_t Tree<T>::Node::nodeDepth() const
{
    size_t depth = 1;

    if (left)
    {
        depth = std::max(depth, left->nodeDepth() + 1);
    }

    if (right)
    {
        depth = std::max(depth, right->nodeDepth() + 1);
    }

    return depth;
}

template<typename T>
Tree<T>::Tree(const std::vector<T>& objects, size_t pageSize)
    : m_pageSize(pageSize)
{
    m_root = generateTree(objects, Axis::X);
}

template<typename T>
std::shared_ptr<typename Tree<T>::Node> Tree<T>::root()
{
    return m_root;
}

template<typename T>
size_t Tree<T>::size() const
{
    return m_root->size();
}

template<typename T>
size_t Tree<T>::nodeCount() const
{
    return m_root->nodeCount();
}

template<typename T>
size_t Tree<T>::nodeDepth() const
{
    return m_root->nodeDepth();
}

template<typename T>
std::optional<Hit> Tree<T>::castRay(const Ray& ray) const
{
    std::vector<Hit> hits;

    Tree<T>::castRayIntoNode(ray, m_root, hits);

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

template<typename T>
std::vector<T> Tree<T>::fetchWithinPyramid(const Pyramid& pyramid) const
{
    // std::cout << "--- fetchWithinPyramid ---" << std::endl;
    std::vector<T> objects;

    Tree<T>::fetchWithinPyramidFromNode(pyramid, m_root, objects);

    return objects;
}

template<typename T>
float Tree<T>::axisMedian(const std::vector<T>& objects, Axis axis)
{
    size_t size = objects.size();

    if (size == 0)
    {
        return 0.0f;
    }
    else if (size == 1)
    {
        return Tree<T>::getPivot(objects[0], axis);
    }

    std::vector<float> pivots;

    for (const auto& object : objects)
    {
        pivots.push_back(Tree<T>::getPivot(object, axis));
    }

    std::sort(pivots.begin(), pivots.end());

    size_t middle = size / 2;
    float median = pivots[middle];

    if (size % 2 == 0)
    {
        median += pivots[middle - 1];
        median /= 2.0f;
    }

    return median;
}

template<typename T>
void Tree<T>::castRayIntoNode(const Ray& ray, std::shared_ptr<Tree<T>::Node> node, std::vector<Hit>& hits)
{
    std::optional<Hit> hit;

    if (rayIntersectsBounds(ray, node->bounds))
    {
        if (node->page)
        {
            for (const auto& object : node->page->contents)
            {
                hit = Tree<T>::rayIntersectsObject(ray, object);

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

template<typename T>
void Tree<T>::fetchWithinPyramidFromNode(const Pyramid& pyramid, std::shared_ptr<Node> node, std::vector<T>& objects)
{
    if (pyramid.intersectsBounds(node->bounds))
    {
        if (node->page)
        {
            for (const auto& object : node->page->contents)
            {
                if (pyramid.containsPoint(Tree<T>::getPivot(object)))
                {
                    objects.push_back(object);
                }
            }
        }

        if (node->left)
        {
            fetchWithinPyramidFromNode(pyramid, node->left, objects);
        }

        if (node->right)
        {
            fetchWithinPyramidFromNode(pyramid, node->right, objects);
        }
    }

    // if (node->page)
    // {
    //     for (const auto& object : node->page->contents)
    //     {
    //         if (pyramid.containsPoint(Tree<T>::getPivot(object)))
    //         {
    //             objects.push_back(object);
    //         }
    //     }
    // }

    // if (node->left)
    // {
    //     fetchWithinPyramidFromNode(pyramid, node->left, objects);
    // }

    // if (node->right)
    // {
    //     fetchWithinPyramidFromNode(pyramid, node->right, objects);
    // }
}

template<typename T>
std::shared_ptr<typename Tree<T>::Node> Tree<T>::generateTree(const std::vector<T>& objects, Axis axis)
{
    // if number of objects < max amount, create node with page and return
    // else, find median center point
    // everything < median goes in left, > median goes right, if equal
    // create Node for left and right

    // std::cout << "generateTree : " << objects.size() << " tris, axis: " << static_cast<int>(axis) << std::endl;

    std::shared_ptr<Tree<T>::Node> node = std::make_shared<Tree<T>::Node>();
    node->axis = axis;
    node->bounds = Tree<T>::getBounds(objects[0]);

    for (const auto& object : objects)
    {
        node->bounds += Tree<T>::getBounds(object);
    }

    if (objects.size() <= m_pageSize)
    {
        node->page = std::make_unique<Tree<T>::Node::Page>();
        node->page->contents = objects;
    }
    else
    {
        node->pivot = Tree<T>::axisMedian(objects, axis);

        std::vector<T> leftObjects, middleObjects, rightObjects;

        for (const auto& object : objects)
        {
            float pivot = Tree<T>::getPivot(object, axis);
            if (pivot < node->pivot)
            {
                leftObjects.push_back(object);
            }
            else if (pivot > node-> pivot)
            {
                rightObjects.push_back(object);
            }
            else
            {
                middleObjects.push_back(object);
            }
        }

        if (!leftObjects.empty())
        {
            // std::cout << "generateTree : left" << std::endl;
            node->left = generateTree(leftObjects, nextAxis(axis));
        }

        if (!rightObjects.empty())
        {
            // std::cout << "generateTree : right" << std::endl;
            node->right = generateTree(rightObjects, nextAxis(axis));
        }

        if (!middleObjects.empty())
        {
            node->page = std::make_unique<Tree<T>::Node::Page>();
            node->page->contents = middleObjects;
        }
    }

    return node;
}

template<>
const Vector& Tree<Triangle>::getPivot(const Triangle& object)
{
    return object.center;
}

template<>
float Tree<Triangle>::getPivot(const Triangle& object, Axis axis)
{
    return object.center.getAxis(axis);
}

template<>
Bounds Tree<Triangle>::getBounds(const Triangle& object)
{
    return object.getBounds();
}

template<>
std::optional<Hit> Tree<Triangle>::rayIntersectsObject(const Ray& ray, const Triangle& object)
{
    return rayIntersectsTriangle(ray, object);
}

template size_t Tree<Triangle>::Node::size() const;
template size_t Tree<Triangle>::Node::nodeCount() const;
template size_t Tree<Triangle>::Node::nodeDepth() const;
template Tree<Triangle>::Tree(const std::vector<Triangle>& objects, size_t pageSize);
template std::shared_ptr<typename Tree<Triangle>::Node> Tree<Triangle>::root();
template size_t Tree<Triangle>::size() const;
template size_t Tree<Triangle>::nodeCount() const;
template size_t Tree<Triangle>::nodeDepth() const;
template std::optional<Hit> Tree<Triangle>::castRay(const Ray& ray) const;
template std::vector<Triangle> Tree<Triangle>::fetchWithinPyramid(const Pyramid& pyramid) const;
template float Tree<Triangle>::axisMedian(const std::vector<Triangle>& objects, Axis axis);
template void Tree<Triangle>::castRayIntoNode(const Ray& ray, std::shared_ptr<Tree<Triangle>::Node> node, std::vector<Hit>& hits);
template std::shared_ptr<typename Tree<Triangle>::Node> Tree<Triangle>::generateTree(const std::vector<Triangle>& objects, Axis axis);

template<>
const Vector& Tree<PhotonHit>::getPivot(const PhotonHit& object)
{
    return object.hit.position;
}

template<>
float Tree<PhotonHit>::getPivot(const PhotonHit& object, Axis axis)
{
    return object.hit.position.getAxis(axis);
}

template<>
Bounds Tree<PhotonHit>::getBounds(const PhotonHit& object)
{
    return {object.hit.position};
}

template<>
std::optional<Hit> Tree<PhotonHit>::rayIntersectsObject(const Ray& ray, const PhotonHit& object)
{
    return std::nullopt;
}

template size_t Tree<PhotonHit>::Node::size() const;
template size_t Tree<PhotonHit>::Node::nodeCount() const;
template size_t Tree<PhotonHit>::Node::nodeDepth() const;
template Tree<PhotonHit>::Tree(const std::vector<PhotonHit>& objects, size_t pageSize);
template std::shared_ptr<typename Tree<PhotonHit>::Node> Tree<PhotonHit>::root();
template size_t Tree<PhotonHit>::size() const;
template size_t Tree<PhotonHit>::nodeCount() const;
template size_t Tree<PhotonHit>::nodeDepth() const;
template std::optional<Hit> Tree<PhotonHit>::castRay(const Ray& ray) const;
template std::vector<PhotonHit> Tree<PhotonHit>::fetchWithinPyramid(const Pyramid& pyramid) const;
template float Tree<PhotonHit>::axisMedian(const std::vector<PhotonHit>& objects, Axis axis);
template void Tree<PhotonHit>::castRayIntoNode(const Ray& ray, std::shared_ptr<Tree<PhotonHit>::Node> node, std::vector<Hit>& hits);
template std::shared_ptr<typename Tree<PhotonHit>::Node> Tree<PhotonHit>::generateTree(const std::vector<PhotonHit>& objects, Axis axis);
