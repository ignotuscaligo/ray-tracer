#include "DielectricMaterial.h"

#include <algorithm>
#include <cmath>

DielectricMaterial::DielectricMaterial()
    : Material()
    , m_ior(1.5)
    , m_tint({1, 1, 1})
{
}

DielectricMaterial::DielectricMaterial(const std::string& name, double ior, const Color& tint)
    : Material(name)
    , m_ior(ior)
    , m_tint(tint)
{
}

double DielectricMaterial::schlickReflectance(double cosTheta, double n1, double n2)
{
    // R0 = ((n1 - n2)/(n1 + n2))^2, then R = R0 + (1 - R0)(1 - cos)^5.
    double r0 = (n1 - n2) / (n1 + n2);
    r0 = r0 * r0;
    const double m = std::clamp(1.0 - cosTheta, 0.0, 1.0);
    const double m5 = m * m * m * m * m;
    return r0 + (1.0 - r0) * m5;
}

DielectricMaterial::Interaction DielectricMaterial::resolve(const Vector& incident,
                                                            const Vector& normal,
                                                            double ior)
{
    Interaction out;

    const Vector dir = Vector::normalized(incident);
    Vector n = Vector::normalized(normal);

    // Entering vs exiting from the sign of dot(rayDir, outwardNormal).
    //   dot < 0 : the ray opposes the outward normal -> hitting the front face
    //             from outside -> ENTERING (air -> glass).
    //   dot > 0 : the ray travels with the outward normal -> leaving the solid
    //             from inside -> EXITING (glass -> air); flip the normal so it
    //             faces back against the incident ray.
    double cosI = Vector::dot(dir, n);
    double n1;
    double n2;
    if (cosI < 0.0)
    {
        // Entering. cosI is negative; the angle-of-incidence cosine is -cosI.
        out.entering = true;
        n1 = 1.0;
        n2 = ior;
        cosI = -cosI;  // now positive, = cos(theta_i) against the oriented normal n
    }
    else
    {
        // Exiting. Flip the normal to the incident side.
        out.entering = false;
        n = -n;
        n1 = ior;
        n2 = 1.0;
        // cosI was dot(dir, outwardNormal) > 0; against the flipped normal it is
        // the same magnitude.
        // (cosI already positive and equal to cos(theta_i) on the flipped side.)
    }

    out.orientedNormal = n;

    // Reflection direction: mirror about the oriented normal (same formula
    // regardless of side). r = d - 2 (d . n) n, where (d . n) = -cosI here.
    out.reflectDir = Vector::normalized(Vector::reflected(dir, n));

    // Snell refraction. eta = n1/n2. With I the incident unit vector and N the
    // normal on the incident side (n), the transmitted direction is
    //   T = eta * I + (eta * cosI - cosT) * N
    // where cosI = -(I . N) = the positive cosine we computed, and
    //   cosT = sqrt(1 - eta^2 (1 - cosI^2)).
    const double eta = n1 / n2;
    const double sinT2 = eta * eta * (1.0 - cosI * cosI);

    if (sinT2 > 1.0)
    {
        // Total internal reflection: no transmitted ray, everything reflects.
        out.totalInternalReflection = true;
        out.reflectance = 1.0;
        out.refractDir = out.reflectDir;  // sensible fallback
        return out;
    }

    const double cosT = std::sqrt(std::max(0.0, 1.0 - sinT2));
    out.refractDir = Vector::normalized(dir * eta + n * (eta * cosI - cosT));

    // Fresnel reflectance. Use the cosine on the side with the larger angle to
    // keep Schlick well-behaved: when going into a denser medium use cosI, when
    // exiting into a less dense medium use cosT (the transmission-side cosine).
    const double cosForFresnel = (n1 <= n2) ? cosI : cosT;
    out.reflectance = std::clamp(schlickReflectance(cosForFresnel, n1, n2), 0.0, 1.0);

    return out;
}

BSDFSample DielectricMaterial::sample(const Vector& incident,
                                      const UnitVector& normal,
                                      RandomGenerator& generator) const
{
    const Interaction it = resolve(incident, normal, m_ior);

    BSDFSample s;
    s.isDelta = true;
    s.pdf = 1.0;

    // Stochastic Russian-roulette pick between the two delta lobes by Fresnel R.
    // Picking by probability R (reflect) / 1-R (refract) and returning weight = 1
    // makes the expected throughput equal to R*reflect + (1-R)*refract, i.e.
    // energy-conserving with a single continued ray. A tint multiplies throughput
    // (1,1,1 = clear).
    const double xi = generator.value();
    if (it.totalInternalReflection || xi < it.reflectance)
    {
        s.direction = it.reflectDir;
    }
    else
    {
        s.direction = it.refractDir;
    }

    s.weight = m_tint;
    // Validity: the direction must be a real unit vector. Reflection always is;
    // refraction is only produced when !TIR. A refracted ray legitimately points
    // to the OTHER side of the surface (dot with oriented normal < 0), so unlike
    // the mirror we do NOT require same-hemisphere agreement with the normal.
    const double mag2 = s.direction.magnitudeSquared();
    s.valid = (mag2 > 0.5);  // normalized vectors are ~1; guards against a zero dir
    return s;
}

Color DielectricMaterial::evaluate(const Vector& /*wi*/, const Vector& /*wo*/, const UnitVector& /*normal*/) const
{
    return Color{0.0f, 0.0f, 0.0f};
}

double DielectricMaterial::pdf(const Vector& /*wi*/, const Vector& /*wo*/, const UnitVector& /*normal*/) const
{
    return 0.0;
}
