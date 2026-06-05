#pragma once

#include <compare>
#include <cstddef>

// ============================================================================
// Flux — a strong type for an ABSOLUTE photometric flux-bundle magnitude.
//
// Photon magnitude in this renderer is `Phi / photonsPerLight` (DESIGN.md §2a):
// an absolute, scene-dependent value, often in the HUNDREDS, NOT a [0,1]
// fraction and NOT near 1.0. The `terminationThreshold` is a floor expressed in
// these same units. Because the magnitude is absolute, a bare `double` default
// (the historical `terminationThreshold = 1.0`) is a documented units trap: 1.0
// is meaningless without knowing the scene's per-photon emission magnitude.
//
// `Flux` makes those units explicit in the type system and makes the trap harder
// to hit: the termination comparison is `Flux > Flux`, so you cannot
// accidentally compare a per-photon magnitude against, say, a luminance, a pdf,
// or a [0,1] reflectance — those are different `double`s with different units,
// and the compiler now knows Flux is its own type. The wrapper is zero-cost
// (a single double, trivially copyable; all ops are constexpr/inline) so the
// hot termination check compiles to the same code as the bare comparison.
//
// SCOPE (deliberately contained): this wraps the termination threshold and the
// per-photon magnitude used in the decay predicate ONLY. It is NOT threaded
// through every Color/double in the renderer — that would sprawl with no payoff.
// The single boundary it guards is "is this photon's bundle magnitude above the
// absolute termination floor?", which is exactly the §2a units trap.
// ============================================================================
class Flux
{
public:
    constexpr Flux() noexcept = default;

    // Explicit so a bare double can't silently become a Flux — constructing one
    // is the point at which you assert "this number is in flux-bundle units".
    constexpr explicit Flux(double value) noexcept : m_value(value) {}

    // The underlying magnitude, for the geometry/energy math that still operates
    // on raw doubles (Color channels, gather divides). Naming it `value()` keeps
    // the unwrap explicit at the few call sites that need it.
    constexpr double value() const noexcept { return m_value; }

    // Ordering is the only operation the termination boundary needs: a photon is
    // alive iff its magnitude exceeds the threshold. Defaulted three-way compare
    // gives <, >, <=, >=, ==, != between two Flux values — and ONLY between two
    // Flux values, which is the safety property (a Flux can't be compared to a
    // bare double without an explicit Flux{...} wrap).
    friend constexpr auto operator<=>(const Flux&, const Flux&) noexcept = default;
    friend constexpr bool operator==(const Flux&, const Flux&) noexcept = default;

private:
    double m_value = 0.0;
};
