#pragma once

#include "Pyramid.h"
#include "Quaternion.h"
#include "Vector.h"

struct PixelSensor
{
    PixelSensor() = default;

    PixelSensor(Vector position, Quaternion rotation, float pitch, float yaw, float pitchStep, float yawStep)
        : pyramid(position, rotation, pitch, yaw, pitchStep, yawStep)
    {
    }

    bool containsPoint(const Vector& point) const
    {
        return pyramid.containsPoint(point);
    }

    int x;
    int y;

    Pyramid pyramid;
};
