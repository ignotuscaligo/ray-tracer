#include "Volume.h"

Volume::Volume()
    : Object()
{
    registerType<Volume>();
}

std::optional<Hit> Volume::castRay(const Ray& ray) const
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

std::optional<Hit> Volume::transformHit(const std::optional<Hit>& hit) const
{
    if (!hit)
    {
        return std::nullopt;
    }

    return Hit{
        position() + (rotation() * hit->position),
        rotation() * hit->normal,
        hit->distance
    };
}
