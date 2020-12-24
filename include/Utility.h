#pragma once

#include "Vector.h"
#include "Triangle.h"

namespace Utility
{

extern const double pi;

void printVector(const Vector& vector);
void printTriangle(const Triangle& triangle);

double radians(double degrees);
double degrees(double radians);

}
