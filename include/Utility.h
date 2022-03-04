#pragma once

#include "Vector.h"
#include "Triangle.h"

namespace Utility
{

extern const double halfPi;
extern const double pi;
extern const double pi2;
extern const double pi4;

void printVector(const Vector& vector);
void printTriangle(const Triangle& triangle);

double radians(double degrees);
double degrees(double radians);

}
