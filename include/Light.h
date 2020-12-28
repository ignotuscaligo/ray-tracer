#pragma once

#include "Color.h"
#include "Object.h"
#include "Photon.h"
#include "RandomGenerator.h"
#include "WorkQueue.h"

class Light : public Object
{
public:
    Light();

    void color(const Color& color);
    Color color() const;

    void brightness(double brightness);
    double brightness() const;

    virtual void emit(WorkQueue<Photon>::Block photonBlock, double photonBrightness, RandomGenerator& generator) const;

protected:
    virtual void updateParameters();

    Color m_color;
    double m_brightness = 0;
    double m_area = 0;
    double m_lumens = 0;
};
