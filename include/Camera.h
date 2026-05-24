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
    // Continuous (floating-point) pixel-space coordinate of a world point. Pixel centers
    // are at integer values (matching pixelDirection's coord -> direction mapping). The
    // returned x is in [0, width) and y is in [0, height); std::nullopt if the point is
    // behind the camera or outside the frustum.
    //
    // This is the input to the 1-pixel-radius bouncehit gating in Worker::processFinalHits.
    // A bouncehit's contribution is splat into all pixels whose integer center is within
    // 1.0 of (fx, fy), with linear falloff in distance.
    struct SubPixelCoords
    {
        double x;
        double y;
    };
    std::optional<SubPixelCoords> coordForPointSubPixel(const Vector& point) const;
    Vector pixelDirection(const PixelCoords& coord) const;

    // Per-pixel exposure window (vision doc pillar 3). The default base-class
    // implementation returns the globally-set exposure window (initially infinite,
    // accepting every photon). Subclasses can override for position-dependent windows
    // to express rolling-shutter / slit / leaf-shutter behavior. Calling
    // setGlobalExposureWindow(...) on the base class lets a simple "global shutter"
    // motion-blur configuration be applied without subclassing.
    virtual ExposureWindow exposureWindowForPixel(const PixelCoords& coord) const;

    // Set the exposure window returned by the default exposureWindowForPixel
    // implementation. Has no effect on subclasses that override exposureWindowForPixel.
    void setGlobalExposureWindow(const ExposureWindow& window);
    ExposureWindow globalExposureWindow() const;

private:
    size_t m_width;
    size_t m_height;
    double m_aspectRatio;
    double m_verticalFieldOfView;
    double m_horizontalFieldOfView;
    ExposureWindow m_globalExposureWindow;
};
