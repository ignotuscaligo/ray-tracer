#include "Utility.h"

#include <cstdlib>
#include <iostream>

#define _USE_MATH_DEFINES
#include <math.h>

namespace Utility
{

const double pi = M_PI;

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

}
