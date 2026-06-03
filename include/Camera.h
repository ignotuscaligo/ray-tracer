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

    // --- Wave 2: physically-based photographic exposure controls ---
    //
    // The image-conversion step maps physical luminance L (cd/m^2) to a [0,1]
    // pixel value using the standard saturation-based exposure relation:
    //
    //   L_max = (N^2 * K) / (t * S)        pixel = L / L_max
    //
    // where N = aperture f-number, t = shutter time (seconds), S = ISO, and K is
    // the reflected-light meter calibration constant (~12.5). Changing N, t, or S
    // by one photographic stop changes L_max — and therefore image brightness —
    // by exactly a factor of 2, like a real camera.
    void fNumber(double fNumber);
    double fNumber() const;

    void shutterTime(double seconds);
    double shutterTime() const;

    void iso(double iso);
    double iso() const;

    // Saturation luminance L_max = (N^2 * K) / (t * S). A scene luminance equal to
    // L_max maps to a pixel value of 1.0 (full white before gamma).
    double saturationLuminance() const;

    // Meter calibration constant K in the exposure relation. Standard reflected-
    // light value is 12.5.
    static constexpr double kMeterCalibration = 12.5;

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

    // Photographic exposure controls. Defaults chosen so MirrorTest at its default
    // intensity renders at a neutral (not blown-out, not black) exposure.
    double m_fNumber = 8.0;
    double m_shutterTime = 0.01;  // seconds
    double m_iso = 100.0;
};
