#include "Camera.h"

#include "Pyramid.h"
#include "Utility.h"

#include <cmath>

Camera::Camera(size_t width, size_t height, float verticalFieldOfView)
    : Object()
    , m_width(width)
    , m_height(height)
    , m_aspectRatio(static_cast<float>(m_width) / static_cast<float>(m_height))
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

float Camera::aspectRatio() const
{
    return m_aspectRatio;
}

float Camera::horizontalFieldOfView() const
{
    return m_horizontalFieldOfView;
}

float Camera::verticalFieldOfView() const
{
    return m_verticalFieldOfView;
}

std::optional<PixelCoords> Camera::coordForPoint(const Vector& point) const
{
    Pyramid frustum = Pyramid(position(), rotation(), m_verticalFieldOfView, m_horizontalFieldOfView);
    Vector position = frustum.relativePositionInFrustum(point);

    if (position.z <= 0.0f || position.x < 0.0f || position.x > 1.0f || position.y < 0.0f || position.y > 1.0f)
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
    float horizontal = static_cast<float>(coord.x) / static_cast<float>(m_width);
    float vertical = static_cast<float>(coord.y) / static_cast<float>(m_height);
    float horizontalAngle = (-m_horizontalFieldOfView / 2.0f) + (horizontal * m_horizontalFieldOfView);
    float verticalAngle = (-m_verticalFieldOfView / 2.0f) + (vertical * m_verticalFieldOfView);

    Vector direction{
        std::sin(Utility::radians(horizontalAngle)),
        std::sin(Utility::radians(verticalAngle)),
        std::cos(Utility::radians(horizontalAngle)) * std::cos(Utility::radians(verticalAngle))
    };

    return rotation() * direction;
}
