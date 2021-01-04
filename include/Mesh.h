#pragma once

#include "Hit.h"
#include "Ray.h"
#include "Tree.h"
#include "Triangle.h"

#include <optional>
#include <string>
#include <vector>

class Mesh
{
public:
    Mesh(const std::string& name, const std::vector<Triangle>& triangles)
        : m_name(name)
        , m_tree(triangles)
    {
    }

    void name(const std::string& name)
    {
        m_name = name;
    }

    std::string name() const
    {
        return m_name;
    }

    std::optional<Hit> castRay(const Ray& ray, std::vector<Hit>& castBuffer) const
    {
        return m_tree.castRay(ray, castBuffer);
    }

private:
    std::string m_name;
    Tree<Triangle> m_tree;
};
