#include "PlaneVolume.h"

PlaneVolume::PlaneVolume()
    : Volume()
    , m_plane({0, 0, 1}, {1, 0, 0})
{
    registerType<PlaneVolume>();
}

std::optional<Hit> PlaneVolume::castRay(const Ray& ray) const
{
    return transformHit(rayIntersectsPlane(transformRay(ray), m_plane));
}
