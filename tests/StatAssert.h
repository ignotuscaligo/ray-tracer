#pragma once

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <sstream>
#include <vector>

// ============================================================================
// StatAssert.h — statistical assert helpers
// ============================================================================
//
// Per docs/test-plan-fable-2026-06.md ("Statistical assert helpers"): replace
// magic tolerances (0.02, 0.04, 0.90) with EXPLICIT confidence intervals so a
// test states its false-positive rate instead of an unexplained margin.
//
//   REQUIRE_MEAN_CI     — a measured mean lies within a normal-approximation CI
//                         of the expected value, given an estimated std-error.
//   REQUIRE_PROPORTION_CI — a measured proportion (k successes of n) is within a
//                         binomial (normal-approx) CI of the expected p.
//   chiSquareStatistic  — histogram-vs-pdf goodness-of-fit statistic, compared
//                         against a critical value for the chosen significance.
//
// The CI half-width uses a z-multiplier; the defaults below correspond to common
// two-sided significance levels:
//   z = 1.96  -> 95%      z = 2.576 -> 99%      z = 3.291 -> 99.9%
// Tests use a WIDE interval (z >= 3) so a correct renderer essentially never
// trips the assert by chance, while a real regression (a bias, a missing factor)
// moves the statistic far outside even the wide band.

namespace rt_test
{

inline constexpr double kZ95 = 1.96;
inline constexpr double kZ99 = 2.576;
inline constexpr double kZ999 = 3.291;

struct CIResult
{
    bool pass = false;
    double measured = 0.0;
    double expected = 0.0;
    double halfWidth = 0.0;  // z * standardError
    double z = 0.0;
};

// Normal-approximation CI for a MEAN: pass iff |measured - expected| <= z * SE.
inline CIResult meanWithinCI(double measured, double expected, double standardError,
                             double z = kZ999)
{
    CIResult r;
    r.measured = measured;
    r.expected = expected;
    r.z = z;
    r.halfWidth = z * standardError;
    r.pass = std::abs(measured - expected) <= r.halfWidth;
    return r;
}

// Normal-approximation CI for a PROPORTION p = k/n (binomial). SE = sqrt(p(1-p)/n),
// evaluated at the EXPECTED p (the null) so the interval doesn't collapse when the
// sample happens to be 0 or n.
inline CIResult proportionWithinCI(size_t successes, size_t trials,
                                   double expectedP, double z = kZ999)
{
    CIResult r;
    r.expected = expectedP;
    r.z = z;
    if (trials == 0)
    {
        r.measured = 0.0;
        r.halfWidth = 0.0;
        r.pass = false;
        return r;
    }
    r.measured = static_cast<double>(successes) / static_cast<double>(trials);
    const double se =
        std::sqrt(std::max(0.0, expectedP * (1.0 - expectedP)) /
                  static_cast<double>(trials));
    r.halfWidth = z * se;
    r.pass = std::abs(r.measured - expectedP) <= r.halfWidth;
    return r;
}

// Pearson chi-square statistic for a binned sample vs an expected-count model:
//   X^2 = sum_i (observed_i - expected_i)^2 / expected_i
// Bins with expected_i <= 0 are skipped (they contribute no constraint). The
// caller compares the returned statistic against the chi-square critical value
// for (bins - 1) degrees of freedom at the chosen significance (see kChiSqCrit*).
inline double chiSquareStatistic(const std::vector<double>& observed,
                                 const std::vector<double>& expected)
{
    double x2 = 0.0;
    const size_t n = std::min(observed.size(), expected.size());
    for (size_t i = 0; i < n; ++i)
    {
        if (expected[i] <= 0.0)
        {
            continue;
        }
        const double d = observed[i] - expected[i];
        x2 += d * d / expected[i];
    }
    return x2;
}

}  // namespace rt_test

// Catch2 assert macros wrapping the CI helpers, with an INFO trail that prints the
// measured value, the expected value, and the interval half-width on failure.

#define REQUIRE_MEAN_CI(measured, expected, standardError, z)                      \
    do                                                                             \
    {                                                                              \
        const ::rt_test::CIResult _ci =                                            \
            ::rt_test::meanWithinCI((measured), (expected), (standardError), (z)); \
        INFO("REQUIRE_MEAN_CI: measured=" << _ci.measured                          \
             << " expected=" << _ci.expected << " +/- " << _ci.halfWidth           \
             << " (z=" << _ci.z << ", SE=" << (standardError) << ")");             \
        REQUIRE(_ci.pass);                                                         \
    } while (false)

#define REQUIRE_PROPORTION_CI(successes, trials, expectedP, z)                      \
    do                                                                             \
    {                                                                              \
        const ::rt_test::CIResult _ci =                                            \
            ::rt_test::proportionWithinCI((successes), (trials), (expectedP), (z));\
        INFO("REQUIRE_PROPORTION_CI: measured=" << _ci.measured                    \
             << " expected=" << _ci.expected << " +/- " << _ci.halfWidth           \
             << " (z=" << _ci.z << ", n=" << (trials) << ")");                     \
        REQUIRE(_ci.pass);                                                         \
    } while (false)
