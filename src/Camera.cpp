#include "Camera.h"

#include "Pyramid.h"
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

std::optional<PixelCoords> Camera::coordForPoint(const Vector& point) const
{
    Pyramid frustum = Pyramid(position(), rotation(), m_verticalFieldOfView, m_horizontalFieldOfView);
    Vector position = frustum.relativePositionInFrustum(point);

    if (position.z <= 0.0 || position.x < 0.0 || position.x > 1.0 || position.y < 0.0 || position.y > 1.0)
    {
        return std::nullopt;
    }

    return PixelCoords{
        std::min(m_width - 1, static_cast<size_t>(std::round(position.x * m_width))),
        std::min(m_height - 1, static_cast<size_t>(std::round(position.y * m_height)))
    };
}

std::optional<Camera::SubPixelCoords> Camera::coordForPointSubPixel(const Vector& point) const
{
    Pyramid frustum = Pyramid(position(), rotation(), m_verticalFieldOfView, m_horizontalFieldOfView);
    Vector position = frustum.relativePositionInFrustum(point);

    if (position.z <= 0.0 || position.x < 0.0 || position.x > 1.0 || position.y < 0.0 || position.y > 1.0)
    {
        return std::nullopt;
    }

    // Pixel centers live at integer values; pixelDirection(coord) maps coord.x = k to
    // normalized fraction k / width. So the continuous pixel coordinate is just
    // (normalized fraction) * dimension.
    return SubPixelCoords{
        position.x * static_cast<double>(m_width),
        position.y * static_cast<double>(m_height)
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

Vector Camera::pixelDirection(const PixelCoords& coord) const
{
    double horizontal = static_cast<double>(coord.x) / static_cast<double>(m_width);
    double vertical = static_cast<double>(coord.y) / static_cast<double>(m_height);
    double horizontalAngle = (-m_horizontalFieldOfView / 2.0) + (horizontal * m_horizontalFieldOfView);
    double verticalAngle = (-m_verticalFieldOfView / 2.0) + (vertical * m_verticalFieldOfView);

    Vector direction{
        std::sin(Utility::radians(horizontalAngle)),
        std::sin(Utility::radians(verticalAngle)),
        std::cos(Utility::radians(horizontalAngle)) * std::cos(Utility::radians(verticalAngle))
    };

    return rotation() * direction;
}
