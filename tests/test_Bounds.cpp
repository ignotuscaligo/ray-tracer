#include <catch2/catch_all.hpp>

#include "Bounds.h"
#include "Vector.h"

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

TEST_CASE("Bounds constructors produce expected initial state", "[Bounds]")
{
    SECTION("Default constructor initializes to zero")
    {
        Bounds bounds;

        REQUIRE_THAT(bounds.x.min,  WithinULP(0.0, 1));
        REQUIRE_THAT(bounds.x.max,  WithinULP(0.0, 1));

        REQUIRE_THAT(bounds.y.min,  WithinULP(0.0, 1));
        REQUIRE_THAT(bounds.y.max,  WithinULP(0.0, 1));

        REQUIRE_THAT(bounds.z.min,  WithinULP(0.0, 1));
        REQUIRE_THAT(bounds.z.max,  WithinULP(0.0, 1));
    }

    SECTION("Limits x, y, z constructor initializes to provided Limits")
    {
        Limits limitsX{-1.0, 1.0};
        Limits limitsY{2.0};
        Limits limitsZ{-3.0, 5.0};

        Bounds bounds{limitsX, limitsY, limitsZ};

        REQUIRE_THAT(bounds.x.min,  WithinULP(limitsX.min, 1));
        REQUIRE_THAT(bounds.x.max,  WithinULP(limitsX.max, 1));

        REQUIRE_THAT(bounds.y.min,  WithinULP(limitsY.min, 1));
        REQUIRE_THAT(bounds.y.max,  WithinULP(limitsY.max, 1));

        REQUIRE_THAT(bounds.z.min,  WithinULP(limitsZ.min, 1));
        REQUIRE_THAT(bounds.z.max,  WithinULP(limitsZ.max, 1));
    }

    SECTION("Vector constructor initializes to single value Limits")
    {
        Vector vector{-1.0, 2.0, -3.0};

        Bounds bounds{vector};

        REQUIRE_THAT(bounds.x.min,  WithinULP(vector.x, 1));
        REQUIRE_THAT(bounds.x.max,  WithinULP(vector.x, 1));

        REQUIRE_THAT(bounds.y.min,  WithinULP(vector.y, 1));
        REQUIRE_THAT(bounds.y.max,  WithinULP(vector.y, 1));

        REQUIRE_THAT(bounds.z.min,  WithinULP(vector.z, 1));
        REQUIRE_THAT(bounds.z.max,  WithinULP(vector.z, 1));
    }

    SECTION("Vector min, max constructor initializes Limits to match")
    {
        Vector vectorMin{-1.0, -2.0, -3.0};
        Vector vectorMax{4.0, 5.0, 6.0};

        Bounds bounds{vectorMin, vectorMax};

        REQUIRE_THAT(bounds.x.min,  WithinULP(vectorMin.x, 1));
        REQUIRE_THAT(bounds.x.max,  WithinULP(vectorMax.x, 1));

        REQUIRE_THAT(bounds.y.min,  WithinULP(vectorMin.y, 1));
        REQUIRE_THAT(bounds.y.max,  WithinULP(vectorMax.y, 1));

        REQUIRE_THAT(bounds.z.min,  WithinULP(vectorMin.z, 1));
        REQUIRE_THAT(bounds.z.max,  WithinULP(vectorMax.z, 1));
    }
}

