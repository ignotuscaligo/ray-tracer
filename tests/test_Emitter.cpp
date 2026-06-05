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
                                   emitter.lightId,
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

TEST_CASE("Emitter retires at exactly its total across chunked pulls", "[Emitter]")
{
    LambertianMaterial material{"diffuse", Color{1.0f, 1.0f, 1.0f}};
    const PhotonHit hit = makeLambertianHit();

    // The live single-photon model uses total == 1 (one outgoing photon per
    // bounce). The Emitter machinery must retire it after a single pull.
    {
        RandomGenerator g{1234};
        auto single = generateLazy(material, hit, /*total=*/1, {1}, g);
        REQUIRE(single.size() == 1);
    }

    // The chunked-retirement bookkeeping must hold for any total (the legacy
    // experiment override path can request total > 1). Different chunkings of the
    // same total must all retire the emitter at exactly `total`. The helper
    // REQUIREs done()/generated()==total internally.
    const size_t total = 9;
    RandomGenerator g1{1234};
    REQUIRE(generateLazy(material, hit, total, {9}, g1).size() == total);

    RandomGenerator g2{1234};
    REQUIRE(generateLazy(material, hit, total, {1, 1, 1, 1, 1, 1, 1, 1, 1}, g2).size() == total);

    RandomGenerator g3{1234};
    REQUIRE(generateLazy(material, hit, total, {4, 5}, g3).size() == total);

    // Over-large final chunk is clamped to the remainder; still exactly total.
    RandomGenerator g4{1234};
    REQUIRE(generateLazy(material, hit, total, {2, 100}, g4).size() == total);
}

TEST_CASE("Single-photon scatter carries magnitude * BSDF weight (no 1/N split)", "[Emitter]")
{
    // SINGLE-PHOTON light tracing: each bounce scatters EXACTLY ONE outgoing
    // photon. The daughter carries parentColor * weight with NO 1/N division.
    // For a Lambertian under cosine-weighted sampling, weight == albedo, so the
    // outgoing photon's color is exactly parentColor * albedo.
    const Color albedo{0.9f, 0.8f, 0.7f};
    LambertianMaterial material{"diffuse", albedo};
    const PhotonHit hit = makeLambertianHit();

    std::vector<Photon> out(1);
    WorkQueue<Photon>::Block block{0, 1, out};
    RandomGenerator g{42};
    material.generateDaughters(block,
                               /*blockStart=*/0,
                               /*globalStart=*/0,
                               /*count=*/1,
                               /*totalDaughters=*/1,
                               hit.photon.ray.direction,
                               hit.hit.normal,
                               hit.hit.position,
                               hit.photon.color,
                               hit.photon.time,
                               hit.photon.bounces,
                               hit.photon.lightId,
                               g);

    // parentColor * albedo, channel-wise (cosine-weighted Lambertian weight = albedo).
    REQUIRE_THAT(out[0].color.red, WithinAbs(hit.photon.color.red * albedo.red, 1e-6f));
    REQUIRE_THAT(out[0].color.green, WithinAbs(hit.photon.color.green * albedo.green, 1e-6f));
    REQUIRE_THAT(out[0].color.blue, WithinAbs(hit.photon.color.blue * albedo.blue, 1e-6f));

    // Carried-forward state and origin at the hit point.
    REQUIRE(out[0].bounces == hit.photon.bounces + 1);
    REQUIRE_THAT(out[0].time, WithinAbs(hit.photon.time, 1e-9f));
    REQUIRE_THAT(out[0].ray.origin.x, WithinAbs(hit.hit.position.x, 1e-12));
    REQUIRE_THAT(out[0].ray.origin.y, WithinAbs(hit.hit.position.y, 1e-12));
    REQUIRE_THAT(out[0].ray.origin.z, WithinAbs(hit.hit.position.z, 1e-12));

    // Outgoing direction is in the upper hemisphere (a valid cosine sample).
    REQUIRE(Vector::dot(out[0].ray.direction, hit.hit.normal) > 0.0);
}

