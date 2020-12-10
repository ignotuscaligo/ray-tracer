#pragma once

#include "Color.h"
#include "Object.h"
#include "Photon.h"
#include "WorkQueue.h"

class Light : public Object
{
public:
    Light();

    void color(const Color& color);
    Color color() const;

    void brightness(float brightness);
    float brightness() const;

    virtual void emit(WorkQueue<Photon>::Block photonBlock) const;

protected:
    Color m_color;
    float m_brightness = 0;
};
