#pragma once

#include "Light.h"
#include "RandomGenerator.h"
#include "Vector.h"

// A finite-area Lambertian emitter. Unlike the point-like OmniLight/SpotLight,
// photons originate from a uniformly-sampled point ACROSS the light's surface
// and leave in a cosine-weighted direction about the surface normal. Because the
// origins are spread over an area, occluders cast PENUMBRAE (soft shadows) for
// free — no shadow-specific code is needed anywhere in the pipeline.
//
// Geometry. The emitter is a planar quad (square/rectangle) or a disc:
//   - center      : the light's position() (origin of the surface).
//   - normal      : forward() == rotation * +Z. Photons leave into the
//                   hemisphere about this normal (Lambertian).
//   - right / up  : the in-plane basis, rotation * +X and rotation * +Y. The
//                   quad spans [-width/2, +width/2] along right and
//                   [-height/2, +height/2] along up. For a disc, `radius` is
//                   used and width/height are ignored.
//
// Power. Lights here are count-independent: each photon carries the light's
// total luminous flux Phi (lumens), and a single 1/N divide happens once at
// image conversion. For a Lambertian area emitter the projected solid angle of
// a cosine-weighted hemisphere is pi, so Phi = I * pi where I is the on-normal
// luminous intensity (candela). That makes m_emissionSolidAngle = pi.
//
// To make an area light directly comparable to a point light of a given total
// flux, "$luminousFlux" sets Phi directly (overriding the I*pi computation).
// An AreaLight and an OmniLight that report the same luminousFlux() deposit the
// same total energy into the scene, so they produce equal mean luminance.
class AreaLight : public Light
{
public:
    enum class Shape
    {
        Square,
        Disc
    };

    AreaLight();

    void shape(Shape shape);
    Shape shape() const;

    // Square/rectangle extents (full width/height, in world units along the
    // in-plane right/up axes). Ignored for Disc.
    void width(double width);
    double width() const;
    void height(double height);
    double height() const;

    // Disc radius (world units). Ignored for Square.
    void radius(double radius);
    double radius() const;

    // Direct total-flux override (lumens). When > 0 it replaces I*pi as Phi,
    // making this light directly energy-comparable to any other light with the
    // same luminousFlux(). 0 (default) keeps the I*pi convention.
    void luminousFluxOverride(double flux);
    double luminousFluxOverride() const;

    double luminousFlux() const override;

    // Surface area of the emitter (world units^2): width*height for a square,
    // pi*radius^2 for a disc.
    double surfaceArea() const;

    // Emitted SURFACE RADIANCE L (the view-independent radiance a Lambertian
    // emitter shows in every direction of its hemisphere). For a Lambertian
    // emitter of radiant exitance M = Phi / area, L = M / pi. This is the value
    // a camera looking directly at the fixture should read, so the panel appears
    // as a uniform patch at the light's true radiance — no special primary-ray-
    // vs-light path, the emissive gather reads this the way the mirror gather
    // reads a lit surface's outgoing radiance. Returns black if the area is
    // degenerate.
    Color surfaceRadiance() const;

    void emit(WorkQueue<Photon>::Block photonBlock, double photonFlux, RandomGenerator& generator) const override;

private:
    Shape m_shape = Shape::Square;
    double m_width = 0.0;
    double m_height = 0.0;
    double m_radius = 0.0;
    double m_luminousFluxOverride = 0.0;
};
