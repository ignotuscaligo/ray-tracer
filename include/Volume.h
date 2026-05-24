#pragma once

#include "Hit.h"
#include "Ray.h"
#include "Object.h"
#include "Transform.h"

#include <optional>
#include <string>
#include <vector>

class AnimationQuery;

class Volume : public Object
{
public:
    Volume();
    Volume(size_t materialIndex);

    void materialIndex(size_t materialIndex);
    size_t materialIndex() const;

    // Cast a ray using the volume's static (scene-load) transform. Equivalent to
    // castRayAt(ray, buffer, 0.0, staticQuery).
    std::optional<Hit> castRay(const Ray& ray, std::vector<Hit>& castBuffer) const;

    // Cast a ray using the volume's transform as resolved by `animation` at time `t`.
    // This is the entry point that lets photons see different scene states per their
    // emission timestamp — the cornerstone of the natural-motion-blur architecture
    // pillar. When `animation` is null or returns the static transform, behavior is
    // identical to castRay.
    std::optional<Hit> castRayAt(const Ray& ray, std::vector<Hit>& castBuffer,
                                  float time, const AnimationQuery* animation) const;

protected:
    virtual std::optional<Hit> castTransformedRay(const Ray& ray, std::vector<Hit>& castBuffer) const;

private:
    Ray transformRay(const Ray& ray, const Transform& worldTransform) const;
    Transform resolveTransformAt(float time, const AnimationQuery* animation) const;

    size_t m_materialIndex;
};
