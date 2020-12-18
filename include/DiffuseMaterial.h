#pragma once

#include "Color.h"
#include "Material.h"
#include "Photon.h"
#include "Vector.h"

class DiffuseMaterial : public Material
{
public:
    DiffuseMaterial();
    DiffuseMaterial(const std::string& name, const Color& color = {1.0f, 1.0f, 1.0f});

    Color colorForHit(const Vector& pixelDirection, const PhotonHit& photonHit) const override;

private:
    Color m_color;
};
