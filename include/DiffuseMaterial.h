#pragma once

#include "AngleGenerator.h"
#include "Color.h"
#include "Material.h"
#include "Photon.h"
#include "RandomGenerator.h"
#include "Vector.h"
#include "WorkQueue.h"

class DiffuseMaterial : public Material
{
public:
    DiffuseMaterial();
    DiffuseMaterial(const std::string& name, const Color& color = {1.0f, 1.0f, 1.0f});

    Color colorForHit(const Vector& pixelDirection, const PhotonHit& photonHit) const override;
    void bounce(WorkQueue<Photon>::Block photonBlock, const PhotonHit& photonHit, RandomGenerator& generator) const override;

private:
    Color m_color;
    AngleGenerator m_angleGenerator;
};
