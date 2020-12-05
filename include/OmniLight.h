#pragma once

#include "Light.h"

class OmniLight : public Light
{
public:
    OmniLight();

    void emit(WorkQueue<Photon>::Block photonBlock) const override;
};
