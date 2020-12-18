#pragma once

#include <random>

class RandomGenerator
{
public:
    RandomGenerator();

    float value(float scale = 1.0f);

private:
    std::mt19937 m_generator;
    std::uniform_real_distribution<float> m_distribution{0.0f, 1.0f};
};
