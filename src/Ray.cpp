#include "Ray.h"

#include "Utility.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>

Ray::Ray(Vector iorigin, Vector idirection)
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

std::optional<Hit> rayIntersectsTriangle(const Ray& ray, const Triangle& triangle)
{
    Hit hit;

    Vector ab = triangle.b - triangle.a;
    Vector ac = triangle.c - triangle.a;
    Vector qp = -ray.direction;

    Vector n = Vector::cross(ab, ac);

    float d = Vector::dot(qp, n);

    if (d <= 0.0f)
    {
        return std::nullopt;
    }

    Vector ap = ray.origin - triangle.a;
    float t = Vector::dot(ap, n);

    if (t < 0.0f)
    {
        return std::nullopt;
    }

    Vector e = Vector::cross(qp,ap);
    float v = Vector::dot(ac, e);
    if (v < 0.0f || v > d)
    {
        return std::nullopt;
    }

    float w = -Vector::dot(ab, e);
    if (w < 0.0f || v + w > d)
    {
        return std::nullopt;
    }

    float ood = 1.0f / d;
    t *= ood;
    v *= ood;
    w *= ood;
    float u = 1.0f - v - w;

    Vector coords{u, v, w};
    hit.position = triangle.getPosition(coords);
    hit.normal = triangle.getNormal(coords).normalize();
    hit.distance = (hit.position - ray.origin).magnitude();

    return hit;
}

std::optional<Hit> rayIntersectsPlane(const Ray& ray, const Plane& plane)
{
    float dot = Vector::dot(plane.normal, ray.direction);

    if (std::abs(dot) <= std::numeric_limits<float>::epsilon())
    {
        return std::nullopt;
    }

    float t = (plane.dot - Vector::dot(plane.normal, ray.origin)) / dot;

    if (t < 0.0f)
    {
        return std::nullopt;
    }

    Hit hit;
    hit.position = ray.origin + (t * ray.direction);
    hit.normal = plane.normal;
    hit.distance = (hit.position - ray.origin).magnitude();

    return hit;
}
