#include "CauchyMaterial.h"

#include "Utility.h"

CauchyMaterial::CauchyMaterial()
    : Material()
    , m_color({1, 1, 1})
    , m_sigma(0.0)
    , m_angleGenerator({Utility::halfPi, 1.0})
{
}

CauchyMaterial::CauchyMaterial(const std::string& name, const Color& color, double sigma)
    : Material(name)
    , m_color(color)
    , m_sigma(sigma)
    , m_angleGenerator({Utility::halfPi, 1.0})
{
}

Color CauchyMaterial::colorForHit(const Vector& pixelDirection, const PhotonHit& photonHit) const
{
    Vector reflection = Vector::reflected(photonHit.photon.ray.direction, photonHit.hit.normal);
    double reflectionDot = Vector::dot(-pixelDirection, reflection);
    double brightess = std::max(0.0, ((reflectionDot + 1.0) / 2.0));
    return m_color * photonHit.photon.color * brightess;
}

void CauchyMaterial::bounce(WorkQueue<Photon>::Block photonBlock, size_t startIndex, size_t endIndex, const PhotonHit& photonHit, RandomGenerator& generator) const
{
    size_t count = endIndex - startIndex;
    if (count == 0)
    {
        return;
    }

    // Incoming ray
    const Vector& incident = photonHit.photon.ray.direction;

    // Surface normal
    const Vector& normal = photonHit.hit.normal;

    // Vector along surface of material, orthogonal to normal and incident
    const Vector planarLeft = Vector::cross(normal, incident).normalize();

    // Vector along surface of material, orthogonal to normal and in the direction of the incident angle
    const Vector planarForward = Vector::cross(planarLeft, planarForward).normalize();

    // Distribution in direction of ray, changes based on incident angle
    Parameters linealParameters;
    linealParameters.sigma = m_sigma;
    linealParameters.mu = Vector::angleBetween(-incident, normal) / Utility::halfPi;
    linealParameters.integralMin = cauchyIntegral(-Utility::halfPi, linealParameters);
    linealParameters.integralMax = cauchyIntegral(Utility::halfPi, linealParameters);
    linealParameters.integralRange = linealParameters.integralMax - linealParameters.integralMin;

    // Distribution away from ray direction, fixed
    // Highest distribution should be along direction of ray
    Parameters lateralParameters;
    lateralParameters.sigma = m_sigma;
    lateralParameters.mu = 0.0;
    lateralParameters.integralMin = cauchyIntegral(-Utility::halfPi, lateralParameters);
    lateralParameters.integralMax = cauchyIntegral(Utility::halfPi, lateralParameters);
    lateralParameters.integralRange = lateralParameters.integralMax - lateralParameters.integralMin;

    double brightness = 1.0 / static_cast<double>(count);

    for (size_t i = startIndex; i < endIndex; ++i)
    {
        const double linealRandom = m_angleGenerator.generate(generator);
        const double lateralRandom = m_angleGenerator.generate(generator);

        const double linealAngle = probabilityDistribution(linealRandom, linealParameters);
        const double lateralAngle = probabilityDistribution(lateralRandom, lateralParameters);

        const double linealNormalFactor = std::cos(linealAngle);
        const double linealForwardFactor = std::sin(linealAngle);
        const double lateralNormalFactor = std::cos(lateralAngle);
        const double lateralLeftFactor = std::sin(lateralAngle);

        const Vector reflectedForward = planarForward * (linealForwardFactor * lateralNormalFactor);
        const Vector reflectedNormal = normal *  (linealNormalFactor * lateralNormalFactor);
        const Vector reflectedLeft = planarLeft * (lateralLeftFactor * linealNormalFactor);

        const Vector reflected = (reflectedForward + reflectedNormal + reflectedLeft).normalize();

        photonBlock[i].ray = {photonHit.hit.position, reflected};
        photonBlock[i].color = m_color * photonHit.photon.color * brightness;
        photonBlock[i].bounces = photonHit.photon.bounces + 1;
    }
}

double CauchyMaterial::intensityDistribution(double angle, const Parameters& parameters)
{
    return 0.0;
}

double CauchyMaterial::cauchyIntegral(double angle, const Parameters& parameters)
{
    const double x = angle / Utility::halfPi;
    return std::atan2((x - parameters.mu), parameters.sigma) / Utility::pi;
}

double CauchyMaterial::probabilityDistribution(double input, const Parameters& parameters)
{
    const double mappedInput = (input * parameters.integralRange) + parameters.integralMin;
    const double inverseIntegral = parameters.sigma * std::tan(mappedInput * Utility::pi) + parameters.mu;
    return inverseIntegral * Utility::halfPi;
}
