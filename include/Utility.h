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

// Firefly fix: apply the minimum-radius floor to a splat footprint radius.
// Returns max(rawRadius, minRadius) when minRadius > 0, else rawRadius. The
// camera splat normalizes each photon by pi * r^2; flooring r caps the weight
// 1/(pi r^2) so a photon landing close to the camera (tiny rawRadius) cannot
// spike a single pixel to white. minRadius <= 0 disables the floor.
double flooredSplatRadius(double rawRadius, double minRadius);

// The per-pixel footprint weight 1/(pi r^2) used to normalize a splat, with the
// minimum-radius floor applied to r first. Returns 0 for a non-positive floored
// radius (degenerate). This is the term whose unbounded growth at small r is the
// firefly; with minRadius > 0 it is bounded above by 1/(pi minRadius^2).
double splatFootprintWeight(double rawRadius, double minRadius);

}
