#include "Ray.h"

#include "Utility.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>

Ray::Ray(Vector iorigin, Vector idirection) noexcept
    : origin(iorigin)
    , direction(idirection)
{
}

bool rayIntersectsBounds(const Ray& ray, const Bounds& bounds) noexcept
{
    double tmin = 0.0;
    double tmax = std::numeric_limits<double>::max();

    for (int i = 0; i < 3; ++i)
    {
        const Axis axis = static_cast<Axis>(i);
        const Limits limits = bounds[axis];
        const double origin = ray.origin[axis];
        const double direction = ray.direction[axis];

        if (std::abs(direction) < std::numeric_limits<double>::epsilon())
        {
            if (origin < limits.min || origin > limits.max)
            {
                return false;
            }
        }
        else
        {
            const double ood = 1.0 / direction;
            double t1 = (limits.min - origin) * ood;
            double t2 = (limits.max - origin) * ood;

            if (t1 > t2)
            {
                std::swap(t1, t2);
            }

            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);

            if (tmin > tmax)
            {
                return false;
            }
        }
    }

    return true;
}

// Watertight ray/triangle intersection (Woop, Benthin, Wald, "Watertight
// Ray/Triangle Intersection", JCGT 2013).
//
// Why this replaces the previous Möller-Trumbore-style test: the old code
// rejected a hit whenever a barycentric coordinate went slightly negative
// (`v < 0.0`, `w < 0.0`). For a point lying exactly on an edge shared by two
// adjacent triangles (e.g. the diagonal each Cornell wall quad is split along),
// floating-point rounding can push the relevant edge coordinate just below zero
// for BOTH triangles, so neither claims the hit and the ray slips through the
// crack to the background — the thin black line along the wall diagonals.
//
// The watertight algorithm guarantees that adjacent triangles tile their shared
// plane with no gaps and no overlaps. Two properties make it work:
//   1. The ray is transformed into a coordinate space where its direction is
//      +Z (axis permutation chosen from the ray alone + a shear). Because this
//      transform depends ONLY on the ray, every triangle that shares an edge
//      sees that edge's endpoints mapped to the SAME 2D points.
//   2. Each edge function U/V/W is the 2D cross product of two such mapped
//      points. For a shared edge, the two triangles traverse it in opposite
//      winding order, so they compute edge-function values that are exact
//      negations of each other. A consistent tie-break on the boundary
//      (sign == 0) therefore assigns the on-edge hit to exactly one triangle.
//
// Backface culling (the `det > 0` requirement below) is preserved so visible
// behaviour matches the previous front-face-only test; the watertightness fix
// is purely in how the inside/edge test is evaluated.
std::optional<Hit> rayIntersectsTriangle(const Ray& ray, const Triangle& triangle) noexcept
{
    // Disable floating-point contraction (FMA fusion) for this routine. The
    // watertightness proof requires that for a shared edge the two adjacent
    // triangles compute exactly negated edge-function values. That holds only if
    // each product is individually rounded: an `a*b - c*d` fused into a single
    // FMA keeps an unrounded intermediate, so the two triangles' values stop
    // being exact negations and a ray on the shared edge can be rejected by
    // both — reopening the crack. Force per-operation rounding here.
#if defined(__clang__)
#pragma clang fp contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

    // --- Step 1: pick the ray's dominant axis and permute so it becomes Z. ---
    // Using the largest-magnitude component keeps the shear well-conditioned.
    const double adx = std::abs(ray.direction.x);
    const double ady = std::abs(ray.direction.y);
    const double adz = std::abs(ray.direction.z);

    int kz = 2;
    if (adx > ady && adx > adz)
    {
        kz = 0;
    }
    else if (ady > adz)
    {
        kz = 1;
    }

    int kx = kz + 1;
    if (kx == 3)
    {
        kx = 0;
    }
    int ky = kx + 1;
    if (ky == 3)
    {
        ky = 0;
    }

    const Axis axisX = static_cast<Axis>(kx);
    const Axis axisY = static_cast<Axis>(ky);
    const Axis axisZ = static_cast<Axis>(kz);

    // Preserve winding: if the dominant direction component is negative, swap
    // kx/ky so the permuted triangle keeps its original orientation. Without
    // this the determinant sign would flip and break the backface convention.
    Axis kxAxis = axisX;
    Axis kyAxis = axisY;
    if (ray.direction.getAxis(axisZ) < 0.0)
    {
        std::swap(kxAxis, kyAxis);
    }

    // --- Step 2: calculate the shear constants. ---
    const double dkx = ray.direction.getAxis(kxAxis);
    const double dky = ray.direction.getAxis(kyAxis);
    const double dkz = ray.direction.getAxis(axisZ);
    const double sx = dkx / dkz;
    const double sy = dky / dkz;
    const double sz = 1.0 / dkz;

    // --- Step 3: triangle vertices relative to the ray origin. ---
    const Vector vA = triangle.a - ray.origin;
    const Vector vB = triangle.b - ray.origin;
    const Vector vC = triangle.c - ray.origin;

    // --- Step 4: shear + scale into the ray-aligned 2D space. ---
    const double aKz = vA.getAxis(axisZ);
    const double bKz = vB.getAxis(axisZ);
    const double cKz = vC.getAxis(axisZ);

    const double ax = vA.getAxis(kxAxis) - sx * aKz;
    const double ay = vA.getAxis(kyAxis) - sy * aKz;
    const double bx = vB.getAxis(kxAxis) - sx * bKz;
    const double by = vB.getAxis(kyAxis) - sy * bKz;
    const double cx = vC.getAxis(kxAxis) - sx * cKz;
    const double cy = vC.getAxis(kyAxis) - sy * cKz;

    // --- Step 5: edge functions (scaled barycentric coordinates). ---
    //
    // These are evaluated in plain double and NOT recomputed in higher
    // precision. The watertightness proof relies on a key IEEE-754 property:
    // for an edge shared by two triangles, the two triangles compute that
    // edge's function from the same two projected points in opposite order, and
    // because floating-point multiplication is commutative (a*b == b*a exactly)
    // the two results are EXACT negations of each other. So on a shared edge one
    // triangle's value is >= 0 and the other's is <= 0 — bit-for-bit. Promoting
    // to long double would widen the intermediate products and destroy that
    // exact-negation property, reopening the crack. Keep it in double.
    const double u = cx * by - cy * bx;
    const double v = ax * cy - ay * cx;
    const double w = bx * ay - by * ax;

    // Top-left fill rule for the on-edge (== 0) case. A hit exactly on a shared
    // edge yields a zero edge function for BOTH adjacent triangles, which would
    // otherwise make both claim it (a harmless double-hit) — but a deterministic
    // result wants exactly one owner. The two triangles traverse the shared edge
    // in opposite directions, so their projected edge vectors are exact
    // negations; a rule based on the edge vector's orientation is therefore true
    // for exactly one of them. We accept a zero edge function only when its edge
    // is "top-left": pointing in -y, or horizontal pointing in -x. (The edge
    // owning each function is A->B for w, B->C for u, C->A for v.)
    const auto edgeAccept = [](double edgeValue, double ex, double ey) noexcept
    {
        if (edgeValue > 0.0)
        {
            return true;
        }
        if (edgeValue < 0.0)
        {
            return false;
        }
        // edgeValue == 0: the hit lies on this edge. Own it iff top-left.
        return (ey < 0.0) || (ey == 0.0 && ex < 0.0);
    };

    // Backface cull combined with the inside/edge test: every edge function must
    // be accepted. Off-edge interior hits need all three strictly positive;
    // on-edge hits are admitted for exactly one triangle by the fill rule.
    if (!edgeAccept(w, bx - ax, by - ay) ||   // edge A->B
        !edgeAccept(u, cx - bx, cy - by) ||   // edge B->C
        !edgeAccept(v, ax - cx, ay - cy))     // edge C->A
    {
        return std::nullopt;
    }

    // det == 0 only for a degenerate or exactly edge-on (zero projected area)
    // triangle; reject it as parallel/grazing.
    const double det = u + v + w;

    if (det <= 0.0)
    {
        return std::nullopt;
    }

    // --- Step 6: scaled hit distance, with the same backface-cull sign. ---
    const double az = sz * aKz;
    const double bz = sz * bKz;
    const double cz = sz * cKz;
    const double tScaled = u * az + v * bz + w * cz;

    // A hit in front of the origin needs tScaled > 0 (det is already > 0 here,
    // the front-facing case matching the previous `d > 0` cull).
    if (tScaled <= 0.0)
    {
        return std::nullopt;
    }

    const double invDet = 1.0 / det;

    // Barycentric coordinates: u weights vertex A, v weights B, w weights C.
    const Vector coords{u * invDet, v * invDet, w * invDet};

    Hit hit;
    hit.position = triangle.getPosition(coords);
    hit.normal = triangle.getNormal(coords).normalize();
    hit.distance = (hit.position - ray.origin).magnitude();

    return hit;
}

