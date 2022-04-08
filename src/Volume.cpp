#include "Volume.h"

Volume::Volume()
    : Object()
    , m_materialIndex(-1)
{
    registerType<Volume>();
}

Volume::Volume(size_t materialIndex)
    : Object()
    , m_materialIndex(materialIndex)
{
    registerType<Volume>();
}

void Volume::materialIndex(size_t materialIndex)
{
    m_materialIndex = materialIndex;
}

size_t Volume::materialIndex() const
{
    return m_materialIndex;
}

std::optional<Hit> Volume::castRay(const Ray& ray, std::vector<Hit>& castBuffer) const
{
    Ray transformedRay = transformRay(ray);
    std::optional<Hit> hit = castTransformedRay(transformedRay, castBuffer);

    if (hit)
    {
        hit->position = position() + (rotation() * hit->position);
        hit->normal = rotation() * hit->normal,
        hit->material = m_materialIndex;
    }

    return hit;
}

std::optional<Hit> Volume::castTransformedRay(const Ray& ray, std::vector<Hit>& castBuffer) const
{
    return std::nullopt;
}

Ray Volume::transformRay(const Ray& ray) const
{
    return {
        rotation().inverse() * (ray.origin - position()),
        rotation().inverse() * ray.direction
    };
}
