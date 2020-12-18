#pragma once

#include "Light.h"
#include "RandomGenerator.h"

class OmniLight : public Light
{
public:
    OmniLight();

    void innerRadius(float innerRadius);
    float innerRadius() const;

    void emit(WorkQueue<Photon>::Block photonBlock, float photonBrightness, RandomGenerator& generator) const override;

private:
    float m_innerRadius = 0;
};
