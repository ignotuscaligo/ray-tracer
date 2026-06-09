#include "Quad.h"

Quad::Quad(const Vector& iorigin, const Vector& iedgeU, const Vector& iedgeV) noexcept
    : origin(iorigin)
    , edgeU(iedgeU)
    , edgeV(iedgeV)
{
    update();
}

Quad Quad::fromCorners(const Vector& c0, const Vector& c1,
                       const Vector& c2, const Vector& c3) noexcept
{
    // origin = c0; the two edges leave c0 along adjacent corners. c2 (the corner
    // opposite c0) is implied by origin + edgeU + edgeV for a planar quad and is
    // accepted only to make the 4-corner JSON form natural to author.
    (void)c2;
    return Quad(c0, c1 - c0, c3 - c0);
}

void Quad::update() noexcept
{
    normal = Vector::cross(edgeU, edgeV);
    normal.normalize();

    // Dual basis of (edgeU, edgeV) within the quad's plane, via the 2x2 Gram
    // matrix. This recovers parameters by dot products:
    //   u = dot(P - origin, invU),  v = dot(P - origin, invV).
    const double g11 = Vector::dot(edgeU, edgeU);
    const double g22 = Vector::dot(edgeV, edgeV);
    const double g12 = Vector::dot(edgeU, edgeV);

    const double det = g11 * g22 - g12 * g12;

    if (det == 0.0)
    {
        // Degenerate quad (collinear or zero-length edges). Leave a zero basis;
        // intersection will reject everything via the parameter test.
        invU = Vector{0.0, 0.0, 0.0};
        invV = Vector{0.0, 0.0, 0.0};
        return;
    }

    const double invDet = 1.0 / det;

    invU = (g22 * edgeU - g12 * edgeV) * invDet;
    invV = (g11 * edgeV - g12 * edgeU) * invDet;
}
