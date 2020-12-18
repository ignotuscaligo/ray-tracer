#include "RandomGenerator.h"

RandomGenerator::RandomGenerator()
{
    std::random_device device;
    m_generator = std::mt19937(device());
}

float RandomGenerator::value(float scale)
{
    return m_distribution(m_generator) * scale;
}
