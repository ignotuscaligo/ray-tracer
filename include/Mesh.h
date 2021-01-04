#pragma once

#include "Tree.h"
#include "Triangle.h"

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

private:
    std::string m_name;
    Tree<Triangle> m_tree;
};
