#pragma once

struct Color
{
    float red;
    float green;
    float blue;

    Color();
    Color(float grey);
    Color(float ired, float igreen, float iblue);

    float brightness() const;

    static const float gamma;
    static Color fromRGB(float red, float green, float blue);

    Color operator+=(const Color& rhs);
};

Color operator*(const Color& lhs, float rhs);
Color operator*(float lhs, const Color& rhs);
Color operator*(const Color& lhs, const Color& rhs);
Color operator/(const Color& lhs, float rhs);
