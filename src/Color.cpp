#include "Color.h"

#include <cmath>

namespace
{

constexpr float gamma = 2.2f;

}

Color::Color()
    : Color(0.0f)
{
}

Color::Color(float grey)
    : red(grey)
    , green(grey)
    , blue(grey)
{
}

Color::Color(float ired, float igreen, float iblue)
    : red(ired)
    , green(igreen)
    , blue(iblue)
{
}

Color Color::fromRGB(float red, float green, float blue)
{
    return {
        std::pow(red / 255.0f, gamma),
        std::pow(green / 255.0f, gamma),
        std::pow(blue / 255.0f, gamma)
    };
}

Color Color::operator+=(const Color& rhs)
{
    red += rhs.red;
    green += rhs.green;
    blue += rhs.blue;

    return *this;
}

Color operator*(const Color& lhs, float rhs)
{
    return {
        lhs.red * rhs,
        lhs.green * rhs,
        lhs.blue * rhs,
    };
}

Color operator*(float lhs, const Color& rhs)
{
    return {
        rhs.red * lhs,
        rhs.green * lhs,
        rhs.blue * lhs,
    };
}

Color operator/(const Color& lhs, float rhs)
{
    return {
        lhs.red / rhs,
        lhs.green / rhs,
        lhs.blue / rhs,
    };
}
