#include <catch2/catch_all.hpp>

#include "Bounds.h"

using Catch::Matchers::WithinULP;

// zero Limits = {0, 0}
// single value Limits = {x, x}
// non-zero Limtis = {x, y}

TEST_CASE("Limits constructors produce expected initial state", "[Limits]")
{
    SECTION("default constructor sets min and max to zero")
    {
        Limits limits;

        REQUIRE_THAT(limits.min,  WithinULP(0.0, 1));
        REQUIRE_THAT(limits.max,  WithinULP(0.0, 1));
    }

    SECTION("min, max constructor sets min and max accordingly")
    {
        const double expectedMin = -1.0;
        const double expectedMax = 1.0;

        Limits limits{expectedMin, expectedMax};

        REQUIRE_THAT(limits.min,  WithinULP(expectedMin, 1));
        REQUIRE_THAT(limits.max,  WithinULP(expectedMax, 1));
    }

    SECTION("min, max constructor sets uses minimum value for min and maximum value for max")
    {
        const double expectedMin = -1.0;
        const double expectedMax = 1.0;

        Limits limits{expectedMax, expectedMin};

        REQUIRE_THAT(limits.min,  WithinULP(expectedMin, 1));
        REQUIRE_THAT(limits.max,  WithinULP(expectedMax, 1));
    }

    SECTION("value constructor sets min and max to value")
    {
        const double expectedValue = 1.0;

        Limits limits{expectedValue};

        REQUIRE_THAT(limits.min,  WithinULP(expectedValue, 1));
        REQUIRE_THAT(limits.max,  WithinULP(expectedValue, 1));
    }

    SECTION("0 value constructor equivalent to default constructor")
    {
        Limits limitsA{0.0};
        Limits limitsB;

        REQUIRE_THAT(limitsA.min,  WithinULP(limitsB.min, 1));
        REQUIRE_THAT(limitsA.max,  WithinULP(limitsB.max, 1));
    }
}

TEST_CASE("Limits::contains method produces expected values with non-zero range", "[Limits]")
{
    const double belowMin = -2.0;
    const double limitsMin = -1.0;
    const double limitsMax = 1.0;
    const double aboveMax = 2.0;

    Limits limits{limitsMin, limitsMax};

    SECTION("value equal to min is contained")
    {
        REQUIRE(limits.contains(limitsMin));
    }

    SECTION("value equal to max is contained")
    {
        REQUIRE(limits.contains(limitsMax));
    }

    SECTION("value within min and max is contained")
    {
        REQUIRE(limits.contains(0.0));
    }

    SECTION("value below min is not contained")
    {
        REQUIRE(!limits.contains(belowMin));
    }

    SECTION("value above max is not contained")
    {
        REQUIRE(!limits.contains(aboveMax));
    }
}

TEST_CASE("Limits::contains method produces expected values with zero range", "[Limits]")
{
    const double belowMin = -1.0;
    const double aboveMax = 1.0;

    Limits limits;

    SECTION("value equal to min is contained")
    {
        REQUIRE(limits.contains(0.0));
    }

    SECTION("value equal to max is contained")
    {
        REQUIRE(limits.contains(0.0));
    }

    SECTION("value within min and max is contained")
    {
        REQUIRE(limits.contains(0.0));
    }

    SECTION("value below min is not contained")
    {
        REQUIRE(!limits.contains(belowMin));
    }

    SECTION("value above max is not contained")
    {
        REQUIRE(!limits.contains(aboveMax));
    }
}

TEST_CASE("Limits::intersects method produces expected values", "[Limits]")
{
    SECTION("equivalent Limits intersect")
    {
        SECTION("equivalent zero Limits intersect")
        {
            Limits limitsA;
            Limits limitsB;

            REQUIRE(limitsA.intersects(limitsB));
            REQUIRE(limitsB.intersects(limitsA));
        }

        SECTION("equivalent single value Limits intersect")
        {
            Limits limitsA{1.0};
            Limits limitsB{1.0};

            REQUIRE(limitsA.intersects(limitsB));
            REQUIRE(limitsB.intersects(limitsA));
        }

        SECTION("equivalent non-zero Limits intersect")
        {
            Limits limitsA{-1.0, 1.0};
            Limits limitsB{-1.0, 1.0};

            REQUIRE(limitsA.intersects(limitsB));
            REQUIRE(limitsB.intersects(limitsA));
        }
    }

    SECTION("overlapping Limits intersect")
    {
        SECTION("zero overlapping single value Limits intersect")
        {
            Limits limitsA;
            Limits limitsB{0.0};

            REQUIRE(limitsA.intersects(limitsB));
            REQUIRE(limitsB.intersects(limitsA));
        }

        SECTION("zero overlapping non-zero Limits intersect")
        {
            Limits limitsA;
            Limits limitsB{-1.0, 1.0};

            REQUIRE(limitsA.intersects(limitsB));
            REQUIRE(limitsB.intersects(limitsA));
        }

        SECTION("single value overlapping non-zero Limits intersect")
        {
            Limits limitsA{0.5};
            Limits limitsB{-1.0, 1.0};

            REQUIRE(limitsA.intersects(limitsB));
            REQUIRE(limitsB.intersects(limitsA));
        }

        SECTION("overlapping non-zero Limits intersect")
        {
            Limits limitsA{-1.0, 1.0};
            Limits limitsB{-2.0, 0.0};

            REQUIRE(limitsA.intersects(limitsB));
            REQUIRE(limitsB.intersects(limitsA));
        }
    }

    SECTION("touching Limits intersect")
    {
        SECTION("zero touching single value Limits intersect")
        {
            Limits limitsA;
            Limits limitsB{0.0};

            REQUIRE(limitsA.intersects(limitsB));
            REQUIRE(limitsB.intersects(limitsA));
        }

        SECTION("zero touching non-zero Limits intersect")
        {
            Limits limitsA;
            Limits limitsB{0.0, 1.0};

            REQUIRE(limitsA.intersects(limitsB));
            REQUIRE(limitsB.intersects(limitsA));
        }

        SECTION("single value touching non-zero Limits intersect")
        {
            Limits limitsA{1.0};
            Limits limitsB{-1.0, 1.0};

            REQUIRE(limitsA.intersects(limitsB));
            REQUIRE(limitsB.intersects(limitsA));
        }

        SECTION("touching non-zero Limits intersect")
        {
            Limits limitsA{-1.0, 0.0};
            Limits limitsB{0.0, 1.0};

            REQUIRE(limitsA.intersects(limitsB));
            REQUIRE(limitsB.intersects(limitsA));
        }
    }

    SECTION("separated Limits do not intersect")
    {
        SECTION("zero separated from single value Limits do not intersect")
        {
            Limits limitsA;
            Limits limitsB{1.0};

            REQUIRE(!limitsA.intersects(limitsB));
            REQUIRE(!limitsB.intersects(limitsA));
        }

        SECTION("zero separated from non-zero Limits do not intersect")
        {
            Limits limitsA;
            Limits limitsB{1.0, 2.0};

            REQUIRE(!limitsA.intersects(limitsB));
            REQUIRE(!limitsB.intersects(limitsA));
        }

        SECTION("single value separated from non-zero Limits do not intersect")
        {
            Limits limitsA{2.0};
            Limits limitsB{-1.0, 1.0};

            REQUIRE(!limitsA.intersects(limitsB));
            REQUIRE(!limitsB.intersects(limitsA));
        }

        SECTION("separated from non-zero Limits do not intersect")
        {
            Limits limitsA{-2.0, -1.0};
            Limits limitsB{0.0, 1.0};

            REQUIRE(!limitsA.intersects(limitsB));
            REQUIRE(!limitsB.intersects(limitsA));
        }
    }
}

