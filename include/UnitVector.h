#pragma once

#include "Contracts.h"
#include "Vector.h"

#include <cmath>

// ============================================================================
// UnitVector — a direction guaranteed to be unit length BY CONSTRUCTION.
//
// review-2 §4c-1 calls an un-normalized vector fed into a dot product (a facing
// cosine, a BSDF foreshortening term, a reflection) "the most pervasive latent-
// bug class" in the renderer: `Vector` is one type used for positions,
// directions, AND normals interchangeably, and every BSDF sample/evaluate/pdf
// and every facing dot silently ASSUMES its normal argument is normalized. A
// caller passing `hit.position - center` (un-normalized) where a normal is
// expected is a silent correctness bug, not a compile error.
//
// UnitVector makes "this direction is normalized" a property the type system
// tracks. It can be obtained only by:
//   * UnitVector::normalize(v)         — normalize an arbitrary Vector, or
//   * UnitVector::alreadyNormalized(v) — an ASSERTED factory (a contract checks
//                                        |v| == 1 in debug/sanitizer builds) for
//                                        the cases where a vector is unit by
//                                        construction (e.g. Vector::unitZ, an
//                                        axis, an already-normalized result).
// There is NO public constructor from a bare Vector, so you cannot accidentally
// label an un-normalized vector as a UnitVector.
//
// It converts IMPLICITLY to const Vector& (the underlying storage), so every
// existing function taking `const Vector&` keeps working unchanged — migrating a
// producer/boundary to UnitVector does not force its consumers to migrate in the
// same pass. This is what lets the migration be incremental.
//
// Zero-cost: one Vector member, all ops inline; in release the normalization is
// the same code the call sites already ran, and the asserted factory's contract
// compiles out.
// ============================================================================
class UnitVector
{
public:
    // Normalize an arbitrary vector. This is the safe default constructor-of-record.
    static UnitVector normalize(const Vector& v) noexcept
    {
        return UnitVector{v.normalized()};
    }

    // Assert that `v` is ALREADY unit length and wrap it without renormalizing.
    // Use only when the vector is unit by construction; the contract catches a
    // mislabel in debug/sanitizer builds. Inert (zero-cost) in release.
    static UnitVector alreadyNormalized(const Vector& v) noexcept
    {
        INVARIANT_MSG(std::abs(v.magnitudeSquared() - 1.0) < 1e-6,
                      "UnitVector::alreadyNormalized given a non-unit vector");
        return UnitVector{v};
    }

    // Implicit conversion to the underlying Vector: existing `const Vector&` APIs
    // (dot, evaluate, every bit of math) accept a UnitVector with no call-site
    // change. The conversion is to a reference into this object's storage.
    operator const Vector&() const noexcept { return m_vector; }

    const Vector& vector() const noexcept { return m_vector; }

    // Named-axis passthroughs so a UnitVector reads like a direction at use sites.
    double x() const noexcept { return m_vector.x; }
    double y() const noexcept { return m_vector.y; }
    double z() const noexcept { return m_vector.z; }

    // The negation of a unit vector is still unit length — preserve the type so a
    // flipped normal / reversed incident direction stays a UnitVector.
    UnitVector operator-() const noexcept { return UnitVector{-m_vector}; }

private:
    explicit UnitVector(const Vector& v) noexcept : m_vector(v) {}

    Vector m_vector;
};
