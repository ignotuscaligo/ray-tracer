#include "MicrofacetMaterial.h"

#include "Utility.h"

#include <algorithm>
#include <cmath>

namespace
{

constexpr double kAlphaFloor = 1e-3;

void buildBasis(const Vector& normal, Vector& tangent, Vector& bitangent)
{
    if (normal.z < -0.9999999)
    {
        tangent = Vector{0.0, -1.0, 0.0};
        bitangent = Vector{-1.0, 0.0, 0.0};
        return;
    }
    const double a = 1.0 / (1.0 + normal.z);
    const double b = -normal.x * normal.y * a;
    tangent = Vector{1.0 - normal.x * normal.x * a, b, -normal.x};
    bitangent = Vector{b, 1.0 - normal.y * normal.y * a, -normal.y};
}

// GGX normal distribution D(h) in local frame where +Z = surface normal.
// D = alpha^2 / (pi * ((alpha^2 - 1) * cos_h^2 + 1)^2)
double ggxD(double cosThetaH, double alpha)
{
    if (cosThetaH <= 0.0)
    {
        return 0.0;
    }
    const double a2 = alpha * alpha;
    const double cos2 = cosThetaH * cosThetaH;
    const double denom = (a2 - 1.0) * cos2 + 1.0;
    return a2 / (Utility::pi * denom * denom);
}

// Smith Lambda for GGX (used to build G1).
double smithLambda(double cosTheta, double alpha)
{
    if (cosTheta >= 1.0 || cosTheta <= 0.0)
    {
        return 0.0;
    }
    const double cos2 = cosTheta * cosTheta;
    const double sin2 = std::max(0.0, 1.0 - cos2);
    const double tan2 = sin2 / cos2;
    return 0.5 * (std::sqrt(1.0 + alpha * alpha * tan2) - 1.0);
}

double smithG1(double cosTheta, double alpha)
{
    return 1.0 / (1.0 + smithLambda(cosTheta, alpha));
}

// Smith separable masking-shadowing (the "uncorrelated" variant; PBRT 4e §9.6.4 / Walter
// 2007 §3.2). The height-correlated form would be 1/(1+Lambda(wi)+Lambda(wo)) — slightly
// more accurate but the separable form is sufficient at this level of detail.
double smithG2(double cosThetaI, double cosThetaO, double alpha)
{
    return smithG1(cosThetaI, alpha) * smithG1(cosThetaO, alpha);
}

// Schlick's Fresnel approximation, per-channel.
Color schlickFresnel(const Color& F0, double cosTheta)
{
    const double t = std::max(0.0, 1.0 - cosTheta);
    const double t5 = t * t * t * t * t;
    return Color{
        static_cast<float>(F0.red   + (1.0 - F0.red)   * t5),
        static_cast<float>(F0.green + (1.0 - F0.green) * t5),
        static_cast<float>(F0.blue  + (1.0 - F0.blue)  * t5)
    };
}

// Sample a half-vector from GGX's D distribution in local space (Walter 2007 eq. 35-36).
Vector ggxSampleHalfLocal(double u1, double u2, double alpha)
{
    const double phi = Utility::pi2 * u1;
    const double cosTheta2 = (1.0 - u2) / (1.0 + (alpha * alpha - 1.0) * u2);
    const double cosTheta = std::sqrt(std::max(0.0, cosTheta2));
    const double sinTheta = std::sqrt(std::max(0.0, 1.0 - cosTheta2));
    return Vector{sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta};
}

}

MicrofacetMaterial::MicrofacetMaterial()
    : Material()
    , m_albedo({1, 1, 1})
    , m_alpha(kDefaultRoughness)
{
}

MicrofacetMaterial::MicrofacetMaterial(const std::string& name, const Color& albedo, double roughness)
    : Material(name)
    , m_albedo(albedo)
    , m_alpha(std::max(kAlphaFloor, roughness))
{
}

