#pragma once

#include "Transform.h"

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
class AnimationQuery
{
public:
    virtual ~AnimationQuery() = default;

    // Resolve the transform of an object identified by `objectName` at the given time
    // (seconds). Implementations may interpolate keyframes, evaluate procedural rigs, or
    // simply return the static scene-load transform.
    virtual Transform transformAt(const std::string& objectName, float time) const = 0;
};

// No-op animation query: returns the same transform regardless of `time`. Used until a
// real animation system exists. The transform itself is supplied by the caller (typically
// by passing the object's scene-load transform).
class StaticAnimationQuery : public AnimationQuery
{
public:
    Transform transformAt(const std::string& /*objectName*/, float /*time*/) const override
    {
        return m_transform;
    }

    void setTransform(const Transform& transform) { m_transform = transform; }

private:
    Transform m_transform;
};
