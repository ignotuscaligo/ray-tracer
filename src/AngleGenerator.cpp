#include "AngleGenerator.h"

#include "Quaternion.h"
#include "Utility.h"

#include <algorithm>
#include <cmath>

float AngleGenerator::map(float value) const
{
    value = std::min(std::max(-1.0f, value), 1.0f);
    value = std::pow(value, linearity);
    value *= maxAngle;

    return value;
}

float AngleGenerator::generate(RandomGenerator& randomGenerator) const
{
    float value = -1.0f + (randomGenerator.value() * 2.0f);

    return map(value);
}

Vector AngleGenerator::generateOffsetVector(const Vector& center, RandomGenerator& randomGenerator) const
{
    float theta = randomGenerator.value(2.0f * Utility::pi);
    float angle = Utility::radians(map(randomGenerator.value(1.0f)));
    bool useZ = (center.y > center.x && center.y > center.z);
    const Vector& crossAxis = useZ ? Vector::unitZ : Vector::unitY;

    Vector offset = Vector::cross(center, crossAxis) * std::sin(angle);
    Quaternion rotation = Quaternion::fromAxisAngle(center, theta);
    offset = rotation * offset;

    return (center * std::cos(angle)) + offset;
}