BSDFSample MicrofacetMaterial::sample(const Vector& incident, const Vector& normal, RandomGenerator& generator) const
{
    // wi is the direction the photon came FROM, in standard BRDF convention.
    const Vector wi = -incident;
    const double cosThetaI = Vector::dot(wi, normal);

    BSDFSample s;

    if (cosThetaI <= 0.0)
    {
        s.valid = false;
        return s;
    }

    Vector tangent, bitangent;
    buildBasis(normal, tangent, bitangent);

    const double u1 = generator.value();
    const double u2 = generator.value();
    const Vector hLocal = ggxSampleHalfLocal(u1, u2, m_alpha);

    // Half-vector in world space.
    Vector wh = tangent * hLocal.x + bitangent * hLocal.y + normal * hLocal.z;
    wh = Vector::normalized(wh);

    // wo = reflect wi about wh.
    const double wiDotWh = Vector::dot(wi, wh);
    if (wiDotWh <= 0.0)
    {
        s.valid = false;
        return s;
    }
    Vector wo = wh * (2.0 * wiDotWh) - wi;
    wo = Vector::normalized(wo);

    const double cosThetaO = Vector::dot(wo, normal);
    const double cosThetaH = Vector::dot(wh, normal);

    if (cosThetaO <= 0.0 || cosThetaH <= 0.0)
    {
        s.valid = false;
        return s;
    }

    // PDF for h sampled from D: p_h = D * cos(theta_h). Jacobian of the wh -> wo
    // reflection map gives 1/(4 |wi . wh|), so p_wo = D * cos(theta_h) / (4 |wi . wh|).
    const double D = ggxD(cosThetaH, m_alpha);
    const double pdfWo = D * cosThetaH / (4.0 * wiDotWh);

    // BRDF f = F * D * G2 / (4 cos_i cos_o)
    const double G2 = smithG2(cosThetaI, cosThetaO, m_alpha);
    const Color F = schlickFresnel(m_albedo, wiDotWh);

    // Throughput weight = f * cos(theta_o) / pdf
    //                   = F * G2 * |wi . wh| / (cos_i * cos_h)
    const double weightScalar = (G2 * wiDotWh) / (cosThetaI * cosThetaH);

    s.direction = wo;
    s.weight = F * static_cast<float>(weightScalar);
    s.pdf = pdfWo;
    s.isDelta = false;
    s.valid = true;
    return s;
}

Color MicrofacetMaterial::evaluate(const Vector& wi, const Vector& wo, const Vector& normal) const
{
    const double cosThetaI = Vector::dot(wi, normal);
    const double cosThetaO = Vector::dot(wo, normal);

    if (cosThetaI <= 0.0 || cosThetaO <= 0.0)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }

    Vector wh = wi + wo;
    if (wh.magnitudeSquared() < 1e-20)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }
    wh = Vector::normalized(wh);
    const double cosThetaH = Vector::dot(wh, normal);
    if (cosThetaH <= 0.0)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }

    const double D = ggxD(cosThetaH, m_alpha);
    const double G2 = smithG2(cosThetaI, cosThetaO, m_alpha);
    const Color F = schlickFresnel(m_albedo, std::max(0.0, Vector::dot(wi, wh)));

    const double scalar = (D * G2) / (4.0 * cosThetaI * cosThetaO);
    return F * static_cast<float>(scalar);
}

size_t MicrofacetMaterial::daughterPhotonCount() const
{
    // Rough microfacet (alpha=1) gets 32 daughters; perfect-mirror limit (alpha~0)
    // collapses to 1. The lobe width is what determines how many directional samples
    // are needed to avoid clumpy artifacts — narrower lobe, fewer samples.
    const long n = std::lround(32.0 * m_alpha);
    return static_cast<size_t>(std::max<long>(1, n));
}

double MicrofacetMaterial::pdf(const Vector& wi, const Vector& wo, const Vector& normal) const
{
    const double cosThetaI = Vector::dot(wi, normal);
    const double cosThetaO = Vector::dot(wo, normal);

    if (cosThetaI <= 0.0 || cosThetaO <= 0.0)
    {
        return 0.0;
    }

    Vector wh = wi + wo;
    if (wh.magnitudeSquared() < 1e-20)
    {
        return 0.0;
    }
    wh = Vector::normalized(wh);
    const double cosThetaH = Vector::dot(wh, normal);
    if (cosThetaH <= 0.0)
    {
        return 0.0;
    }

    const double wiDotWh = Vector::dot(wi, wh);
    if (wiDotWh <= 0.0)
    {
        return 0.0;
    }

    const double D = ggxD(cosThetaH, m_alpha);
    return D * cosThetaH / (4.0 * wiDotWh);
}