std::optional<Hit> rayIntersectsPlane(const Ray& ray, const Plane& plane) noexcept
{
    const double dot = Vector::dot(plane.normal, ray.direction);

    if (std::abs(dot) <= std::numeric_limits<double>::epsilon())
    {
        return std::nullopt;
    }

    const double t = (plane.dot - Vector::dot(plane.normal, ray.origin)) / dot;

    if (t < 0.0)
    {
        return std::nullopt;
    }

    Hit hit;
    hit.position = ray.origin + (t * ray.direction);
    hit.normal = plane.normal;
    hit.distance = (hit.position - ray.origin).magnitude();

    return hit;
}

std::optional<Hit> rayIntersectsSphere(const Ray& ray, const Sphere& sphere) noexcept
{
    // Closed-form ray-sphere intersection. The ray direction is not assumed to
    // be unit length, so we solve the general quadratic
    //     a t^2 + b t + c = 0
    // where a = dir . dir, b = 2 (origin - center) . dir, c = |origin - center|^2 - r^2.
    // We pick the nearest root with t > epsilon, which correctly handles rays
    // that originate inside the sphere (one root negative, one positive) and
    // grazing rays (discriminant ~ 0).
    const Vector oc = ray.origin - sphere.center;

    const double a = Vector::dot(ray.direction, ray.direction);

    if (std::abs(a) <= std::numeric_limits<double>::epsilon())
    {
        return std::nullopt;
    }

    const double b = 2.0 * Vector::dot(oc, ray.direction);
    const double c = Vector::dot(oc, oc) - sphere.radius * sphere.radius;

    const double discriminant = b * b - 4.0 * a * c;

    if (discriminant < 0.0)
    {
        return std::nullopt;
    }

    const double sqrtDiscriminant = std::sqrt(discriminant);
    const double inverse = 1.0 / (2.0 * a);

    // Nearest root first.
    double t = (-b - sqrtDiscriminant) * inverse;

    constexpr double epsilon = 1e-6;

    if (t <= epsilon)
    {
        // Either the nearest intersection is behind the origin or we are inside
        // the sphere; fall back to the far root.
        t = (-b + sqrtDiscriminant) * inverse;

        if (t <= epsilon)
        {
            return std::nullopt;
        }
    }

    Hit hit;
    hit.position = ray.origin + (t * ray.direction);
    hit.normal = (hit.position - sphere.center).normalize();
    hit.distance = (hit.position - ray.origin).magnitude();

    return hit;
}

