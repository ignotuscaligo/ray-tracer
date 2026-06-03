#include "Ray.h"

#include "Utility.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>

Ray::Ray(Vector iorigin, Vector idirection) noexcept
    : origin(iorigin)
    , direction(idirection)
{
}

bool rayIntersectsBounds(const Ray& ray, const Bounds& bounds) noexcept
{
    double tmin = 0.0;
    double tmax = std::numeric_limits<double>::max();

    for (int i = 0; i < 3; ++i)
    {
        const Axis axis = static_cast<Axis>(i);
        const Limits limits = bounds[axis];
        const double origin = ray.origin[axis];
        const double direction = ray.direction[axis];

        if (std::abs(direction) < std::numeric_limits<double>::epsilon())
        {
            if (origin < limits.min || origin > limits.max)
            {
                return false;
            }
        }
        else
        {
            const double ood = 1.0 / direction;
            double t1 = (limits.min - origin) * ood;
            double t2 = (limits.max - origin) * ood;

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

std::optional<Hit> rayIntersectsTriangle(const Ray& ray, const Triangle& triangle) noexcept
{
    Hit hit;

    const Vector ab = triangle.b - triangle.a;
    const Vector ac = triangle.c - triangle.a;
    const Vector qp = -ray.direction;

    const Vector n = Vector::cross(ab, ac);

    const double d = Vector::dot(qp, n);

    if (d <= 0.0)
    {
        return std::nullopt;
    }

    const Vector ap = ray.origin - triangle.a;
    const double t = Vector::dot(ap, n);

    if (t < 0.0)
    {
        return std::nullopt;
    }

    const Vector e = Vector::cross(qp,ap);
    double v = Vector::dot(ac, e);
    if (v < 0.0 || v > d)
    {
        return std::nullopt;
    }

    double w = -Vector::dot(ab, e);
    if (w < 0.0 || v + w > d)
    {
        return std::nullopt;
    }

    const double ood = 1.0 / d;
    v *= ood;
    w *= ood;
    const double u = 1.0 - v - w;

    const Vector coords{u, v, w};
    hit.position = triangle.getPosition(coords);
    hit.normal = triangle.getNormal(coords).normalize();
    hit.distance = (hit.position - ray.origin).magnitude();

    return hit;
}

std::optional<Hit> rayIntersectsPlane(const Ray& ray, const Plane& plane) noexcept
{
    const double dot = Vector::dot(plane.normal, ray.direction);

    if (std::abs(dot) <= std::numeric_limits<double>::epsilon())
    {
        return std::nullopt;
    }

    const double t = (plane.dot - Vector::dot(plane.normal, ray.origin)) / dot;

    if (t < 0.0)
    {
        return std::nullopt;
    }

    Hit hit;
    hit.position = ray.origin + (t * ray.direction);
    hit.normal = plane.normal;
    hit.distance = (hit.position - ray.origin).magnitude();

    return hit;
}

std::optional<Hit> rayIntersectsSphere(const Ray& ray, const Sphere& sphere) noexcept
{
    // Closed-form ray-sphere intersection. The ray direction is not assumed to
    // be unit length, so we solve the general quadratic
    //     a t^2 + b t + c = 0
    // where a = dir . dir, b = 2 (origin - center) . dir, c = |origin - center|^2 - r^2.
    // We pick the nearest root with t > epsilon, which correctly handles rays
    // that originate inside the sphere (one root negative, one positive) and
    // grazing rays (discriminant ~ 0).
    const Vector oc = ray.origin - sphere.center;

    const double a = Vector::dot(ray.direction, ray.direction);

    if (std::abs(a) <= std::numeric_limits<double>::epsilon())
    {
        return std::nullopt;
    }

    const double b = 2.0 * Vector::dot(oc, ray.direction);
    const double c = Vector::dot(oc, oc) - sphere.radius * sphere.radius;

    const double discriminant = b * b - 4.0 * a * c;

    if (discriminant < 0.0)
    {
        return std::nullopt;
    }

    const double sqrtDiscriminant = std::sqrt(discriminant);
    const double inverse = 1.0 / (2.0 * a);

    // Nearest root first.
    double t = (-b - sqrtDiscriminant) * inverse;

    constexpr double epsilon = 1e-6;

    if (t <= epsilon)
    {
        // Either the nearest intersection is behind the origin or we are inside
        // the sphere; fall back to the far root.
        t = (-b + sqrtDiscriminant) * inverse;

        if (t <= epsilon)
        {
            return std::nullopt;
        }
    }

    Hit hit;
    hit.position = ray.origin + (t * ray.direction);
    hit.normal = (hit.position - sphere.center).normalize();
    hit.distance = (hit.position - ray.origin).magnitude();

    return hit;
}
