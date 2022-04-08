#pragma once

#include "AngleGenerator.h"
#include "Color.h"
#include "Material.h"
#include "Photon.h"
#include "RandomGenerator.h"
#include "Vector.h"
#include "WorkQueue.h"

class CauchyMaterial : public Material
{
public:
    static constexpr double kDefaultSigma = 0.2;

    CauchyMaterial();
    CauchyMaterial(const std::string& name, const Color& color = {1.0f, 1.0f, 1.0f}, double sigma = kDefaultSigma);

    Color colorForHit(const Vector& pixelDirection, const PhotonHit& photonHit) const override;
    void bounce(WorkQueue<Photon>::Block photonBlock, size_t startIndex, size_t endIndex, const PhotonHit& photonHit, RandomGenerator& generator) const override;

private:
    struct Parameters
    {
        double sigma;
        double mu;
        double integralMin;
        double integralMax;
        double integralRange;
    };

    // Given an angle between -pi/2 to pi/2, return the relative intensity
    static double intensityDistribution(double angle, const Parameters& parameters);

    // Expects angle and inputAngle to be between -pi/2 to pi/2
    static double cauchyIntegral(double angle, const Parameters& parameters);

    // Given a random number between 0 and 1, return an output angle between -pi/2 and pi/2
    // such that the distribution of angles matches a Cauchy distribution
    static double probabilityDistribution(double input, const Parameters& parameters);

    Color m_color;
    double m_sigma;
    AngleGenerator m_angleGenerator;
};
