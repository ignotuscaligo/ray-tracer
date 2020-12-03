#pragma once

struct Color
{
    float red;
    float green;
    float blue;

    Color() = default;
    Color(float grey);
    Color(float ired, float igreen, float iblue);
};

Color operator*(const Color& lhs, float rhs);
Color operator*(float lhs, const Color& rhs);
Color operator/(const Color& lhs, float rhs);
