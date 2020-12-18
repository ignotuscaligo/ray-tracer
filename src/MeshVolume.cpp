#include "MeshVolume.h"

MeshVolume::MeshVolume(size_t materialIndex, const std::vector<Triangle>& triangles)
    : Volume(materialIndex)
    , m_tree(triangles)
{
    registerType<MeshVolume>();
}

std::optional<Hit> MeshVolume::castTransformedRay(const Ray& ray) const
{
    return m_tree.castRay(ray);
}
