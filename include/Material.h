#pragma once

#include "Color.h"
#include "Photon.h"
#include "RandomGenerator.h"
#include "Vector.h"
#include "WorkQueue.h"

#include <string>

class Material
{
public:
    Material() = default;
    Material(const std::string& name);

    std::string name() const;

    virtual Color colorForHit(const Vector& pixelDirection, const PhotonHit& photonHit) const = 0;
    virtual void bounce(WorkQueue<Photon>::Block photonBlock, size_t startIndex, size_t endIndex, const PhotonHit& photonHit, RandomGenerator& generator) const = 0;

private:
    std::string m_name;
};
