#include "AreaLight.h"

#include "Utility.h"

#include <cmath>

AreaLight::AreaLight()
    : Light()
{
    registerType<AreaLight>();
    // Lambertian emitter into a hemisphere: the projected (cosine-weighted)
    // solid angle is pi, so Phi = I * pi (see AreaLight.h). The "$luminousFlux"
    // override path bypasses this in luminousFlux().
    m_emissionSolidAngle = Utility::pi;
}

void AreaLight::shape(Shape shape)
{
    m_shape = shape;
}

AreaLight::Shape AreaLight::shape() const
{
    return m_shape;
}

void AreaLight::width(double width)
{
    m_width = width;
}

double AreaLight::width() const
{
    return m_width;
}

void AreaLight::height(double height)
{
    m_height = height;
}

double AreaLight::height() const
{
    return m_height;
}

void AreaLight::radius(double radius)
{
    m_radius = radius;
}

double AreaLight::radius() const
{
    return m_radius;
}

void AreaLight::luminousFluxOverride(double flux)
{
    m_luminousFluxOverride = flux;
}

double AreaLight::luminousFluxOverride() const
{
    return m_luminousFluxOverride;
}

double AreaLight::luminousFlux() const
{
    // A direct-flux override makes this light energy-comparable to any other
    // light reporting the same Phi (used for the energy-conservation check
    // against an equal-power OmniLight). Otherwise fall back to I * pi.
    if (m_luminousFluxOverride > 0.0)
    {
        return m_luminousFluxOverride;
    }

    return Light::luminousFlux();
}

void AreaLight::emit(WorkQueue<Photon>::Block photonBlock, double photonFlux, RandomGenerator& generator) const
{
    Color photonColor = m_color * static_cast<float>(photonFlux);

    // In-plane basis + surface normal from the light's orientation. right/up
    // span the emitter surface; normal is the Lambertian emission axis.
    const Quaternion orientation = rotation();
    const Vector right = orientation * Vector::unitX;
    const Vector up = orientation * Vector::unitY;
    const Vector normal = orientation * Vector::unitZ; // == forward()
    const Vector center = position();

    for (auto& photon : photonBlock)
    {
        // 1) Sample an ORIGIN uniformly across the light surface.
        Vector origin;
        if (m_shape == Shape::Disc)
        {
            // Uniform-by-area disc sample: r = R*sqrt(xi) gives uniform density
            // (sampling r linearly would bunch points at the center).
            const double theta = generator.value(Utility::pi2);
            const double r = m_radius * std::sqrt(generator.value(1.0));
            origin = center + (right * (r * std::cos(theta))) + (up * (r * std::sin(theta)));
        }
        else
        {
            // Uniform over the rectangle: independent uniform offsets in u,v
            // centered on the light position.
            const double u = (generator.value(1.0) - 0.5) * m_width;
            const double v = (generator.value(1.0) - 0.5) * m_height;
            origin = center + (right * u) + (up * v);
        }

        // 2) Sample a DIRECTION cosine-weighted about the normal (Malley's
        // method: a uniform-disc sample lifted onto the hemisphere). This is the
        // correct distribution for a Lambertian emitter.
        const double diskTheta = generator.value(Utility::pi2);
        const double diskR = std::sqrt(generator.value(1.0));
        const double dx = diskR * std::cos(diskTheta);
        const double dy = diskR * std::sin(diskTheta);
        const double dz = std::sqrt(std::max(0.0, 1.0 - (dx * dx) - (dy * dy)));

        Vector direction = (right * dx) + (up * dy) + (normal * dz);
        direction.normalize();

        photon.ray = {origin, direction};
        photon.color = photonColor;
        photon.bounces = 0;
    }
}
