#pragma once

#include "Hit.h"
#include "Ray.h"
#include "Sphere.h"
#include "Volume.h"

#include <optional>

class SphereVolume : public Volume
{
public:
    SphereVolume();
    SphereVolume(size_t materialIndex);
    SphereVolume(size_t materialIndex, const Vector& center, double radius);

    void center(const Vector& center);
    Vector center() const;

    void radius(double radius);
    double radius() const;

protected:
    std::optional<Hit> castTransformedRay(const Ray& ray, std::vector<Hit>& castBuffer) const override;

private:
    Sphere m_sphere;
};