TEST_CASE("Bounds::extend performs expected operation", "[Bounds]")
{
    SECTION("Per axis extend behaves as expected")
    {
        Limits limitsX1{-1.0, 1.0};
        Limits limitsY1{-2.0, 3.0};
        Limits limitsZ1{-5.0, 2.0};

        Limits limitsX2{-2.0, 1.0};
        Limits limitsY2{-3.0, 3.0};
        Limits limitsZ2{-6.0, 2.0};

        Limits limitsX3{-1.0, 2.0};
        Limits limitsY3{-2.0, 4.0};
        Limits limitsZ3{-5.0, 3.0};

        Bounds bounds;

        SECTION("X axis")
        {
            bounds.extend(limitsX1, Axis::X);

            REQUIRE_THAT(bounds.x.min,  WithinULP(limitsX1.min, 1));
            REQUIRE_THAT(bounds.x.max,  WithinULP(limitsX1.max, 1));

            REQUIRE_THAT(bounds.y.min,  WithinULP(0.0, 1));
            REQUIRE_THAT(bounds.y.max,  WithinULP(0.0, 1));

            REQUIRE_THAT(bounds.z.min,  WithinULP(0.0, 1));
            REQUIRE_THAT(bounds.z.max,  WithinULP(0.0, 1));

            bounds.extend(limitsX2, Axis::X);

            REQUIRE_THAT(bounds.x.min,  WithinULP(limitsX2.min, 1));
            REQUIRE_THAT(bounds.x.max,  WithinULP(limitsX1.max, 1));

            REQUIRE_THAT(bounds.y.min,  WithinULP(0.0, 1));
            REQUIRE_THAT(bounds.y.max,  WithinULP(0.0, 1));

            REQUIRE_THAT(bounds.z.min,  WithinULP(0.0, 1));
            REQUIRE_THAT(bounds.z.max,  WithinULP(0.0, 1));

            bounds.extend(limitsX3, Axis::X);

            REQUIRE_THAT(bounds.x.min,  WithinULP(limitsX2.min, 1));
            REQUIRE_THAT(bounds.x.max,  WithinULP(limitsX3.max, 1));

            REQUIRE_THAT(bounds.y.min,  WithinULP(0.0, 1));
            REQUIRE_THAT(bounds.y.max,  WithinULP(0.0, 1));

            REQUIRE_THAT(bounds.z.min,  WithinULP(0.0, 1));
            REQUIRE_THAT(bounds.z.max,  WithinULP(0.0, 1));
        }

        SECTION("Y axis")
        {
            bounds.extend(limitsY1, Axis::Y);

            REQUIRE_THAT(bounds.x.min,  WithinULP(0.0, 1));
            REQUIRE_THAT(bounds.x.max,  WithinULP(0.0, 1));

            REQUIRE_THAT(bounds.y.min,  WithinULP(limitsY1.min, 1));
            REQUIRE_THAT(bounds.y.max,  WithinULP(limitsY1.max, 1));

            REQUIRE_THAT(bounds.z.min,  WithinULP(0.0, 1));
            REQUIRE_THAT(bounds.z.max,  WithinULP(0.0, 1));

            bounds.extend(limitsY2, Axis::Y);

            REQUIRE_THAT(bounds.x.min,  WithinULP(0.0, 1));
            REQUIRE_THAT(bounds.x.max,  WithinULP(0.0, 1));

            REQUIRE_THAT(bounds.y.min,  WithinULP(limitsY2.min, 1));
            REQUIRE_THAT(bounds.y.max,  WithinULP(limitsY1.max, 1));

            REQUIRE_THAT(bounds.z.min,  WithinULP(0.0, 1));
            REQUIRE_THAT(bounds.z.max,  WithinULP(0.0, 1));

            bounds.extend(limitsY3, Axis::Y);

            REQUIRE_THAT(bounds.x.min,  WithinULP(0.0, 1));
            REQUIRE_THAT(bounds.x.max,  WithinULP(0.0, 1));

            REQUIRE_THAT(bounds.y.min,  WithinULP(limitsY2.min, 1));
            REQUIRE_THAT(bounds.y.max,  WithinULP(limitsY3.max, 1));

            REQUIRE_THAT(bounds.z.min,  WithinULP(0.0, 1));
            REQUIRE_THAT(bounds.z.max,  WithinULP(0.0, 1));
        }

        SECTION("Z axis")
        {
            bounds.extend(limitsZ1, Axis::Z);

            REQUIRE_THAT(bounds.x.min,  WithinULP(0.0, 1));
            REQUIRE_THAT(bounds.x.max,  WithinULP(0.0, 1));

            REQUIRE_THAT(bounds.y.min,  WithinULP(0.0, 1));
            REQUIRE_THAT(bounds.y.max,  WithinULP(0.0, 1));

            REQUIRE_THAT(bounds.z.min,  WithinULP(limitsZ1.min, 1));
            REQUIRE_THAT(bounds.z.max,  WithinULP(limitsZ1.max, 1));

            bounds.extend(limitsZ2, Axis::Z);

            REQUIRE_THAT(bounds.x.min,  WithinULP(0.0, 1));
            REQUIRE_THAT(bounds.x.max,  WithinULP(0.0, 1));

            REQUIRE_THAT(bounds.y.min,  WithinULP(0.0, 1));
            REQUIRE_THAT(bounds.y.max,  WithinULP(0.0, 1));

            REQUIRE_THAT(bounds.z.min,  WithinULP(limitsZ2.min, 1));
            REQUIRE_THAT(bounds.z.max,  WithinULP(limitsZ1.max, 1));

            bounds.extend(limitsZ3, Axis::Z);

            REQUIRE_THAT(bounds.x.min,  WithinULP(0.0, 1));
            REQUIRE_THAT(bounds.x.max,  WithinULP(0.0, 1));

            REQUIRE_THAT(bounds.y.min,  WithinULP(0.0, 1));
            REQUIRE_THAT(bounds.y.max,  WithinULP(0.0, 1));

            REQUIRE_THAT(bounds.z.min,  WithinULP(limitsZ2.min, 1));
            REQUIRE_THAT(bounds.z.max,  WithinULP(limitsZ3.max, 1));
        }

        SECTION("All axes")
        {
            bounds.extend(limitsX1, Axis::X);
            bounds.extend(limitsY1, Axis::Y);
            bounds.extend(limitsZ1, Axis::Z);

            REQUIRE_THAT(bounds.x.min,  WithinULP(limitsX1.min, 1));
            REQUIRE_THAT(bounds.x.max,  WithinULP(limitsX1.max, 1));

            REQUIRE_THAT(bounds.y.min,  WithinULP(limitsY1.min, 1));
            REQUIRE_THAT(bounds.y.max,  WithinULP(limitsY1.max, 1));

            REQUIRE_THAT(bounds.z.min,  WithinULP(limitsZ1.min, 1));
            REQUIRE_THAT(bounds.z.max,  WithinULP(limitsZ1.max, 1));

            bounds.extend(limitsX2, Axis::X);
            bounds.extend(limitsY2, Axis::Y);
            bounds.extend(limitsZ2, Axis::Z);

            REQUIRE_THAT(bounds.x.min,  WithinULP(limitsX2.min, 1));
            REQUIRE_THAT(bounds.x.max,  WithinULP(limitsX1.max, 1));

            REQUIRE_THAT(bounds.y.min,  WithinULP(limitsY2.min, 1));
            REQUIRE_THAT(bounds.y.max,  WithinULP(limitsY1.max, 1));

            REQUIRE_THAT(bounds.z.min,  WithinULP(limitsZ2.min, 1));
            REQUIRE_THAT(bounds.z.max,  WithinULP(limitsZ1.max, 1));

            bounds.extend(limitsX3, Axis::X);
            bounds.extend(limitsY3, Axis::Y);
            bounds.extend(limitsZ3, Axis::Z);

            REQUIRE_THAT(bounds.x.min,  WithinULP(limitsX2.min, 1));
            REQUIRE_THAT(bounds.x.max,  WithinULP(limitsX3.max, 1));

            REQUIRE_THAT(bounds.y.min,  WithinULP(limitsY2.min, 1));
            REQUIRE_THAT(bounds.y.max,  WithinULP(limitsY3.max, 1));

            REQUIRE_THAT(bounds.z.min,  WithinULP(limitsZ2.min, 1));
            REQUIRE_THAT(bounds.z.max,  WithinULP(limitsZ3.max, 1));
        }
    }
}

