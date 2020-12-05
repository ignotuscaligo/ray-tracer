#include "Light.h"

Light::Light()
    : Object()
{
    registerType<Light>();
}

void Light::color(const Color& color)
{
    m_color = color;
}

Color Light::color() const
{
    return m_color;
}

void Light::brightness(float brightness)
{
    m_brightness = brightness;
}

float Light::brightness() const
{
    return m_brightness;
}

void Light::emit(WorkQueue<Photon>::Block photonBlock) const
{
}
