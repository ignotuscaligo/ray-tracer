#include "Utility.h"

#include <iostream>

#define _USE_MATH_DEFINES
#include <math.h>

void printPoint(const Point& point)
{
    std::cout << "(" << point.x << ", " << point.y << ", " << point.z << ")";
}

void printTriangle(const Triangle& triangle)
{
    std::cout << "<";
    printPoint(triangle.a);
    std::cout << ", ";
    printPoint(triangle.b);
    std::cout << ", ";
    printPoint(triangle.c);
    std::cout << ">";
}

float radians(float degrees)
{
    return M_PI * (degrees / 180.0f);
}

float degrees(float radians)
{
    return (radians * 180.0f) / M_PI;
}
