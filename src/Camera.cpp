#include "Camera.h"

#include "AnimationQuery.h"
#include "Pyramid.h"
#include "RandomGenerator.h"
#include "Utility.h"

#include <cmath>
#include <stdexcept>

Camera::Camera()
    : Object()
    , m_width(0)
    , m_height(0)
    , m_aspectRatio(0)
    , m_verticalFieldOfView(0)
    , m_horizontalFieldOfView(0)
{
    registerType<Camera>();
}

Camera::Camera(size_t width, size_t height, double verticalFieldOfView)
    : Object()
    , m_width(width)
    , m_height(height)
    , m_aspectRatio(static_cast<double>(m_width) / static_cast<double>(m_height))
    , m_verticalFieldOfView(verticalFieldOfView)
    , m_horizontalFieldOfView(m_verticalFieldOfView * m_aspectRatio)
{
    registerType<Camera>();
}

size_t Camera::width() const
{
    return m_width;
}

size_t Camera::height() const
{
    return m_height;
}

double Camera::aspectRatio() const
{
    return m_aspectRatio;
}

double Camera::horizontalFieldOfView() const
{
    return m_horizontalFieldOfView;
}

void Camera::verticalFieldOfView(double verticalFieldOfView)
{
    m_verticalFieldOfView = verticalFieldOfView;
}

double Camera::verticalFieldOfView() const
{
    return m_verticalFieldOfView;
}

void Camera::fNumber(double fNumber)
{
    m_fNumber = fNumber;
}

double Camera::fNumber() const
{
    return m_fNumber;
}

void Camera::shutterTime(double seconds)
{
    m_shutterTime = seconds;
}

double Camera::shutterTime() const
{
    return m_shutterTime;
}

void Camera::iso(double iso)
{
    m_iso = iso;
}

double Camera::iso() const
{
    return m_iso;
}

void Camera::projection(Projection type)
{
    m_projection = type;
}

Camera::Projection Camera::projection() const
{
    return m_projection;
}

void Camera::orthographicHeight(double height)
{
    m_orthographicHeight = height;
}

double Camera::orthographicHeight() const
{
    return m_orthographicHeight;
}

void Camera::apertureRadius(double radius)
{
    m_apertureRadius = radius;
}

double Camera::apertureRadius() const
{
    return m_apertureRadius;
}

void Camera::focusDistance(double distance)
{
    m_focusDistance = distance;
}

double Camera::focusDistance() const
{
    return m_focusDistance;
}

void Camera::focalLength(double length)
{
    m_focalLength = length;
}

double Camera::focalLength() const
{
    return m_focalLength;
}

double Camera::effectiveApertureRadius() const
{
    if (m_apertureRadius > 0.0)
    {
        return m_apertureRadius;
    }
    // Physical relation: aperture diameter = focal length / f-number, so
    // radius = focalLength / (2 * N). Guard a zero/negative f-number.
    if (m_fNumber <= 0.0)
    {
        return 0.0;
    }
    return m_focalLength / (2.0 * m_fNumber);
}

double Camera::saturationLuminance() const
{
    // L_max = (N^2 * K) / (t * S). Guard against degenerate (zero) controls.
    const double denom = m_shutterTime * m_iso;
    if (denom <= 0.0)
    {
        return 0.0;
    }
    return (m_fNumber * m_fNumber * kMeterCalibration) / denom;
}

void Camera::setFromRenderConfiguration(size_t width, size_t height)
{
    if (width == 0 || height == 0)
    {
        throw std::runtime_error("Cannot configure Camera with 0 width or 0 height");
    }

    m_width = width;
    m_height = height;
    m_aspectRatio = static_cast<double>(m_width) / static_cast<double>(m_height);
    m_horizontalFieldOfView = m_verticalFieldOfView * m_aspectRatio;
}

void Camera::outputName(const std::string& name)
{
    m_outputName = name;
}

const std::string& Camera::outputName() const
{
    return m_outputName;
}

void Camera::setResolution(size_t width, size_t height)
{
    setFromRenderConfiguration(width, height);
    m_hasResolutionOverride = true;
}

bool Camera::hasResolutionOverride() const
{
    return m_hasResolutionOverride;
}

void Camera::bounceFilter(int bounce)
{
    m_bounceFilter = bounce;
}

int Camera::bounceFilter() const
{
    return m_bounceFilter;
}

void Camera::lightFilter(int lightIndex)
{
    m_lightFilter = lightIndex;
}

int Camera::lightFilter() const
{
    return m_lightFilter;
}

