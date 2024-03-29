#include "DiffuseMaterial.h"

#include "Utility.h"

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
    double reflectionDot = Vector::dot(-pixelDirection, reflection);
    double brightess = std::max(0.0, ((reflectionDot + 1.0) / 2.0));
    return m_color * photonHit.photon.color * brightess;
}

void DiffuseMaterial::bounce(WorkQueue<Photon>::Block photonBlock, size_t startIndex, size_t endIndex, const PhotonHit& photonHit, RandomGenerator& generator) const
{
    size_t count = endIndex - startIndex;
    if (count == 0)
    {
        return;
    }

    Vector reflection = Vector::reflected(photonHit.photon.ray.direction, photonHit.hit.normal);

    double brightness = (1.0 / static_cast<double>(count)) / (Utility::pi * 2.0);

    for (size_t i = startIndex; i < endIndex; ++i)
    {
        Vector offsetReflection = m_angleGenerator.generateOffsetVector(reflection, generator);

        double normalDot = Vector::dot(offsetReflection, photonHit.hit.normal);
        if (normalDot <= 0.0)
        {
            brightness = 0.0;
        }

        photonBlock[i].ray = {photonHit.hit.position, offsetReflection};
        photonBlock[i].color = m_color * photonHit.photon.color * brightness;
        photonBlock[i].bounces = photonHit.photon.bounces + 1;
    }
}
