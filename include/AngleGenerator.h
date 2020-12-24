#pragma once

#include "RandomGenerator.h"
#include "Vector.h"
#include "Utility.h"

struct AngleGenerator
{
    double map(double value) const;
    double generate(RandomGenerator& randomGenerator) const;
    Vector generateOffsetVector(const Vector& center, RandomGenerator& randomGenerator) const;

    double maxAngle = Utility::pi;
    double linearity = 1.0;
};