TEST_CASE("Single-photon scatter uses the STOCHASTIC sample, not the deterministic mode", "[Emitter]")
{
    // The single outgoing photon must be drawn from the material's stochastic
    // importance sample (sample()), NEVER sampleMode() (the BRDF peak). Using the
    // mode for a lone photon biases the estimate (the old "N=1 is biased" bug).
    // For a Lambertian the mode direction is exactly the surface normal; a fair
    // cosine draw is almost never exactly the normal, so over many seeds the
    // scattered directions must spread away from the normal rather than pinning to
    // it. We assert the mean direction is not the normal and that individual draws
    // deviate from it.
    LambertianMaterial material{"diffuse", Color{1.0f, 1.0f, 1.0f}};
    const PhotonHit hit = makeLambertianHit();
    const Vector normal = hit.hit.normal;

    int offNormal = 0;
    const int trials = 200;
    for (int t = 0; t < trials; ++t)
    {
        std::vector<Photon> out(1);
        WorkQueue<Photon>::Block block{0, 1, out};
        RandomGenerator g{static_cast<unsigned>(1000 + t)};
        material.generateDaughters(block, 0, 0, 1, 1,
                                   hit.photon.ray.direction, normal, hit.hit.position,
                                   hit.photon.color, hit.photon.time,
                                   hit.photon.bounces, hit.photon.lightId, g);
        const double cosToNormal = Vector::dot(out[0].ray.direction, normal);
        // sampleMode() would return cos == 1 exactly (direction == normal).
        if (cosToNormal < 0.9999) ++offNormal;
    }

    // The overwhelming majority of stochastic draws are off the exact normal.
    // If the code were (incorrectly) using sampleMode(), offNormal would be 0.
    REQUIRE(offNormal > trials * 9 / 10);
}

TEST_CASE("Lazy chunked single-photon scatter matches the eager single-shot path", "[Emitter]")
{
    // Even though the core model emits one photon per bounce, generateDaughters
    // remains chunk-invariant: producing `count` independent stochastic samples in
    // chunks consumes the RNG in the same order as one eager call, so the eager
    // and lazy paths stay bit-for-bit identical. (Population growth is prevented at
    // the Worker by forcing total == 1; this test exercises the primitive's
    // chunk-invariance with total > 1.)
    LambertianMaterial material{"diffuse", Color{0.9f, 0.8f, 0.7f}};
    const PhotonHit hit = makeLambertianHit();
    const size_t total = 6;

    std::vector<Photon> eager(total);
    {
        WorkQueue<Photon>::Block block{0, total, eager};
        RandomGenerator g{42};
        material.bounce(block, 0, total, hit, g);
    }

    RandomGenerator g{42};
    std::vector<Photon> lazy = generateLazy(material, hit, total, {3, 3}, g);

    REQUIRE(eager.size() == lazy.size());
    for (size_t i = 0; i < total; ++i)
    {
        INFO("sample index " << i);
        REQUIRE_THAT(eager[i].ray.direction.x, WithinAbs(lazy[i].ray.direction.x, 1e-12));
        REQUIRE_THAT(eager[i].ray.direction.y, WithinAbs(lazy[i].ray.direction.y, 1e-12));
        REQUIRE_THAT(eager[i].ray.direction.z, WithinAbs(lazy[i].ray.direction.z, 1e-12));
        // No 1/N split: each sample carries the full parentColor * weight.
        REQUIRE_THAT(eager[i].color.red, WithinAbs(lazy[i].color.red, 1e-7f));
        REQUIRE_THAT(eager[i].color.green, WithinAbs(lazy[i].color.green, 1e-7f));
        REQUIRE_THAT(eager[i].color.blue, WithinAbs(lazy[i].color.blue, 1e-7f));
        REQUIRE(eager[i].bounces == hit.photon.bounces + 1);
    }
}

TEST_CASE("Decay termination cutoff is an absolute magnitude floor", "[Decay]")
{
    const Flux threshold{1.0};

    // A photon stays alive while its current magnitude exceeds the absolute floor,
    // regardless of how bright it was emitted (emission magnitude no longer plays
    // any role in the predicate).
    REQUIRE(photonDecayAlive(/*current=*/Flux{2.0}, threshold));   // 2.0 > 1.0
    REQUIRE(photonDecayAlive(/*current=*/Flux{500.0}, threshold)); // 500 > 1.0
    REQUIRE_FALSE(photonDecayAlive(/*current=*/Flux{0.5}, threshold)); // 0.5 < 1.0
    REQUIRE_FALSE(photonDecayAlive(/*current=*/Flux{1.0}, threshold)); // not strictly greater

    // NOT scale-invariant (by design): a brighter photon survives deeper. With the
    // same absolute floor, current 5.0 lives while current 0.5 dies — the relative
    // ratio to some emission magnitude is irrelevant.
    REQUIRE(photonDecayAlive(/*current=*/Flux{5.0}, threshold));
    REQUIRE_FALSE(photonDecayAlive(/*current=*/Flux{0.5}, threshold));

    // The Photon overload uses the max colour channel as the current magnitude.
    Photon bright;
    bright.color = Color{0.2f, 1.5f, 0.1f}; // max channel 1.5 > 1.0 → alive
    REQUIRE(photonDecayAlive(bright, threshold));
    Photon dim;
    dim.color = Color{0.2f, 0.4f, 0.1f};    // max channel 0.4 < 1.0 → dead
    REQUIRE_FALSE(photonDecayAlive(dim, threshold));
}
