#pragma once

#include "Hit.h"
#include "Plane.h"
#include "Ray.h"
#include "Volume.h"

#include <optional>

class PlaneVolume : public Volume
{
public:
    PlaneVolume();

    std::optional<Hit> castRay(const Ray& ray) const override;

private:
    Plane m_plane;
};
