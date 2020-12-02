#include "MeshVolume.h"

MeshVolume::MeshVolume(const std::vector<Triangle>& triangles)
    : m_tree(triangles)
{
}

std::optional<Hit> MeshVolume::castRay(const Ray& ray) const
{
    return m_tree.castRay(ray);
}
