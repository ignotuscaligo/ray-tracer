#pragma once

#include "Vector.h"
#include "Triangle.h"

namespace Utility
{

extern const float pi;

void printVector(const Vector& vector);
void printTriangle(const Triangle& triangle);

float radians(float degrees);
float degrees(float radians);

}
