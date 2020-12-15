#pragma once

#include "Vector.h"
#include "Triangle.h"

namespace Utility
{

void printVector(const Vector& vector);
void printTriangle(const Triangle& triangle);

float radians(float degrees);
float degrees(float radians);

float random(float value = 1.0f);

}
