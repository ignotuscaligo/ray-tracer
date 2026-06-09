#pragma once

#include "Vector.h"

// A flat, parametric quadrilateral defined by an origin corner and two edge
// vectors. A surface point is
//
//     P(u, v) = origin + u * edgeU + v * edgeV,   (u, v) in [0, 1] x [0, 1]
//
// so the quad is the parallelogram spanned by edgeU and edgeV anchored at
// origin. This origin + two-edges form is the clean representation for an
// analytic planar primitive: the same (origin, basis, parameter-domain) shape
// is what a future parametric-surface toolkit (lofts / sweeps / revolutions)
// will evaluate, just with a curved P(u, v) instead of a flat one.
//
// A rectangle is the common case (edgeU perpendicular to edgeV); nothing here
// requires perpendicularity, so general parallelograms work too.
struct Quad
{
    Quad() = default;
    Quad(const Vector& origin, const Vector& edgeU, const Vector& edgeV) noexcept;

    // Build from four corners given in order around the quad (c0 -> c1 -> c2 ->
    // c3). origin = c0, edgeU = c1 - c0, edgeV = c3 - c0. For a planar quad c2 is
    // the opposite corner (origin + edgeU + edgeV) and is implied.
    static Quad fromCorners(const Vector& c0, const Vector& c1,
                            const Vector& c2, const Vector& c3) noexcept;

    // Recompute the cached normal and the parameter-recovery basis from the
    // current origin/edges. Called by the constructors; call again after
    // mutating origin/edgeU/edgeV directly.
    void update() noexcept;

    Vector origin;
    Vector edgeU;
    Vector edgeV;

    // Cached, derived from the edges by update():
    Vector normal;        // unit normal = normalize(edgeU x edgeV)

    // Basis that recovers (u, v) from a point P in the quad's plane:
    //   u = dot(P - origin, invU),  v = dot(P - origin, invV)
    // Built so dot(edgeU, invU) = 1, dot(edgeV, invU) = 0 (and symmetrically for
    // invV), i.e. the dual basis of (edgeU, edgeV). Precomputing it makes the
    // inside test a pair of dot products instead of solving a 2x2 system per ray.
    Vector invU;
    Vector invV;
};
