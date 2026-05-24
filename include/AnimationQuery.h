#pragma once

#include "Transform.h"
#include "Vector.h"
#include "Quaternion.h"

#include <optional>
#include <string>

// Continuous-time animation interface. The renderer never advances "the scene" to a
// discrete frame — it asks AnimationQuery for object transforms at arbitrary timestamps.
//
// This is the foundation pillar from the architecture vision doc:
//   - photons carry an emission timestamp
//   - workers query transforms at that timestamp when computing bounces
//   - the camera filters bounces by per-pixel exposure windows
//
// With a non-trivial AnimationQuery (e.g. linear keyframes + slerp on rotation), motion
// blur falls out for free because each photon hits the scene at its own time. With the
// default StaticAnimationQuery, behavior is identical to the no-animation baseline — the
// transform is the same regardless of `time`.
//
// transformAt returns std::nullopt when the object is not animated; callers fall back to
// the object's scene-load transform. This avoids requiring the animation system to know
// about every object's static position.
class AnimationQuery
{
public:
    virtual ~AnimationQuery() = default;

    virtual std::optional<Transform> transformAt(const std::string& objectName, float time) const = 0;
};

// No-op animation query: never returns an override, so callers always fall back to the
// scene-load transform. Used as the default and as the test for "no animation".
class StaticAnimationQuery : public AnimationQuery
{
public:
    std::optional<Transform> transformAt(const std::string& /*objectName*/, float /*time*/) const override
    {
        return std::nullopt;
    }
};

// Translates a single named object at constant velocity from its scene-load position.
// All other objects are static.
//
// position(t) = basePosition + velocity * t
//
// This is the minimum-viable non-trivial AnimationQuery — enough to exercise the
// motion-blur pipeline end-to-end on rotationally-symmetric primitives (where rotation
// produces no observable change). Real keyframed animation comes later; the BRDF
// pipeline doesn't care which AnimationQuery implementation is in play, only that the
// queried transform is consistent with the photon's emission time.
class TranslatingAnimationQuery : public AnimationQuery
{
public:
    TranslatingAnimationQuery(const std::string& objectName,
                               const Vector& basePosition,
                               const Quaternion& baseRotation,
                               const Vector& velocity)
        : m_objectName(objectName)
        , m_basePosition(basePosition)
        , m_baseRotation(baseRotation)
        , m_velocity(velocity)
    {
    }

    std::optional<Transform> transformAt(const std::string& objectName, float time) const override
    {
        if (objectName != m_objectName)
        {
            return std::nullopt;
        }

        Transform t;
        t.position = m_basePosition + m_velocity * static_cast<double>(time);
        t.rotation = m_baseRotation;
        return t;
    }

private:
    std::string m_objectName;
    Vector m_basePosition;
    Quaternion m_baseRotation;
    Vector m_velocity;
};
