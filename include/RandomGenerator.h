#pragma once

#include <cstdint>
#include <random>

class RandomGenerator
{
public:
    RandomGenerator();

    // Deterministic-seed constructor. Used by tests that need a reproducible draw
    // sequence (e.g. asserting lazy chunked daughter generation reproduces the
    // eager single-shot fan-out). Production code uses the default constructor,
    // which seeds from std::random_device.
    explicit RandomGenerator(std::uint32_t seed);

    double value(double scale = 1.0f);

private:
    std::mt19937 m_generator;
    std::uniform_real_distribution<double> m_distribution{0.0, 1.0};
};
