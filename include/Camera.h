#pragma once

#include "Object.h"
#include "PixelCoords.h"

#include <optional>

class Camera : public Object
{
public:
    Camera(size_t width, size_t height, float verticalFieldOfView);

    size_t width() const;
    size_t height() const;
    float aspectRatio() const;
    float horizontalFieldOfView() const;
    float verticalFieldOfView() const;

    std::optional<PixelCoords> coordForPoint(const Vector& point) const;
    Vector pixelDirection(const PixelCoords& coord) const;

private:
    const size_t m_width;
    const size_t m_height;
    const float m_aspectRatio;
    const float m_verticalFieldOfView;
    const float m_horizontalFieldOfView;
};
