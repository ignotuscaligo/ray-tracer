#pragma once

#include "Property.h"
#include "Transform.h"
#include "Vector.h"
#include "Quaternion.h"

#include <optional>
#include <string>
#include <unordered_map>

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

// Keyframed animation oracle. For each animated object it holds:
//   - a position Property<Vector>     (evaluated -> Transform::position)
//   - a rotation-angle Property<double> about a fixed axis (-> Transform::rotation)
//   - a base rotation the spin angle is composed onto (the object's scene-load
//     orientation), so the authored spin layers on top of the static pose.
//
// The rotation is parameterized as a SCALAR ANGLE about an axis rather than as a
// quaternion curve on purpose: the fan deliverable spins about one axis, and a
// scalar-angle Property makes ANGULAR VELOCITY (the thing that drives motion-blur
// length) a direct, smooth, continuous-derivative function of the keyframes. The
// Hermite-eased angle curve is exactly what makes the blur smear-up as the fan
// accelerates and go sharp as it slows. The general Property mechanism could carry
// any field (material, camera) later; object transform is the wired deliverable.
//
// An object with no entry here -> transformAt returns nullopt -> caller falls back
// to the scene-load transform (a static object renders unchanged).
class KeyframedAnimationQuery : public AnimationQuery
{
public:
    struct AnimatedObject
    {
        bool hasPosition = false;
        Property<Vector> position;          // world position over time.

        bool hasRotation = false;
        Vector rotationAxis{0.0, 1.0, 0.0}; // spin axis (object/world space, unit).
        Property<double> rotationAngle;     // radians about rotationAxis over time.
        Quaternion baseRotation;            // scene-load orientation to compose onto.

        Vector scale{1.0, 1.0, 1.0};        // preserved scene-load scale.
    };

    void setObject(const std::string& name, const AnimatedObject& animated)
    {
        m_objects[name] = animated;
    }

    bool empty() const { return m_objects.empty(); }

    std::optional<Transform> transformAt(const std::string& objectName, float time) const override
    {
        auto it = m_objects.find(objectName);
        if (it == m_objects.end())
        {
            return std::nullopt;
        }

        const AnimatedObject& a = it->second;
        const double t = static_cast<double>(time);

        Transform out;
        out.scale = a.scale;

        out.position = a.hasPosition ? a.position.evaluate(t) : Vector{0.0, 0.0, 0.0};

        if (a.hasRotation)
        {
            const double angle = a.rotationAngle.evaluate(t);
            const Quaternion spin = Quaternion::fromAxisAngle(a.rotationAxis, angle);
            // Compose the authored spin onto the scene-load orientation.
            out.rotation = spin * a.baseRotation;
        }
        else
        {
            out.rotation = a.baseRotation;
        }

        return out;
    }

private:
    std::unordered_map<std::string, AnimatedObject> m_objects;
};
