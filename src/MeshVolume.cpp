#include "MeshVolume.h"

MeshVolume::MeshVolume(const std::vector<Triangle>& triangles)
    : m_tree(triangles)
{
}

std::optional<Hit> MeshVolume::castRay(const Ray& ray) const
{
    std::vector<Hit> hits = m_tree.castRay(ray);
    float minDistance;
    std::optional<Hit> result;

    for (const auto& hit : hits)
    {
        float distance = (hit.position - ray.origin).magnitude();

        if (!result || distance < minDistance)
        {
            result = hit;
            minDistance = distance;
        }
    }

    return result;
}
