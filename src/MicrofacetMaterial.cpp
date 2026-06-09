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

// Sample the GGX distribution of VISIBLE normals (Heitz 2018, "Sampling the GGX
// Distribution of Visible Normals", JCGT 7(4)). `Ve` is the VIEW direction (wi)
// expressed in the local frame (+Z = surface normal), pointing away from the
// surface (Ve.z > 0). Returns the sampled microfacet normal (half-vector) in the
// same local frame. Isotropic roughness, so alphaX == alphaY == alpha.
//
// Why VNDF instead of plain NDF (Walter 2007) sampling: the NDF sampler's
// reflection throughput f*cos/pdf = F*G2*(wi.wh)/(cos_i*cos_h) is NOT bounded by 1
// — at grazing incidence with a half-vector tilted toward wi it can exceed 1,
// giving a per-bounce ENERGY GAIN (fireflies) and violating the renderer's
// monotonic-decay termination premise (DESIGN §2 / §2b). Sampling from the
// distribution of *visible* normals cancels the (wi.wh)/cos_i geometry in the
// estimator, so the weight collapses to F*G2/G1(wi) = F*G1(wo) <= 1 by
// construction. Heitz 2018 §3.2 (the "bounded, lower-variance" sampler).
Vector ggxSampleVisibleNormalLocal(const Vector& Ve, double alpha, double u1, double u2)
{
    // 1. Stretch the view direction to the hemisphere configuration (alpha -> 1).
    Vector Vh = Vector::normalized(Vector{alpha * Ve.x, alpha * Ve.y, Ve.z});

    // 2. Orthonormal basis (Heitz's tangent frame around Vh). Special-case the
    //    grazing/degenerate case where Vh is along +Z.
    const double lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    Vector T1 = (lensq > 0.0)
                    ? Vector{-Vh.y, Vh.x, 0.0} * (1.0 / std::sqrt(lensq))
                    : Vector{1.0, 0.0, 0.0};
    Vector T2 = Vector::cross(Vh, T1);

    // 3. Uniformly sample a point on the projected disk, then warp the lower half
    //    to account for the projected hemisphere (Heitz eq. for the disk remap).
    const double r = std::sqrt(u1);
    const double phi = Utility::pi2 * u2;
    const double t1 = r * std::cos(phi);
    double t2 = r * std::sin(phi);
    const double s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * std::sqrt(std::max(0.0, 1.0 - t1 * t1)) + s * t2;

    // 4. Reproject onto the hemisphere around Vh.
    const double t3 = std::sqrt(std::max(0.0, 1.0 - t1 * t1 - t2 * t2));
    Vector Nh = T1 * t1 + T2 * t2 + Vh * t3;

    // 5. Unstretch back to the ellipsoid configuration -> the microfacet normal.
    Vector Ne = Vector::normalized(
        Vector{alpha * Nh.x, alpha * Nh.y, std::max(0.0, Nh.z)});
    return Ne;
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

BSDFSample MicrofacetMaterial::sample(const Vector& incident, const UnitVector& normal, RandomGenerator& generator) const
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

    // Express wi (the view direction) in the local frame (+Z = surface normal) for
    // VNDF sampling. cosThetaI > 0 (checked above) so Ve.z > 0.
    const Vector Ve{Vector::dot(wi, tangent), Vector::dot(wi, bitangent), cosThetaI};

    const double u1 = generator.value();
    const double u2 = generator.value();

    // VNDF SAMPLING (Heitz 2018). Sample the microfacet normal from the
    // distribution of VISIBLE normals so the reflection throughput stays <= 1.
    const Vector hLocal = ggxSampleVisibleNormalLocal(Ve, m_alpha, u1, u2);

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

    // VNDF pdf of the sampled wo: p(wo) = D_vis(wh) / (4 |wi.wh|), where
    // D_vis(wh) = G1(wi) * max(0, wi.wh) * D(wh) / cos_i. The |wi.wh| cancels, so
    //   p(wo) = G1(wi) * D(wh) / (4 cos_i).
    const double D = ggxD(cosThetaH, m_alpha);
    const double G1i = smithG1(cosThetaI, m_alpha);
    const double pdfWo = (G1i * D) / (4.0 * cosThetaI);

    // VNDF throughput weight. With f = F * D * G2 / (4 cos_i cos_o) and the VNDF
    // pdf above, the throughput f*cos_o/pdf SIMPLIFIES to:
    //   weight = F * G2 / G1(wi) = F * G1(wo)   (separable Smith: G2 = G1i*G1o)
    // which is <= 1 by construction (G1(wo) in [0,1], Fresnel F in [0,1]) — no
    // grazing-incidence energy gain, restoring the monotonic-decay invariant.
    const double G2 = smithG2(cosThetaI, cosThetaO, m_alpha);
    const Color F = schlickFresnel(m_albedo, wiDotWh);
    const double weightScalar = (G1i > 0.0) ? (G2 / G1i) : 0.0;

    s.direction = wo;
    s.weight = F * static_cast<float>(weightScalar);
    s.pdf = pdfWo;
    s.isDelta = false;
    s.valid = true;
    return s;
}

