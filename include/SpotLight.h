#pragma once

#include "AngleGenerator.h"
#include "Light.h"
#include "RandomGenerator.h"

class SpotLight : public Light
{
public:
    SpotLight();

    void innerRadius(double innerRadius);
    double innerRadius() const;

    void angle(double angle);
    double angle() const;

    void emit(WorkQueue<Photon>::Block photonBlock, double photonBrightness, RandomGenerator& generator) const override;

private:
    double m_innerRadius = 0;
    double m_angle = 0;
    double m_area = 0;
    AngleGenerator m_angleGenerator;
};
