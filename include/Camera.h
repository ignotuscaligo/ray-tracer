#pragma once

#include "Object.h"
#include "PixelCoords.h"

#include <optional>

class Camera : public Object
{
public:
    Camera();
    Camera(size_t width, size_t height, double verticalFieldOfView);

    size_t width() const;
    size_t height() const;
    double aspectRatio() const;
    double horizontalFieldOfView() const;

    void verticalFieldOfView(double verticalFieldOfView);
    double verticalFieldOfView() const;

    void setFromRenderConfiguration(size_t width, size_t height);

    std::optional<PixelCoords> coordForPoint(const Vector& point) const;
    Vector pixelDirection(const PixelCoords& coord) const;

private:
    size_t m_width;
    size_t m_height;
    double m_aspectRatio;
    double m_verticalFieldOfView;
    double m_horizontalFieldOfView;
};
