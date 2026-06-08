#include "EmitterPatch.h"

#include "AreaLight.h"
#include "Quaternion.h"

#include <limits>

namespace
{
constexpr double kSelfHitThreshold = std::numeric_limits<double>::epsilon();
}

std::vector<EmitterPatch> collectEmitterPatches(
    const std::vector<std::shared_ptr<Object>>& objects)
{
    std::vector<EmitterPatch> patches;
    for (const auto& object : objects)
    {
        if (!object->hasType<AreaLight>())
        {
            continue;
        }
        auto light = std::static_pointer_cast<AreaLight>(object);
        const Color radiance = light->surfaceRadiance();
        if (radiance.red <= 0.0f && radiance.green <= 0.0f && radiance.blue <= 0.0f)
        {
            continue;
        }

        const Quaternion orientation = light->rotation();
        EmitterPatch patch;
        patch.center = light->position();
        patch.right = (orientation * Vector::unitX).normalized();
        patch.up = (orientation * Vector::unitY).normalized();
        patch.normal = (orientation * Vector::unitZ).normalized();
        patch.radiance = radiance;
        if (light->shape() == AreaLight::Shape::Disc)
        {
            patch.isDisc = true;
            patch.radius = light->radius();
        }
        else
        {
            patch.halfWidth = light->width() * 0.5;
            patch.halfHeight = light->height() * 0.5;
        }
        patches.push_back(patch);
    }
    return patches;
}

std::optional<double> intersectEmitterPatch(const EmitterPatch& patch, const Ray& ray)
{
    const double denom = Vector::dot(ray.direction, patch.normal);
    // Front face only: the camera must look at the emitting side, i.e. the ray
    // travels against the normal (denom < 0).
    if (denom >= -kSelfHitThreshold)
    {
        return std::nullopt;
    }

    const double t = Vector::dot(patch.center - ray.origin, patch.normal) / denom;
    if (t <= kSelfHitThreshold)
    {
        return std::nullopt;
    }

    const Vector hitPoint = ray.origin + (ray.direction * t);
    const Vector local = hitPoint - patch.center;
    const double u = Vector::dot(local, patch.right);
    const double v = Vector::dot(local, patch.up);

    if (patch.isDisc)
    {
        if ((u * u + v * v) > (patch.radius * patch.radius))
        {
            return std::nullopt;
        }
    }
    else
    {
        if (std::abs(u) > patch.halfWidth || std::abs(v) > patch.halfHeight)
        {
            return std::nullopt;
        }
    }
    return t;
}
