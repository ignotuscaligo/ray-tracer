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
