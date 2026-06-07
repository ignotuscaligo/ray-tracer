#pragma once

#include "Vector.h"

#include <algorithm>
#include <vector>

// ============================================================================
// Property<T> — a value that is either CONSTANT or an ANIMATED keyframe curve.
// ============================================================================
//
// This is the animation system's atom. Any field that can vary over time (an
// object's rotation angle, a position component, later a material parameter or a
// camera attribute) is expressed as a Property<T>. The renderer never advances
// "the scene" to a discrete frame; it asks each Property for its value at an
// arbitrary timestamp via evaluate(timeSeconds).
//
//   - A CONSTANT Property returns the same value at every time. A static scene
//     built entirely from constant Properties is therefore bit-for-bit identical
//     to the pre-animation baseline — evaluate(t) is independent of t.
//   - An ANIMATED Property holds a sorted list of keyframes (time, value, +
//     optional tangents) and interpolates between them with a SMOOTH cubic
//     Hermite spline so that the VELOCITY (first derivative) is continuous across
//     keyframes. Continuous velocity is the load-bearing property for motion
//     blur: blur length tracks instantaneous speed, so an eased spin-up smears
//     progressively rather than in discrete jumps.
//
// [INVARIANT] evaluate() at a keyframe's exact time returns that keyframe's value
// exactly (the Hermite basis has h00(0)=1, h00(1)=0, etc.). Endpoints are exact;
// times outside the keyframe range CLAMP to the nearest endpoint value (held, not
// extrapolated) — a spinning object holds its first/last pose outside its
// authored interval rather than flying off via extrapolation.
//
// T must support: T + T, T * scalar (double), and T - T (for the auto-tangent
// finite difference). double and Vector both satisfy this.

template <typename T>
struct Keyframe
{
    double time = 0.0;
    T value{};

    // Optional explicit tangents (value-units per second). When useAutoTangent is
    // true the interpolator computes a Catmull-Rom-style tangent from the
    // neighbouring keyframes instead, giving a smooth curve through all points
    // without the author having to specify slopes. Explicit tangents let an author
    // pin a velocity (e.g. zero at a hold) when desired.
    T inTangent{};
    T outTangent{};
    bool useAutoTangent = true;
};

template <typename T>
class Property
{
public:
    // Construct a CONSTANT property (the default for any non-animated field).
    Property() = default;
    explicit Property(const T& constantValue)
        : m_constant(constantValue)
    {
    }

    // Replace the constant value (only meaningful while this is a constant).
    void setConstant(const T& value)
    {
        m_keyframes.clear();
        m_constant = value;
    }

    // Add a keyframe. Keyframes are kept sorted by time. Adding any keyframe makes
    // the property ANIMATED; the stored constant is then ignored by evaluate().
    void addKeyframe(const Keyframe<T>& key)
    {
        auto insertAt = std::upper_bound(
            m_keyframes.begin(), m_keyframes.end(), key,
            [](const Keyframe<T>& a, const Keyframe<T>& b) { return a.time < b.time; });
        m_keyframes.insert(insertAt, key);
    }

    void addKeyframe(double time, const T& value)
    {
        Keyframe<T> key;
        key.time = time;
        key.value = value;
        addKeyframe(key);
    }

    bool isAnimated() const { return !m_keyframes.empty(); }
    size_t keyframeCount() const { return m_keyframes.size(); }
    const std::vector<Keyframe<T>>& keyframes() const { return m_keyframes; }

    // Evaluate the property at a time (seconds). Constant -> the constant value at
    // all times. Animated -> clamped, cubic-Hermite-interpolated keyframe value.
    T evaluate(double timeSeconds) const
    {
        if (m_keyframes.empty())
        {
            return m_constant;
        }

        if (m_keyframes.size() == 1)
        {
            return m_keyframes.front().value;
        }

        // Clamp (hold) outside the authored range — no extrapolation.
        if (timeSeconds <= m_keyframes.front().time)
        {
            return m_keyframes.front().value;
        }
        if (timeSeconds >= m_keyframes.back().time)
        {
            return m_keyframes.back().value;
        }

        // Find the segment [k0, k1] containing timeSeconds. upper_bound gives the
        // first keyframe strictly after the time; the segment start is the one
        // before it. The range checks above guarantee 1 <= idx <= size-1.
        size_t idx = 1;
        while (idx < m_keyframes.size() && m_keyframes[idx].time <= timeSeconds)
        {
            ++idx;
        }
        const Keyframe<T>& k0 = m_keyframes[idx - 1];
        const Keyframe<T>& k1 = m_keyframes[idx];

        const double dt = k1.time - k0.time;
        if (dt <= 0.0)
        {
            return k0.value;  // coincident keyframes: degenerate, hold the first.
        }

        const double s = (timeSeconds - k0.time) / dt;  // normalized [0,1] in segment.

        // Tangents in value-units per second. For auto tangents we use a
        // Catmull-Rom finite difference over neighbours (one-sided at the ends),
        // which produces a smooth interpolating spline with continuous velocity.
        const T m0 = outTangentOf(idx - 1);
        const T m1 = inTangentOf(idx);

        // Hermite basis (s in [0,1]); tangents are scaled by dt to convert from
        // per-second to per-segment so velocity is continuous across segments of
        // unequal duration.
        const double s2 = s * s;
        const double s3 = s2 * s;
        const double h00 = 2.0 * s3 - 3.0 * s2 + 1.0;
        const double h10 = s3 - 2.0 * s2 + s;
        const double h01 = -2.0 * s3 + 3.0 * s2;
        const double h11 = s3 - s2;

        return k0.value * h00
             + m0 * (h10 * dt)
             + k1.value * h01
             + m1 * (h11 * dt);
    }

private:
    // Auto (Catmull-Rom) tangent at keyframe i, in value-units per second:
    //   m_i = (v_{i+1} - v_{i-1}) / (t_{i+1} - t_{i-1})
    // One-sided difference at the boundaries.
    T autoTangent(size_t i) const
    {
        const size_t n = m_keyframes.size();
        if (n < 2)
        {
            return T{};
        }
        if (i == 0)
        {
            const double span = m_keyframes[1].time - m_keyframes[0].time;
            if (span <= 0.0) return T{};
            return (m_keyframes[1].value - m_keyframes[0].value) * (1.0 / span);
        }
        if (i == n - 1)
        {
            const double span = m_keyframes[n - 1].time - m_keyframes[n - 2].time;
            if (span <= 0.0) return T{};
            return (m_keyframes[n - 1].value - m_keyframes[n - 2].value) * (1.0 / span);
        }
        const double span = m_keyframes[i + 1].time - m_keyframes[i - 1].time;
        if (span <= 0.0) return T{};
        return (m_keyframes[i + 1].value - m_keyframes[i - 1].value) * (1.0 / span);
    }

    T outTangentOf(size_t i) const
    {
        return m_keyframes[i].useAutoTangent ? autoTangent(i) : m_keyframes[i].outTangent;
    }

    T inTangentOf(size_t i) const
    {
        return m_keyframes[i].useAutoTangent ? autoTangent(i) : m_keyframes[i].inTangent;
    }

    T m_constant{};
    std::vector<Keyframe<T>> m_keyframes;
};
