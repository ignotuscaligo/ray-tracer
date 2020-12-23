#include "DiffuseMaterial.h"

DiffuseMaterial::DiffuseMaterial()
    : Material()
    , m_color({1, 1, 1})
{
}

DiffuseMaterial::DiffuseMaterial(const std::string& name, const Color& color)
    : Material(name)
    , m_color(color)
{
}

Color DiffuseMaterial::colorForHit(const Vector& pixelDirection, const PhotonHit& photonHit) const
{
    Vector reflection = Vector::reflected(photonHit.photon.ray.direction, photonHit.hit.normal);
    float reflectionDot = Vector::dot(-pixelDirection, reflection);
    float brightess = std::max(0.0f, ((reflectionDot + 1.0f) / 2.0f));
    return m_color * photonHit.photon.color * brightess;
}
