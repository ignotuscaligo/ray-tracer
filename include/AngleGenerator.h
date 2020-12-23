#pragma once

#include "RandomGenerator.h"
#include "Vector.h"

struct AngleGenerator
{
    float map(float value) const;
    float generate(RandomGenerator& randomGenerator) const;
    Vector generateOffsetVector(const Vector& center, RandomGenerator& randomGenerator) const;

    float maxAngle = 90.0f;
    float linearity = 1.0f;
};
