#include "AngleGenerator.h"

#include "Quaternion.h"

#include <algorithm>
#include <cmath>

double AngleGenerator::map(double value) const
{
    value = std::min(std::max(-1.0, value), 1.0);
    value = std::pow(value, linearity);
    value *= maxAngle;

    return value;
}

double AngleGenerator::generate(RandomGenerator& randomGenerator) const
{
    double value = -1.0 + (randomGenerator.value() * 2.0);

    return map(value);
}

Vector AngleGenerator::generateOffsetVector(const Vector& center, RandomGenerator& randomGenerator) const
{
    double theta = randomGenerator.value(Utility::pi2);
    double angle = map(randomGenerator.value(1.0));
    bool useZ = std::abs(Vector::dot(Vector::unitY, center)) > 0.9;
    const Vector& crossAxis = useZ ? Vector::unitZ : Vector::unitY;

    Vector offset = Vector::cross(center, crossAxis) * std::sin(angle);
    Quaternion rotation = Quaternion::fromAxisAngle(center, theta);
    offset = rotation * offset;

    return (center * std::cos(angle)) + offset;
}
