#pragma once

#include "Triangle.h"
#include "Vector.h"

struct Hit
{
    Triangle triangle;
    Vector incident; // direction from incoming ray
    Vector coords; // barycentric position in the triangle
    Vector position; // hit position
    Vector normal; // hit normal
};
