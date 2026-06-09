#pragma once

#include "Hit.h"
#include "Quad.h"
#include "Ray.h"
#include "Volume.h"

#include <optional>

// A flat quadrilateral as a first-class analytic surface, the planar analogue of
// SphereVolume. Because it derives from Volume and reports hits through the
// standard castTransformedRay interface, it participates automatically in the
// scene intersection loop, the photon bounce path, and the probe gather/deposit
// — no special-casing needed; a non-delta material on it gathers like any other
// surface. Unlike a quad built from two triangles, it has NO internal seam, so a
// wall made from one QuadVolume cannot show the shared-edge crack.
class QuadVolume : public Volume
{
public:
    QuadVolume();
    QuadVolume(size_t materialIndex);
    QuadVolume(size_t materialIndex, const Quad& quad);

    void quad(const Quad& quad);
    const Quad& quad() const;

    void origin(const Vector& origin);
    Vector origin() const;

    void edgeU(const Vector& edgeU);
    Vector edgeU() const;

    void edgeV(const Vector& edgeV);
    Vector edgeV() const;

protected:
    std::optional<Hit> castTransformedRay(const Ray& ray, std::vector<Hit>& castBuffer) const override;

private:
    Quad m_quad;
};
