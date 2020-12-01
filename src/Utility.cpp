#include "Utility.h"

#include <iostream>

#define _USE_MATH_DEFINES
#include <math.h>

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
    return M_PI * (degrees / 180.0f);
}

float degrees(float radians)
{
    return (radians * 180.0f) / M_PI;
}
