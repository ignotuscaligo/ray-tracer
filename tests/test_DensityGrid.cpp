#include <catch2/catch_test_macros.hpp>

#include "Color.h"
#include "DensityGrid.h"
#include "Vector.h"

#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

namespace
{

bool approxEqual(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) <= eps;
}

}  // namespace

TEST_CASE("DensityGrid accumulates deposits into the correct cell", "[DensityGrid]")
{
    DensityGrid grid(/*cellSize=*/1.0);

    // Two deposits in the SAME cell (both floor to (0,0,0)) must sum.
    grid.add(Vector{0.1, 0.2, 0.3}, Color{1.0f, 0.0f, 0.0f});
    grid.add(Vector{0.9, 0.9, 0.9}, Color{0.0f, 2.0f, 0.0f});

    // A deposit in a DIFFERENT cell (floors to (5,0,0)) must not mix in.
    grid.add(Vector{5.5, 0.0, 0.0}, Color{0.0f, 0.0f, 3.0f});

    const DensityGrid::Cell c0 = grid.lookupCell(Vector{0.4, 0.4, 0.4});
    REQUIRE(c0.count == 2);
    REQUIRE(approxEqual(c0.power.red, 1.0f));
    REQUIRE(approxEqual(c0.power.green, 2.0f));
    REQUIRE(approxEqual(c0.power.blue, 0.0f));

    const DensityGrid::Cell c1 = grid.lookupCell(Vector{5.1, 0.0, 0.0});
    REQUIRE(c1.count == 1);
    REQUIRE(approxEqual(c1.power.blue, 3.0f));

    // Two occupied cells total.
    REQUIRE(grid.cellCount() == 2);
    REQUIRE(grid.depositCount() == 3);
}

TEST_CASE("DensityGrid negative coordinates floor correctly", "[DensityGrid]")
{
    DensityGrid grid(/*cellSize=*/2.0);

    // -0.5 / 2 = -0.25 -> floor -> -1 ; 0.5 / 2 = 0.25 -> floor -> 0. Different cells.
    grid.add(Vector{-0.5, 0.0, 0.0}, Color{1.0f});
    grid.add(Vector{0.5, 0.0, 0.0}, Color{1.0f});
    REQUIRE(grid.cellCount() == 2);

    // -3.9 / 2 = -1.95 -> floor -> -2 ; -2.1 / 2 = -1.05 -> floor -> -2. Same cell.
    DensityGrid grid2(/*cellSize=*/2.0);
    grid2.add(Vector{-3.9, 0.0, 0.0}, Color{1.0f});
    grid2.add(Vector{-2.1, 0.0, 0.0}, Color{1.0f});
    REQUIRE(grid2.cellCount() == 1);
    REQUIRE(grid2.lookupCell(Vector{-3.0, 0.0, 0.0}).count == 2);
}

TEST_CASE("DensityGrid irradiance lookup normalizes by N and cell area", "[DensityGrid]")
{
    const double cellSize = 4.0;
    DensityGrid grid(cellSize);

    // Deposit total power 8.0 (white) into one cell.
    grid.add(Vector{1.0, 1.0, 1.0}, Color{8.0f, 8.0f, 8.0f});

    const double N = 2.0;
    const Color irr = grid.lookupIrradiance(Vector{2.0, 2.0, 2.0}, N);

    // expected = (1/N) * sumPower / cellArea = (1/2) * 8 / 16 = 0.25
    const float expected = static_cast<float>((1.0 / N) * 8.0 / (cellSize * cellSize));
    REQUIRE(approxEqual(irr.red, expected));
    REQUIRE(approxEqual(irr.green, expected));
    REQUIRE(approxEqual(irr.blue, expected));

    // Empty cell -> black.
    const Color empty = grid.lookupIrradiance(Vector{100.0, 100.0, 100.0}, N);
    REQUIRE(approxEqual(empty.red, 0.0f));
    REQUIRE(approxEqual(empty.green, 0.0f));
    REQUIRE(approxEqual(empty.blue, 0.0f));
}

TEST_CASE("DensityGrid concurrent adds are race-free", "[DensityGrid]")
{
    DensityGrid grid(/*cellSize=*/1.0);

    constexpr int threadCount = 8;
    constexpr int perThread = 50000;

    // Every thread deposits power 1.0 into the SAME cell (the origin cell). The
    // running sum must equal threadCount*perThread with no lost updates — this is
    // the worst case for the shard lock (all adds collide on one cell's shard).
    std::vector<std::thread> threads;
    for (int t = 0; t < threadCount; ++t)
    {
        threads.emplace_back([&grid]() {
            for (int i = 0; i < perThread; ++i)
            {
                grid.add(Vector{0.5, 0.5, 0.5}, Color{1.0f, 0.0f, 0.0f});
            }
        });
    }
    for (auto& th : threads)
    {
        th.join();
    }

    const DensityGrid::Cell cell = grid.lookupCell(Vector{0.5, 0.5, 0.5});
    REQUIRE(cell.count == static_cast<std::uint64_t>(threadCount) * perThread);
    REQUIRE(approxEqual(cell.power.red,
                        static_cast<float>(threadCount * perThread), 1.0f));
    REQUIRE(grid.cellCount() == 1);
    REQUIRE(grid.depositCount() ==
            static_cast<std::uint64_t>(threadCount) * perThread);
}

TEST_CASE("DensityGrid memory is bounded by occupied cells, not deposit count",
          "[DensityGrid]")
{
    DensityGrid grid(/*cellSize=*/1.0);

    // A million deposits all into ONE cell: footprint stays a single cell.
    for (int i = 0; i < 1000000; ++i)
    {
        grid.add(Vector{0.5, 0.5, 0.5}, Color{0.001f});
    }
    REQUIRE(grid.cellCount() == 1);
    REQUIRE(grid.depositCount() == 1000000);
    // One-cell footprint is tiny regardless of the million deposits.
    REQUIRE(grid.memoryBytes() < 1024);
}
