#include "RandomGenerator.h"

RandomGenerator::RandomGenerator()
{
    std::random_device device;
    m_generator = std::mt19937(device());
}

double RandomGenerator::value(double scale)
{
    return m_distribution(m_generator) * scale;
}
