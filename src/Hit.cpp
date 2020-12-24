#include "Hit.h"

Hit::Hit(const Hit& other) noexcept
    : position(other.position)
    , normal(other.normal)
    , distance(other.distance)
    , material(other.material)
{
}

Hit Hit::operator=(const Hit& rhs) noexcept
{
    position = rhs.position;
    normal = rhs.normal;
    distance = rhs.distance;
    material = rhs.material;

    return *this;
}
