#pragma once

#include "RandomGenerator.h"
#include "Vector.h"

struct AngleGenerator
{
    double map(double value) const;
    double generate(RandomGenerator& randomGenerator) const;
    Vector generateOffsetVector(const Vector& center, RandomGenerator& randomGenerator) const;

    double maxAngle = 90.0;
    double linearity = 1.0;
};
