#include "Volume.h"

#include "AnimationQuery.h"

Volume::Volume()
    : Object()
    , m_materialIndex(-1)
{
    registerType<Volume>();
}

Volume::Volume(size_t materialIndex)
    : Object()
    , m_materialIndex(materialIndex)
{
    registerType<Volume>();
}

void Volume::materialIndex(size_t materialIndex)
{
    m_materialIndex = materialIndex;
}

size_t Volume::materialIndex() const
{
    return m_materialIndex;
}

std::optional<Hit> Volume::castRay(const Ray& ray, std::vector<Hit>& castBuffer) const
{
    return castRayAt(ray, castBuffer, 0.0f, nullptr);
}

std::optional<Hit> Volume::castRayAt(const Ray& ray, std::vector<Hit>& castBuffer,
                                      float time, const AnimationQuery* animation) const
{
    const Transform worldTransform = resolveTransformAt(time, animation);
    const Ray transformedRay = transformRay(ray, worldTransform);
    std::optional<Hit> hit = castTransformedRay(transformedRay, castBuffer);

    if (hit)
    {
        hit->position = worldTransform.position + (worldTransform.rotation * hit->position);
        hit->normal = worldTransform.rotation * hit->normal;
        hit->material = m_materialIndex;
    }

    return hit;
}

std::optional<Hit> Volume::castTransformedRay(const Ray& /*ray*/, std::vector<Hit>& /*castBuffer*/) const
{
    // Base no-op: concrete volumes (PlaneVolume, SphereVolume, MeshVolume) override.
    return std::nullopt;
}

Ray Volume::transformRay(const Ray& ray, const Transform& worldTransform) const
{
    return {
        worldTransform.rotation.inverse() * (ray.origin - worldTransform.position),
        worldTransform.rotation.inverse() * ray.direction
    };
}

Transform Volume::resolveTransformAt(float time, const AnimationQuery* animation) const
{
    if (animation)
    {
        std::optional<Transform> overridden = animation->transformAt(name(), time);
        if (overridden)
        {
            return *overridden;
        }
    }

    Transform t;
    t.position = position();
    t.rotation = rotation();
    t.scale = transform.scale;
    return t;
}
