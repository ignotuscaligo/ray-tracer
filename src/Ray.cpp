#include "Ray.h"
#include "Utility.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>

Ray::Ray(Point iorigin, Point idirection)
    : origin(iorigin)
    , direction(idirection)
{
}

bool rayIntersectsBounds(const Ray& ray, const Bounds& bounds)
{
    float tmin = 0.0f;
    float tmax = std::numeric_limits<float>::max();

    for (int i = 0; i < 3; ++i)
    {
        const Axis axis = static_cast<Axis>(i);
        const Limits limits = bounds[axis];
        const float origin = ray.origin[axis];
        const float direction = ray.direction[axis];

        if (std::abs(direction) < std::numeric_limits<float>::epsilon())
        {
            if (origin < limits.min || origin > limits.max)
            {
                return false;
            }
        }
        else
        {
            float ood = 1.0f / direction;
            float t1 = (limits.min - origin) * ood;
            float t2 = (limits.max - origin) * ood;

            if (t1 > t2)
            {
                std::swap(t1, t2);
            }

            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);

            if (tmin > tmax)
            {
                return false;
            }
        }
    }

    return true;
}

bool rayIntersectsTriangle(const Ray& ray, const Triangle& triangle)
{
    Point ab = triangle.b - triangle.a;
    Point ac = triangle.c - triangle.a;
    Point qp = -ray.direction;

    Point n = cross(ab, ac);

    float d = dot(qp, n);

    if (d <= 0.0f)
    {
        return false;
    }

    Point ap = ray.origin - triangle.a;
    float t = dot(ap, n);

    if (t < 0.0f)
    {
        return false;
    }

    Point e = cross(qp,ap);
    float v = dot(ac, e);
    if (v < 0.0f || v > d)
    {
        return false;
    }

    float w = -dot(ab, e);
    if (w < 0.0f || v + w > d)
    {
        return false;
    }

    return true;
}