namespace
{

// Rectilinear reverse projection: world point -> normalized image fraction
// (u, v) in [0,1] with depth along the view axis. This is the exact inverse of
// the perspective forward ray-gen (generatePrimaryRay), so a photon's splat lands
// in the same pixel the gather ray for that pixel would hit. Returns nullopt if
// the point is behind the camera or outside the frustum. depth is the
// forward-axis distance (> 0 in front).
struct ProjectedPoint
{
    double u;
    double v;
    double depth;
};

std::optional<ProjectedPoint> projectRectilinear(const Vector& point,
                                                 const Vector& eye,
                                                 const Quaternion& rot,
                                                 double verticalFovDegrees,
                                                 double aspect)
{
    const Vector right = rot * Vector::unitX;
    const Vector up = rot * Vector::unitY;
    const Vector forward = rot * Vector::unitZ;

    const Vector toPoint = point - eye;
    const double depth = Vector::dot(toPoint, forward);
    if (depth <= 0.0)
    {
        return std::nullopt;  // behind the camera
    }

    const double halfHeight = std::tan(Utility::radians(verticalFovDegrees) / 2.0);
    if (halfHeight <= 0.0)
    {
        return std::nullopt;
    }

    const double dx = Vector::dot(toPoint, right);
    const double dy = Vector::dot(toPoint, up);

    // Screen coords sx,sy in [-1,1] mirror generatePrimaryRay's mapping.
    const double sx = (dx / depth) / (aspect * halfHeight);
    const double sy = (dy / depth) / halfHeight;

    if (sx < -1.0 || sx > 1.0 || sy < -1.0 || sy > 1.0)
    {
        return std::nullopt;  // outside the frustum
    }

    return ProjectedPoint{(sx + 1.0) / 2.0, (sy + 1.0) / 2.0, depth};
}

}  // namespace

std::optional<PixelCoords> Camera::coordForPoint(const Vector& point) const
{
    const std::optional<ProjectedPoint> projected =
        projectRectilinear(point, position(), rotation(), m_verticalFieldOfView, m_aspectRatio);
    if (!projected)
    {
        return std::nullopt;
    }

    return PixelCoords{
        std::min(m_width - 1, static_cast<size_t>(std::round(projected->u * m_width))),
        std::min(m_height - 1, static_cast<size_t>(std::round(projected->v * m_height)))
    };
}

std::optional<Camera::SubPixelCoords> Camera::coordForPointSubPixel(const Vector& point) const
{
    const std::optional<ProjectedPoint> projected =
        projectRectilinear(point, position(), rotation(), m_verticalFieldOfView, m_aspectRatio);
    if (!projected)
    {
        return std::nullopt;
    }

    // Pixel centers live at integer values; the forward mapping puts pixel coord
    // k at normalized fraction (k + 0.5) / dimension. We invert to a continuous
    // coordinate consistent with that.
    return SubPixelCoords{
        projected->u * static_cast<double>(m_width),
        projected->v * static_cast<double>(m_height)
    };
}

Camera::ExposureWindow Camera::exposureWindowForPixel(const PixelCoords& /*coord*/) const
{
    // Default: the globally-configured window. Initially infinite (accepts everything);
    // setGlobalExposureWindow narrows it for motion-blur runs without requiring a
    // Camera subclass. Rolling-shutter / slit / leaf models override this method.
    return m_globalExposureWindow;
}

void Camera::setGlobalExposureWindow(const ExposureWindow& window)
{
    m_globalExposureWindow = window;
}

Camera::ExposureWindow Camera::globalExposureWindow() const
{
    return m_globalExposureWindow;
}

namespace
{

// Normalized image-plane coordinates for a pixel. sx,sy in [-1, 1): sx increases
// left->right, sy increases bottom->top (NOT flipped — y=0 maps to sy=-1, matching
// the historical vertical-angle convention and coordForPointSubPixel's inverse).
// With subPixelOffset in [0,1)^2 the sample lands inside the pixel cell; (0.5,0.5)
// is the pixel center.
struct ScreenSample
{
    double sx;
    double sy;
};

ScreenSample screenSampleFor(const PixelCoords& coord,
                             size_t width,
                             size_t height,
                             double offsetX,
                             double offsetY)
{
    const double u = (static_cast<double>(coord.x) + offsetX) / static_cast<double>(width);
    const double v = (static_cast<double>(coord.y) + offsetY) / static_cast<double>(height);
    return ScreenSample{2.0 * u - 1.0, 2.0 * v - 1.0};
}

}  // namespace

Vector Camera::pixelDirection(const PixelCoords& coord) const
{
    // Direction of the deterministic pixel-center primary ray. For perspective and
    // reallens this is the rectilinear pinhole direction; for orthographic every
    // pixel shares the forward direction. (reallens uses the same center direction
    // here; aperture jitter only happens through generatePrimaryRay with a
    // generator.) Kept for callers that only need a direction.
    return generatePrimaryRay(coord, nullptr).direction;
}

