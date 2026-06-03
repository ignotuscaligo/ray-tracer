#pragma once

#include "Light.h"
#include "RandomGenerator.h"

class ParallelLight : public Light
{
public:
    ParallelLight();

    void radius(double radius);
    double radius() const;

    void emit(WorkQueue<Photon>::Block photonBlock, double photonFlux, RandomGenerator& generator) const override;

private:
    double m_radius = 0;
};
