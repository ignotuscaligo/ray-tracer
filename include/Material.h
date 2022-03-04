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

    // Get the color / intensity of the photon hit given a viewing direction
    // Used for colelcting photon hits into the view buffer
    virtual Color colorForHit(const Vector& pixelDirection, const PhotonHit& photonHit) const = 0;

    // Given a photon hit against this material, populate the photon block with a collection of bounced photons
    virtual void bounce(WorkQueue<Photon>::Block photonBlock, size_t startIndex, size_t endIndex, const PhotonHit& photonHit, RandomGenerator& generator) const = 0;

private:
    std::string m_name;
};
