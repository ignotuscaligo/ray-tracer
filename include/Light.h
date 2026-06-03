#pragma once

#include "Color.h"
#include "Object.h"
#include "Photon.h"
#include "RandomGenerator.h"
#include "WorkQueue.h"

// Wave 2 (physical units): lights are now defined PHOTOMETRICALLY.
//
// The primary physical input is luminous INTENSITY in candela (cd = lumens per
// steradian). The total emitted luminous flux is
//   Phi = I * Omega   lumens,
// where Omega is the solid angle (steradians) the light emits into:
//   - OmniLight (isotropic point source): Omega = 4*pi  =>  Phi = 4*pi*I
//   - SpotLight (cone):                   Omega = spherical-cap solid angle
//   - ParallelLight (collimated beam):    no true solid angle; Omega is the
//     emitter disc area (a documented radiometric simplification — a parallel
//     beam's flux is irradiance*area, here folded into I*area).
//
// `luminousFlux()` returns Phi. That is the COUNT-INDEPENDENT weight each emitted
// photon carries (see LightQueue / Renderer). The legacy `brightness` setter is
// retained as an alias onto intensity so older scenes keep loading; candela is
// the unit of record.
class Light : public Object
{
public:
    Light();

    void color(const Color& color);
    Color color() const;

    // Luminous intensity in candela (lumens / steradian).
    void intensityCandela(double intensity);
    double intensityCandela() const;

    // Legacy alias for intensityCandela so existing "$brightness" scenes load.
    void brightness(double brightness);
    double brightness() const;

    // Total emitted luminous flux Phi = I * m_emissionSolidAngle (lumens).
    double luminousFlux() const;

    virtual void emit(WorkQueue<Photon>::Block photonBlock, double photonFlux, RandomGenerator& generator) const;

protected:
    Color m_color;
    double m_intensityCandela = 0.0;
    // Solid angle (sr) the light emits into; subclasses set this from geometry.
    // Default 4*pi (isotropic full sphere).
    double m_emissionSolidAngle = 0.0;
};
