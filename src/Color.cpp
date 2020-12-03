#include "Color.h"

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