TEST_CASE("Limits assignment operator produces expected values", "[Limits]")
{
    Limits limitsA;
    Limits limitsB{-1.0, 1.0};

    limitsA = limitsB;

    SECTION("assignment sets min and max equal to the right hand side Limits")
    {
        REQUIRE_THAT(limitsA.min,  WithinULP(limitsB.min, 1));
        REQUIRE_THAT(limitsA.max,  WithinULP(limitsB.max, 1));
    }

    SECTION("assignment copies value from right hand side Limits")
    {
        limitsA.min = -2.0;
        limitsA.max = -1.0;

        REQUIRE_THAT(limitsA.min,  WithinULP(-2.0, 1));
        REQUIRE_THAT(limitsA.max,  WithinULP(-1.0, 1));

        REQUIRE_THAT(limitsB.min,  WithinULP(-1.0, 1));
        REQUIRE_THAT(limitsB.max,  WithinULP(1.0, 1));
    }
}

TEST_CASE("Limits addition assignment operator extends range", "[Limits]")
{
    SECTION("equivalent addition assignment produces no change")
    {
        Limits limitsA{-1.0, 1.0};
        Limits limitsB{-1.0, 1.0};

        limitsA += limitsB;

        REQUIRE_THAT(limitsA.min,  WithinULP(-1.0, 1));
        REQUIRE_THAT(limitsA.max,  WithinULP(1.0, 1));
    }

    SECTION("addition assignment with lower min extends min")
    {
        Limits limitsA{-1.0, 1.0};
        Limits limitsB{-2.0, 1.0};

        limitsA += limitsB;

        REQUIRE_THAT(limitsA.min,  WithinULP(-2.0, 1));
        REQUIRE_THAT(limitsA.max,  WithinULP(1.0, 1));
    }

    SECTION("addition assignment with higher min does nothing")
    {
        Limits limitsA{-1.0, 1.0};
        Limits limitsB{0.0, 1.0};

        limitsA += limitsB;

        REQUIRE_THAT(limitsA.min,  WithinULP(-1.0, 1));
        REQUIRE_THAT(limitsA.max,  WithinULP(1.0, 1));
    }

    SECTION("addition assignment with higher max extends max")
    {
        Limits limitsA{-1.0, 1.0};
        Limits limitsB{-1.0, 2.0};

        limitsA += limitsB;

        REQUIRE_THAT(limitsA.min,  WithinULP(-1.0, 1));
        REQUIRE_THAT(limitsA.max,  WithinULP(2.0, 1));
    }

    SECTION("addition assignment with lower max does nothing")
    {
        Limits limitsA{-1.0, 1.0};
        Limits limitsB{-1.0, 0.0};

        limitsA += limitsB;

        REQUIRE_THAT(limitsA.min,  WithinULP(-1.0, 1));
        REQUIRE_THAT(limitsA.max,  WithinULP(1.0, 1));
    }

    SECTION("addition assignment with wider range extends min and max")
    {
        Limits limitsA{-1.0, 1.0};
        Limits limitsB{-2.0, 2.0};

        limitsA += limitsB;

        REQUIRE_THAT(limitsA.min,  WithinULP(-2.0, 1));
        REQUIRE_THAT(limitsA.max,  WithinULP(2.0, 1));
    }

    SECTION("addition assignment with contained range does nothing")
    {
        Limits limitsA{-2.0, 2.0};
        Limits limitsB{-1.0, 1.0};

        limitsA += limitsB;

        REQUIRE_THAT(limitsA.min,  WithinULP(-2.0, 1));
        REQUIRE_THAT(limitsA.max,  WithinULP(2.0, 1));
    }
}
