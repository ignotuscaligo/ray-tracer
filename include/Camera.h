#pragma once

#include "Object.h"
#include "PixelCoords.h"

#include <limits>
#include <optional>
#include <utility>

class Camera : public Object
{
public:
    // Half-open exposure interval [start, end) in the same time units as Photon::time
    // (seconds). A photon contributes to a pixel only if its emission timestamp falls
    // within the exposure window returned for that pixel.
    struct ExposureWindow
    {
        float start = -std::numeric_limits<float>::infinity();
        float end   =  std::numeric_limits<float>::infinity();

        bool contains(float t) const
        {
            return t >= start && t < end;
        }
    };

    Camera();
    Camera(size_t width, size_t height, double verticalFieldOfView);
    virtual ~Camera() = default;

    size_t width() const;
    size_t height() const;
    double aspectRatio() const;
    double horizontalFieldOfView() const;

    void verticalFieldOfView(double verticalFieldOfView);
    double verticalFieldOfView() const;

    void setFromRenderConfiguration(size_t width, size_t height);

    std::optional<PixelCoords> coordForPoint(const Vector& point) const;
    Vector pixelDirection(const PixelCoords& coord) const;

    // Per-pixel exposure window (vision doc pillar 3). The default base-class
    // implementation returns an infinite window — every photon is accepted regardless of
    // its emission time. Subclasses can return narrower or position-dependent windows to
    // express global / rolling / leaf-shutter behavior. Override to introduce real motion
    // blur or shutter-pattern effects.
    virtual ExposureWindow exposureWindowForPixel(const PixelCoords& coord) const;

private:
    size_t m_width;
    size_t m_height;
    double m_aspectRatio;
    double m_verticalFieldOfView;
    double m_horizontalFieldOfView;
};