BSDFSample MicrofacetMaterial::sampleMode(const Vector& incident, const UnitVector& normal, RandomGenerator& /*generator*/) const
{
    // GGX D peaks at h = normal, which means the BRDF lobe peaks at
    // wo = reflect(wi, normal) — perfect-reflection direction. As alpha -> 0
    // this collapses to the pure mirror direction; for larger alpha the lobe
    // widens around it.
    const Vector wi = -incident;
    const double cosThetaI = Vector::dot(wi, normal);

    BSDFSample s;
    if (cosThetaI <= 0.0)
    {
        s.valid = false;
        return s;
    }

    const Vector wo = Vector::normalized(Vector::reflected(incident, normal));
    const double cosThetaO = Vector::dot(wo, normal);
    if (cosThetaO <= 0.0)
    {
        s.valid = false;
        return s;
    }

    // h = (wi + wo).normalized; for perfect reflection, h = normal.
    // F at peak uses cos(wi, h) = cos(wi, n) = cosThetaI.
    const Color F = schlickFresnel(m_albedo, cosThetaI);
    const double G2 = smithG2(cosThetaI, cosThetaO, m_alpha);

    // Throughput weight = f * cos(theta_o) / pdf
    // At the peak (h = n), this reduces to F * G2 * cos_i / (cos_i * cos_h)
    // with cos_h = 1, so weight = F * G2.
    const double weightScalar = G2;

    s.direction = wo;
    s.weight = F * static_cast<float>(weightScalar);
    s.pdf = ggxD(1.0, m_alpha) * 1.0 / (4.0 * cosThetaI);
    s.isDelta = false;
    s.valid = true;
    return s;
}

Color MicrofacetMaterial::evaluate(const Vector& wi, const Vector& wo, const UnitVector& normal) const
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
    // Rough microfacet (alpha=1) gets 9 daughters matching Lambertian's
    // hemisphere coverage; perfect-mirror limit (alpha~0) collapses to 1.
    // Lobe width is what determines how many directional samples are needed
    // to avoid clumpy artifacts — narrower lobe, fewer samples.
    const long n = std::lround(9.0 * m_alpha);
    return static_cast<size_t>(std::max<long>(1, n));
}

double MicrofacetMaterial::pdf(const Vector& wi, const Vector& wo, const UnitVector& normal) const
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

    // VNDF pdf (matches sample()): p(wo) = G1(wi) * D(wh) / (4 cos_i). This is the
    // density of the visible-normal sampler, so it is the correct query pdf for any
    // MIS that pairs with sample(). (The plain-NDF pdf D*cos_h/(4|wi.wh|) was the
    // old NDF sampler's density and no longer matches the live sampler.)
    const double D = ggxD(cosThetaH, m_alpha);
    const double G1i = smithG1(cosThetaI, m_alpha);
    return (G1i * D) / (4.0 * cosThetaI);
}
