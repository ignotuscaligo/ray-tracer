#pragma once

#include "Light.h"
#include "RandomGenerator.h"

class OmniLight : public Light
{
public:
    OmniLight();

    void innerRadius(double innerRadius);
    double innerRadius() const;

    void emit(WorkQueue<Photon>::Block photonBlock, double photonBrightness, RandomGenerator& generator) const override;

private:
    double m_innerRadius = 0;
};
