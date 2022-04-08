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