TEST_CASE("Bounds::getLimits returns expected values", "[Bounds]")
{
    Limits limitsX{-1.0, 2.0};
    Limits limitsY{-2.0, 3.0};
    Limits limitsZ{-3.0, 4.0};

    Bounds bounds{limitsX, limitsY, limitsZ};

    Limits actualLimitsX = bounds.getLimits(Axis::X);
    Limits actualLimitsY = bounds.getLimits(Axis::Y);
    Limits actualLimitsZ = bounds.getLimits(Axis::Z);

    REQUIRE_THAT(actualLimitsX.min,  WithinULP(limitsX.min, 1));
    REQUIRE_THAT(actualLimitsX.max,  WithinULP(limitsX.max, 1));

    REQUIRE_THAT(actualLimitsY.min,  WithinULP(limitsY.min, 1));
    REQUIRE_THAT(actualLimitsY.max,  WithinULP(limitsY.max, 1));

    REQUIRE_THAT(actualLimitsZ.min,  WithinULP(limitsZ.min, 1));
    REQUIRE_THAT(actualLimitsZ.max,  WithinULP(limitsZ.max, 1));
}

TEST_CASE("Bounds::operator[] returns expected values", "[Bounds]")
{
    Limits limitsX{-1.0, 2.0};
    Limits limitsY{-2.0, 3.0};
    Limits limitsZ{-3.0, 4.0};

    Bounds bounds{limitsX, limitsY, limitsZ};

    Limits actualLimitsX = bounds[Axis::X];
    Limits actualLimitsY = bounds[Axis::Y];
    Limits actualLimitsZ = bounds[Axis::Z];

    REQUIRE_THAT(actualLimitsX.min,  WithinULP(limitsX.min, 1));
    REQUIRE_THAT(actualLimitsX.max,  WithinULP(limitsX.max, 1));

    REQUIRE_THAT(actualLimitsY.min,  WithinULP(limitsY.min, 1));
    REQUIRE_THAT(actualLimitsY.max,  WithinULP(limitsY.max, 1));

    REQUIRE_THAT(actualLimitsZ.min,  WithinULP(limitsZ.min, 1));
    REQUIRE_THAT(actualLimitsZ.max,  WithinULP(limitsZ.max, 1));
}

TEST_CASE("Bounds::contains returns expected values", "[Bounds]")
{
}

TEST_CASE("Bounds::intersects returns expected values", "[Bounds]")
{
}

TEST_CASE("Bounds::minimum and ::maximum return expected values", "[Bounds]")
{
}
