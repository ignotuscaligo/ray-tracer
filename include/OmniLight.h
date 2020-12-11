#pragma once

#include "Light.h"

class OmniLight : public Light
{
public:
    OmniLight();

    void innerRadius(float innerRadius);
    float innerRadius() const;

    void emit(WorkQueue<Photon>::Block photonBlock) const override;

private:
    float m_innerRadius = 0;
};
