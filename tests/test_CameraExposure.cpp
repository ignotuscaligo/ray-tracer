#include <catch2/catch_all.hpp>

#include "Camera.h"

#include <cmath>

using Catch::Matchers::WithinRel;

// Wave 2: physically-based photographic exposure.
//
// pixel = L / L_max, with L_max = (N^2 * K) / (t * S).
//
// "One stop" of any control means a 2x change in the IMAGE BRIGHTNESS that a
// fixed scene luminance produces. Because pixel = L / L_max, halving L_max
// doubles the pixel value. So:
//   - ISO doubling     (S -> 2S):  L_max halves  -> 2x brighter image
//   - shutter doubling (t -> 2t):  L_max halves  -> 2x brighter image
//   - aperture one stop (N -> N/sqrt(2)): N^2 halves -> L_max halves -> 2x brighter
//
// We test the L_max relation directly (it is the single quantity the conversion
// divides by), since image pixel value is exactly proportional to 1 / L_max.

namespace
{
Camera makeCamera()
{
    Camera camera(64, 64, 60.0);
    camera.fNumber(8.0);
    camera.shutterTime(0.01);
    camera.iso(100.0);
    return camera;
}
}

TEST_CASE("Saturation luminance matches L_max = (N^2 * K) / (t * S)", "[exposure]")
{
    Camera camera = makeCamera();

    const double expected = (8.0 * 8.0 * Camera::kMeterCalibration) / (0.01 * 100.0);
    REQUIRE_THAT(camera.saturationLuminance(), WithinRel(expected, 1e-9));
}

TEST_CASE("One stop of ISO doubles image exposure (L_max halves)", "[exposure]")
{
    Camera base = makeCamera();
    Camera doubled = makeCamera();
    doubled.iso(200.0);  // +1 stop of ISO

    // Image brightness is proportional to 1 / L_max. Doubling ISO must halve L_max,
    // i.e. exactly double the exposure of any fixed scene luminance.
    const double exposureRatio = base.saturationLuminance() / doubled.saturationLuminance();
    REQUIRE_THAT(exposureRatio, WithinRel(2.0, 1e-9));
}

TEST_CASE("One stop of shutter doubles image exposure (L_max halves)", "[exposure]")
{
    Camera base = makeCamera();
    Camera doubled = makeCamera();
    doubled.shutterTime(0.02);  // +1 stop: twice the exposure time

    const double exposureRatio = base.saturationLuminance() / doubled.saturationLuminance();
    REQUIRE_THAT(exposureRatio, WithinRel(2.0, 1e-9));
}

TEST_CASE("One f-stop of aperture doubles image exposure (L_max halves)", "[exposure]")
{
    Camera base = makeCamera();
    Camera opened = makeCamera();
    // One photographic stop wider: f-number divided by sqrt(2) (e.g. f/8 -> f/5.6),
    // which halves N^2 and therefore halves L_max.
    opened.fNumber(8.0 / std::sqrt(2.0));

    const double exposureRatio = base.saturationLuminance() / opened.saturationLuminance();
    REQUIRE_THAT(exposureRatio, WithinRel(2.0, 1e-9));
}

TEST_CASE("Closing one f-stop halves image exposure (L_max doubles)", "[exposure]")
{
    Camera base = makeCamera();
    Camera closed = makeCamera();
    closed.fNumber(8.0 * std::sqrt(2.0));  // f/8 -> f/11, one stop down

    const double exposureRatio = base.saturationLuminance() / closed.saturationLuminance();
    REQUIRE_THAT(exposureRatio, WithinRel(0.5, 1e-9));
}
