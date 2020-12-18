#include "PlaneVolume.h"

PlaneVolume::PlaneVolume(size_t materialIndex)
    : Volume(materialIndex)
    , m_plane({0, 0, 1}, {1, 0, 0})
{
    registerType<PlaneVolume>();
}

std::optional<Hit> PlaneVolume::castTransformedRay(const Ray& ray) const
{
    return rayIntersectsPlane(ray, m_plane);
}