Ray Camera::generatePrimaryRay(const PixelCoords& coord, RandomGenerator* generator) const
{
    // Static pose (scene-load eye + orientation). The time-aware overload
    // generatePrimaryRayAt resolves an animated pose first and calls the same body,
    // so a NON-animated camera goes through here unchanged (static parity).
    return buildPrimaryRay(coord, position(), rotation(), generator);
}

Ray Camera::generatePrimaryRayAt(const PixelCoords& coord,
                                 float time,
                                 const AnimationQuery* animation,
                                 RandomGenerator* generator) const
{
    // Camera motion blur: resolve the eye + orientation at this ray's scene time,
    // then build the ray exactly as the static path does. With no animation entry
    // this returns the scene-load pose for every time, so the ray is identical to
    // generatePrimaryRay (byte-for-byte static parity).
    const EyeRotation pose = resolveEyeRotationAt(time, animation);
    return buildPrimaryRay(coord, pose.eye, pose.rotation, generator);
}

Camera::EyeRotation Camera::resolveEyeRotationAt(float time,
                                                 const AnimationQuery* animation) const
{
    if (animation != nullptr)
    {
        // Mirrors Volume::resolveTransformAt: an animated camera carries an entry
        // under its name(); a static one returns nullopt and falls back below.
        const std::optional<Transform> overridden = animation->transformAt(name(), time);
        if (overridden)
        {
            return EyeRotation{overridden->position, overridden->rotation};
        }
    }
    return EyeRotation{position(), rotation()};
}

Ray Camera::buildPrimaryRay(const PixelCoords& coord,
                            const Vector& eye,
                            const Quaternion& rot,
                            RandomGenerator* generator) const
{
    const Vector right = rot * Vector::unitX;
    const Vector up = rot * Vector::unitY;
    const Vector forward = rot * Vector::unitZ;

    // Sub-pixel jitter: center (0.5,0.5) when no generator, else uniform within the
    // pixel cell for anti-aliasing across samples.
    double offX = 0.5;
    double offY = 0.5;
    if (generator != nullptr)
    {
        offX = generator->value(1.0);
        offY = generator->value(1.0);
    }
    const ScreenSample s = screenSampleFor(coord, m_width, m_height, offX, offY);

    const double halfHeight = std::tan(Utility::radians(m_verticalFieldOfView) / 2.0);
    const double aspect = m_aspectRatio;

    switch (m_projection)
    {
        case Projection::Orthographic:
        {
            // Parallel rays: all share the forward direction. The ORIGIN varies
            // across the image plane (size set by orthographicHeight). No
            // perspective convergence.
            const double halfH = m_orthographicHeight / 2.0;
            const Vector origin = eye
                + (right * (s.sx * aspect * halfH))
                + (up * (s.sy * halfH));
            return Ray{origin, forward.normalized()};
        }

        case Projection::RealLens:
        {
            // Thin-lens DOF. The pinhole direction defines the ray that would be
            // cast by a perspective camera; it crosses the focus plane at
            // focusDistance along the view axis. We originate the ray from a
            // sampled point on the aperture disk and aim it at that focus point, so
            // everything at focusDistance stays sharp while nearer/farther points
            // blur. Each sample jitters the aperture point.
            const Vector pinholeDir =
                (forward + (right * (s.sx * aspect * halfHeight)) + (up * (s.sy * halfHeight)))
                    .normalized();

            // Focus point: where the pinhole ray crosses the focus plane (a plane
            // perpendicular to forward at focusDistance). t along the ray such that
            // its forward-axis depth == focusDistance.
            const double cosToAxis = Vector::dot(pinholeDir, forward);
            const double t = (cosToAxis > 1e-9)
                ? (m_focusDistance / cosToAxis)
                : m_focusDistance;
            const Vector focusPoint = eye + (pinholeDir * t);

            // Sample a point on the aperture disk (concentric within the lens
            // plane spanned by right/up). Center when no generator -> degenerates
            // to the sharp pinhole ray.
            double lensX = 0.0;
            double lensY = 0.0;
            if (generator != nullptr)
            {
                const double radius = effectiveApertureRadius();
                // Uniform disk sample via sqrt-radius / angle.
                const double r = radius * std::sqrt(generator->value(1.0));
                const double theta = generator->value(Utility::pi2);
                lensX = r * std::cos(theta);
                lensY = r * std::sin(theta);
            }
            const Vector aperturePoint = eye + (right * lensX) + (up * lensY);
            const Vector dir = (focusPoint - aperturePoint).normalized();
            return Ray{aperturePoint, dir};
        }

        case Projection::Perspective:
        default:
        {
            // Rectilinear pinhole: ray through a flat image plane. Straight world
            // lines stay straight on screen. [INVARIANT] tangent-based, NOT f-theta.
            const Vector direction =
                forward
                + (right * (s.sx * aspect * halfHeight))
                + (up * (s.sy * halfHeight));
            return Ray{eye, direction.normalized()};
        }
    }
}
