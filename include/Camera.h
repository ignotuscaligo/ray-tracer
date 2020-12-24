#pragma once

#include "Object.h"
#include "PixelCoords.h"

#include <optional>

class Camera : public Object
{
public:
    Camera(size_t width, size_t height, double verticalFieldOfView);

    size_t width() const;
    size_t height() const;
    double aspectRatio() const;
    double horizontalFieldOfView() const;
    double verticalFieldOfView() const;

    std::optional<PixelCoords> coordForPoint(const Vector& point) const;
    Vector pixelDirection(const PixelCoords& coord) const;

private:
    const size_t m_width;
    const size_t m_height;
    const double m_aspectRatio;
    const double m_verticalFieldOfView;
    const double m_horizontalFieldOfView;
};
