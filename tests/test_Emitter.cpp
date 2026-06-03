#include <catch2/catch_all.hpp>

#include "Color.h"
#include "Emitter.h"
#include "LambertianMaterial.h"
#include "Photon.h"
#include "RandomGenerator.h"
#include "Vector.h"
#include "WorkQueue.h"

#include <vector>

using Catch::Matchers::WithinAbs;

namespace
{

// A bounce-hit on a Lambertian surface: photon travelling -z into a +z-facing
// surface at the origin, carrying a unit-ish color and a nonzero bounce depth.
PhotonHit makeLambertianHit()
{
    PhotonHit ph;
    ph.photon.ray = Ray{Vector{0, 0, 5}, Vector{0, 0, -1}};
    ph.photon.color = Color{0.6f, 0.4f, 0.2f};
    ph.photon.bounces = 1;
    ph.photon.time = 0.25f;
    ph.hit.position = Vector{0, 0, 0};
    ph.hit.normal = Vector{0, 0, 1};
    ph.hit.distance = 5.0;
    ph.hit.material = 0;
    return ph;
}

// Drive `material->generateDaughters` lazily in chunks of `chunkSizes`, mirroring
// what Worker::processEmissions does as photon-queue space is reserved across
// iterations. Returns the materialized daughters in global-index order.
std::vector<Photon> generateLazy(const Material& material,
                                 const PhotonHit& hit,
                                 size_t total,
                                 const std::vector<size_t>& chunkSizes,
                                 RandomGenerator& generator)
{
    Emitter emitter(hit, static_cast<std::uint32_t>(total));

    std::vector<Photon> out(total);
    WorkQueue<Photon>::Block block{0, total, out};

    size_t slot = 0;
    for (size_t chunk : chunkSizes)
    {
        const size_t produce = std::min(chunk, static_cast<size_t>(emitter.remaining()));
        if (produce == 0)
        {
            continue;
        }

        material.generateDaughters(block,
                                   /*blockStart=*/slot,
                                   /*globalStart=*/emitter.generated(),
                                   /*count=*/produce,
                                   /*totalDaughters=*/emitter.total(),
                                   emitter.incident,
                                   emitter.normal,
                                   emitter.position,
                                   emitter.color,
                                   emitter.time,
                                   emitter.bounces,
                                   generator);
        emitter.advance(static_cast<std::uint32_t>(produce));
        slot += produce;
    }

    REQUIRE(emitter.done());
    REQUIRE(emitter.generated() == total);
    REQUIRE(emitter.remaining() == 0);

    return out;
}

}  // namespace

TEST_CASE("Emitter generates exactly its daughter count across chunked pulls", "[Emitter]")
{
    LambertianMaterial material{"diffuse", Color{1.0f, 1.0f, 1.0f}};
    const PhotonHit hit = makeLambertianHit();
    const size_t total = material.daughterPhotonCount();
    REQUIRE(total == 9);

    // Different chunkings of the same N must all retire the emitter at exactly N
    // daughters. The helper REQUIREs done()/generated()==total internally.
    RandomGenerator g1{1234};
    auto a = generateLazy(material, hit, total, {9}, g1);
    REQUIRE(a.size() == total);

    RandomGenerator g2{1234};
    auto b = generateLazy(material, hit, total, {1, 1, 1, 1, 1, 1, 1, 1, 1}, g2);
    REQUIRE(b.size() == total);

    RandomGenerator g3{1234};
    auto c = generateLazy(material, hit, total, {4, 5}, g3);
    REQUIRE(c.size() == total);

    // Over-large final chunk is clamped to the remainder; still exactly N.
    RandomGenerator g4{1234};
    auto d = generateLazy(material, hit, total, {2, 100}, g4);
    REQUIRE(d.size() == total);
}

