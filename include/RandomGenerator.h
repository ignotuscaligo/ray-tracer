#pragma once

#include <random>

class RandomGenerator
{
public:
    RandomGenerator();

    double value(double scale = 1.0f);

private:
    std::mt19937 m_generator;
    std::uniform_real_distribution<double> m_distribution{0.0, 1.0};
};
