#pragma once

#include "Light.h"
#include "RandomGenerator.h"

class ParallelLight : public Light
{
public:
    ParallelLight();

    void radius(double radius);
    double radius() const;

    void emit(WorkQueue<Photon>::Block photonBlock, double photonBrightness, RandomGenerator& generator) const override;

private:
    double m_radius = 0;
    double m_area = 0;
};
