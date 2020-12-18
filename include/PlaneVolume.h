#pragma once

#include "Hit.h"
#include "Plane.h"
#include "Ray.h"
#include "Volume.h"

#include <optional>

class PlaneVolume : public Volume
{
public:
    PlaneVolume(size_t materialIndex);

protected:
    std::optional<Hit> castTransformedRay(const Ray& ray) const override;

private:
    Plane m_plane;
};
