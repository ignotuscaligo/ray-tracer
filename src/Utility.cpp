#include "Utility.h"

#include <cstdlib>
#include <iostream>

#define _USE_MATH_DEFINES
#include <math.h>

namespace Utility
{

const double halfPi = M_PI / 2.0;
const double pi = M_PI;
const double pi2 = M_PI * 2.0;
const double pi4 = M_PI * 4.0;

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

double radians(double degrees)
{
    return pi * (degrees / 180.0);
}

double degrees(double radians)
{
    return (radians * 180.0) / pi;
}

double flooredSplatRadius(double rawRadius, double minRadius)
{
    if (minRadius > 0.0 && rawRadius < minRadius)
    {
        return minRadius;
    }
    return rawRadius;
}

double splatFootprintWeight(double rawRadius, double minRadius)
{
    const double r = flooredSplatRadius(rawRadius, minRadius);
    if (r <= 0.0)
    {
        return 0.0;
    }
    return 1.0 / (pi * r * r);
}

}
