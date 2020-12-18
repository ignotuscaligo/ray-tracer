#include "Utility.h"

#include <cstdlib>
#include <iostream>

#define _USE_MATH_DEFINES
#include <math.h>

namespace Utility
{

const float pi = M_PI;

void printVector(const Vector& point)
{
    std::cout << "(" << point.x << ", " << point.y << ", " << point.z << ")";
}

void printTriangle(const Triangle& triangle)
{
    std::cout << "<";
    printVector(triangle.a);
    std::cout << ", ";
    printVector(triangle.b);
    std::cout << ", ";
    printVector(triangle.c);
    std::cout << ">";
}

float radians(float degrees)
{
    return static_cast<float>(M_PI) * (degrees / 180.0f);
}

float degrees(float radians)
{
    return (radians * 180.0f) / static_cast<float>(M_PI);
}

}
