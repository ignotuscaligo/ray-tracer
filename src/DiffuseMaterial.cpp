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
    float reflectionDot = std::max(0.0f, Vector::dot(-pixelDirection, reflection));
    return m_color * photonHit.photon.color * reflectionDot;
}