TEST_CASE("Lazy chunked fan-out reproduces the eager single-shot fan-out", "[Emitter]")
{
    LambertianMaterial material{"diffuse", Color{0.9f, 0.8f, 0.7f}};
    const PhotonHit hit = makeLambertianHit();
    const size_t total = material.daughterPhotonCount();

    // EAGER: one bounce() call materializing all N daughters, seed S.
    std::vector<Photon> eager(total);
    {
        WorkQueue<Photon>::Block block{0, total, eager};
        RandomGenerator g{42};
        material.bounce(block, 0, total, hit, g);
    }

    // LAZY: same seed S, generated in {3,3,3} chunks. Because daughter index 0
    // uses sampleMode() and 1..N-1 use sample(), keyed on the GLOBAL index, and
    // the energy split is 1/total (not 1/chunk), and draws happen in ascending
    // global order, the two must be bit-for-bit identical.
    RandomGenerator g{42};
    std::vector<Photon> lazy = generateLazy(material, hit, total, {3, 3, 3}, g);

    REQUIRE(eager.size() == lazy.size());
    for (size_t i = 0; i < total; ++i)
    {
        INFO("daughter index " << i);
        // Same sampled direction (proves same RNG draw drove the same global index).
        REQUIRE_THAT(eager[i].ray.direction.x, WithinAbs(lazy[i].ray.direction.x, 1e-12));
        REQUIRE_THAT(eager[i].ray.direction.y, WithinAbs(lazy[i].ray.direction.y, 1e-12));
        REQUIRE_THAT(eager[i].ray.direction.z, WithinAbs(lazy[i].ray.direction.z, 1e-12));

        // Same 1/N energy split.
        REQUIRE_THAT(eager[i].color.red, WithinAbs(lazy[i].color.red, 1e-7f));
        REQUIRE_THAT(eager[i].color.green, WithinAbs(lazy[i].color.green, 1e-7f));
        REQUIRE_THAT(eager[i].color.blue, WithinAbs(lazy[i].color.blue, 1e-7f));

        // Carried-forward state.
        REQUIRE(eager[i].bounces == lazy[i].bounces);
        REQUIRE(eager[i].bounces == hit.photon.bounces + 1);
        REQUIRE_THAT(eager[i].time, WithinAbs(lazy[i].time, 1e-9f));

        // Daughter ray originates at the hit point.
        REQUIRE_THAT(lazy[i].ray.origin.x, WithinAbs(hit.hit.position.x, 1e-12));
        REQUIRE_THAT(lazy[i].ray.origin.y, WithinAbs(hit.hit.position.y, 1e-12));
        REQUIRE_THAT(lazy[i].ray.origin.z, WithinAbs(hit.hit.position.z, 1e-12));
    }
}

TEST_CASE("Lazy fan-out conserves energy versus eager (1/N split, not 1/chunk)", "[Emitter]")
{
    LambertianMaterial material{"diffuse", Color{1.0f, 1.0f, 1.0f}};
    const PhotonHit hit = makeLambertianHit();
    const size_t total = material.daughterPhotonCount();

    std::vector<Photon> eager(total);
    {
        WorkQueue<Photon>::Block block{0, total, eager};
        RandomGenerator g{7};
        material.bounce(block, 0, total, hit, g);
    }

    RandomGenerator g{7};
    std::vector<Photon> lazy = generateLazy(material, hit, total, {2, 2, 2, 3}, g);

    // Total carried energy must match: if lazy keyed invN on the chunk size it
    // would inflate energy. Summed channel energy is the chunk-invariant check.
    float eagerSum = 0.0f;
    float lazySum = 0.0f;
    for (size_t i = 0; i < total; ++i)
    {
        eagerSum += eager[i].color.red + eager[i].color.green + eager[i].color.blue;
        lazySum += lazy[i].color.red + lazy[i].color.green + lazy[i].color.blue;
    }
    REQUIRE_THAT(lazySum, WithinAbs(eagerSum, 1e-6f));
}