std::optional<Hit> rayIntersectsQuad(const Ray& ray, const Quad& quad) noexcept
{
    // Step 1: ray-plane. The quad's plane passes through `origin` with `normal`.
    const double denom = Vector::dot(quad.normal, ray.direction);

    if (std::abs(denom) <= std::numeric_limits<double>::epsilon())
    {
        // Ray parallel to the plane.
        return std::nullopt;
    }

    const Vector toOrigin = quad.origin - ray.origin;
    const double t = Vector::dot(toOrigin, quad.normal) / denom;

    constexpr double epsilon = 1e-9;

    if (t <= epsilon)
    {
        // Plane is behind the ray origin (or coincident).
        return std::nullopt;
    }

    const Vector position = ray.origin + (t * ray.direction);

    // Step 2: inside test in the quad's own 2D parameter basis. Recover (u, v)
    // by projecting onto the precomputed dual basis; the point is inside iff
    // both lie in [0, 1].
    const Vector local = position - quad.origin;
    const double u = Vector::dot(local, quad.invU);
    const double v = Vector::dot(local, quad.invV);

    if (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0)
    {
        return std::nullopt;
    }

    Hit hit;
    hit.position = position;
    // Two-sided: orient the shading normal against the incoming ray so a quad
    // is visible (and lit) from either face — the natural behaviour for walls
    // authored without caring which way the geometric normal points.
    hit.normal = denom > 0.0 ? -quad.normal : quad.normal;
    hit.uv = Vector{u, v, 0.0};
    hit.distance = (hit.position - ray.origin).magnitude();

    return hit;
}
