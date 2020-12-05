#include "MeshVolume.h"

MeshVolume::MeshVolume(const std::vector<Triangle>& triangles)
    : Volume()
    , m_tree(triangles)
{
    registerType<MeshVolume>();
}

std::optional<Hit> MeshVolume::castRay(const Ray& ray) const
{
    return m_tree.castRay(ray);
}
