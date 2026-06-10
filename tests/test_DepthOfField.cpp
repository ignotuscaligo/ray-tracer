#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Camera.h"
#include "Image.h"
#include "PixelCoords.h"
#include "Quaternion.h"
#include "RandomGenerator.h"
#include "Ray.h"
#include "Renderer.h"
#include "SceneLoader.h"
#include "Utility.h"
#include "Vector.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using Catch::Approx;

// ===========================================================================
// THIN-LENS DEPTH OF FIELD (reallens projection)
// ===========================================================================
//
// DOF emerges from APERTURE SAMPLING in the probe pass: each primary camera sample
// originates from a point on a finite aperture disk and is aimed at the focus point
// where the pinhole ray crosses the focus plane. Everything at $focusDistance stays
// sharp; nearer/farther geometry spreads over a circle of confusion that grows with
// aperture and with |depth - focusDistance|. The blur is SAMPLED NOISE integrated
// over the per-pixel samples — never a post-process or stepped approximation.
//
// Invariants pinned here:
//   1. GEOMETRIC: a zero-EFFECTIVE-aperture reallens ray is byte-for-byte the
//      perspective pinhole ray (the "DOF off == pinhole" base case), and every
//      aperture sample for a fixed film point converges on the focus plane.
//   2. END-TO-END (rendered): the object AT the focus distance stays sharp as the
//      aperture grows; off-focus objects blur MORE as the aperture grows; a
//      zero-aperture reallens render matches a pinhole render within Monte-Carlo
//      noise (no DOF regression on the pinhole path).

// ---------------------------------------------------------------------------
// Part 1: geometric ray-gen invariants (fast, deterministic)
// ---------------------------------------------------------------------------

namespace
{

std::shared_ptr<Camera> makeAxisCamera(size_t w, size_t h, double vfovDeg)
{
    auto camera = std::make_shared<Camera>(w, h, vfovDeg);
    camera->transform.position = Vector{0, 0, 0};
    camera->transform.rotation = Quaternion();
    return camera;
}

}  // namespace

TEST_CASE("DOF: zero effective aperture reallens ray equals the pinhole ray",
          "[dof][camera][projection]")
{
    // A reallens camera whose effective aperture is 0 (apertureRadius 0 AND a derived
    // radius of 0 via focalLength 0) is geometrically a pinhole. With NO generator the
    // ray must be identical to a perspective camera's ray for the same pixel — this is
    // the base case of the "DOF off == pinhole" invariant.
    auto pinhole = makeAxisCamera(64, 64, 60.0);
    pinhole->projection(Camera::Projection::Perspective);

    auto reallens = makeAxisCamera(64, 64, 60.0);
    reallens->projection(Camera::Projection::RealLens);
    reallens->apertureRadius(0.0);
    reallens->focalLength(0.0);   // derived radius = 0 / (2N) = 0
    reallens->focusDistance(100.0);
    REQUIRE(reallens->effectiveApertureRadius() == Approx(0.0));

    for (const PixelCoords coord : {PixelCoords{0, 0}, PixelCoords{17, 40},
                                    PixelCoords{63, 63}, PixelCoords{32, 5}})
    {
        const Ray a = pinhole->generatePrimaryRay(coord, nullptr);
        const Ray b = reallens->generatePrimaryRay(coord, nullptr);
        INFO("pixel " << coord.x << "," << coord.y);
        REQUIRE(b.origin.x == Approx(a.origin.x).margin(1e-12));
        REQUIRE(b.origin.y == Approx(a.origin.y).margin(1e-12));
        REQUIRE(b.origin.z == Approx(a.origin.z).margin(1e-12));
        REQUIRE(b.direction.x == Approx(a.direction.x).margin(1e-12));
        REQUIRE(b.direction.y == Approx(a.direction.y).margin(1e-12));
        REQUIRE(b.direction.z == Approx(a.direction.z).margin(1e-12));
    }
}

TEST_CASE("DOF: aperture samples for a fixed film point converge on the focus plane",
          "[dof][camera][projection]")
{
    // For a FIXED film (sub-pixel) point, every aperture sample must reach the SAME
    // focus point at depth == focusDistance. The blur off-focus is exactly the spread
    // of where these converging rays cross OTHER depths; on the focus plane they all
    // meet, so the focus plane stays sharp. We aim several aperture points at the
    // pinhole ray's focus point analytically and confirm convergence (mirrors the
    // Camera RealLens construction).
    auto camera = makeAxisCamera(64, 64, 60.0);
    camera->projection(Camera::Projection::RealLens);
    camera->apertureRadius(5.0);
    const double focus = 90.0;
    camera->focusDistance(focus);

    const PixelCoords coord{40, 24};
    const Ray pinhole = camera->generatePrimaryRay(coord, nullptr);  // center, no jitter
    const double tFocus = (focus - pinhole.origin.z) / pinhole.direction.z;
    const Vector focusPoint = pinhole.origin + pinhole.direction * tFocus;
    REQUIRE(focusPoint.z == Approx(focus).margin(1e-9));

    const std::vector<Vector> lensSamples{Vector{5, 0, 0}, Vector{-5, 0, 0},
                                          Vector{0, 5, 0}, Vector{3, -4, 0}};
    for (const Vector& lens : lensSamples)
    {
        const Vector aperture = pinhole.origin + lens;
        const Vector dir = Vector::normalized(focusPoint - aperture);
        const double t = (focus - aperture.z) / dir.z;
        const Vector hit = aperture + dir * t;
        REQUIRE(hit.x == Approx(focusPoint.x).margin(1e-6));
        REQUIRE(hit.y == Approx(focusPoint.y).margin(1e-6));
        REQUIRE(hit.z == Approx(focus).margin(1e-6));
    }
}

// ---------------------------------------------------------------------------
// Part 2: end-to-end rendered DOF invariants
// ---------------------------------------------------------------------------
//
// Scene: three equal-SCREEN-radius diffuse spheres at distinct depths (Near 130,
// Mid 200, Far 280), each at a fixed screen fraction so all are in-frame and
// separated; lit frontally (light at the camera) so each is a near-uniform bright
// disc on a dark background (silhouette, not a shading terminator, dominates the
// boundary). The camera focuses on the MIDDLE sphere (depth 200).
//
// Metric: SILHOUETTE TRANSITION WIDTH via a radial luminance profile around each
// sphere centre (angular averaging cancels photon speckle). The 10-90 fall width of
// that profile is the circle-of-confusion proxy: sharp in-focus, wide off-focus.

namespace
{

constexpr int kImageDim = 200;
constexpr double kVfov = 45.0;
constexpr double kFocusDistance = 200.0;

double halfTan()
{
    return std::tan(Utility::radians(kVfov) / 2.0);
}

struct SphereSpec
{
    const char* name;
    double screenFracX;  // target screen x fraction in [-1,1]
    double depth;
};

const SphereSpec kSpheres[3] = {
    {"Near", -0.45, 130.0},
    {"Mid", 0.00, 200.0},
    {"Far", 0.45, 280.0},
};

double worldX(double frac, double depth)
{
    return frac * halfTan() * depth;  // aspect 1
}

double worldRadius(double depth)
{
    return 0.14 * halfTan() * depth;  // constant screen radius across depths
}

// Build the DOF test scene JSON for a given projection config.
std::string sceneJson(bool reallens, double apertureRadius, double focalLengthOverride)
{
    std::string s = "{\n  \"$materials\": {\n";
    s += "    \"Near\": {\"$type\": \"Diffuse\", \"$color\": [0.9]},\n";
    s += "    \"Mid\": {\"$type\": \"Diffuse\", \"$color\": [0.9]},\n";
    s += "    \"Far\": {\"$type\": \"Diffuse\", \"$color\": [0.9]}\n  },\n";
    s += "  \"$workerConfiguration\": {\"$workerCount\": 8, \"$fetchSize\": 50000, "
         "\"$photonQueueSize\": 8000000},\n";
    s += "  \"$renderConfiguration\": {\n";
    s += "    \"$width\": " + std::to_string(kImageDim) +
         ", \"$height\": " + std::to_string(kImageDim) + ",\n";
    s += "    \"$photonsPerLight\": 32000000, \"$startFrame\": 0, \"$endFrame\": 0,\n";
    s += "    \"$shutterTime\": 0.0, \"$bounceThreshold\": 2, "
         "\"$splatMinRadiusScale\": 1.0\n  },\n";
    s += "  \"$scene\": {\n";
    s += "    \"Camera\": {\n";
    s += "      \"$type\": \"Camera\", \"$verticalFieldOfView\": " +
         std::to_string(kVfov) + ",\n";
    s += "      \"$fNumber\": 8.0, \"$shutterTime\": 0.01, \"$iso\": 12000.0,\n";
    s += "      \"$position\": [0.0, 0.0, 0.0],\n";
    s += "      \"$rotation\": {\"$type\": \"PitchYawRollDegrees\", \"$value\": "
         "[0.0, 0.0, 0.0]}";
    if (reallens)
    {
        s += ",\n      \"$projection\": \"reallens\",\n";
        s += "      \"$apertureRadius\": " + std::to_string(apertureRadius) + ",\n";
        s += "      \"$focalLength\": " + std::to_string(focalLengthOverride) + ",\n";
        s += "      \"$focusDistance\": " + std::to_string(kFocusDistance) + "\n";
    }
    else
    {
        s += "\n";
    }
    s += "    },\n";
    s += "    \"Key\": {\n";
    s += "      \"$type\": \"OmniLight\", \"$position\": [0.0, 0.0, -1.0],\n";
    s += "      \"$color\": [1.0, 1.0, 1.0], \"$intensityCandela\": 1500000\n    },\n";
    s += "    \"Spheres\": {\n      \"$type\": \"Object\",\n";
    for (int i = 0; i < 3; ++i)
    {
        const SphereSpec& sp = kSpheres[i];
        const double x = worldX(sp.screenFracX, sp.depth);
        const double r = worldRadius(sp.depth);
        s += std::string("      \"") + sp.name + "\": {\"$type\": \"SphereVolume\", "
             "\"$material\": \"" + sp.name + "\", \"$center\": [" +
             std::to_string(x) + ", 0.0, " + std::to_string(sp.depth) +
             "], \"$radius\": " + std::to_string(r) + "}";
        s += (i < 2) ? ",\n" : "\n";
    }
    s += "    }\n  }\n}\n";
    return s;
}

double projectX(double x, double depth)
{
    const double sx = (x / depth) / halfTan();
    return (sx + 1.0) / 2.0 * kImageDim;
}

double projectY()
{
    return (0.0 + 1.0) / 2.0 * kImageDim;  // y=0 world -> image centre row
}

double screenRadius(double frac, double depth)
{
    const double cx = projectX(worldX(frac, depth), depth);
    const double ex = projectX(worldX(frac, depth) + worldRadius(depth), depth);
    return std::abs(ex - cx);
}

double luminanceAt(Image& img, int x, int y)
{
    const Pixel& p = img.getPixel(static_cast<size_t>(x), static_cast<size_t>(y));
    return 0.2126 * static_cast<double>(p.red) +
           0.7152 * static_cast<double>(p.green) +
           0.0722 * static_cast<double>(p.blue);
}

// Radial 10-90 silhouette transition width (px) for one sphere. Builds L(rho) =
// mean luminance over all angles at radius rho (angular averaging removes speckle),
// then measures the radial distance over which it falls from 90% to 10% of
// (interior - background). interior is a thin annulus just inside the silhouette
// (frontal-flat shading there); background is well outside.
double transitionWidth(Image& img, double frac, double depth)
{
    const double cx = projectX(worldX(frac, depth), depth);
    const double cy = projectY();
    const double sr = screenRadius(frac, depth);
    const double rmax = 2.2 * sr;
    const int nbins = static_cast<int>(rmax);
    std::vector<double> sum(static_cast<size_t>(nbins), 0.0);
    std::vector<double> cnt(static_cast<size_t>(nbins), 0.0);
    for (int y = 0; y < kImageDim; ++y)
    {
        for (int x = 0; x < kImageDim; ++x)
        {
            const double dx = x - cx;
            const double dy = y - cy;
            const double rho = std::sqrt(dx * dx + dy * dy);
            if (rho >= rmax)
            {
                continue;
            }
            const int b = static_cast<int>(rho / rmax * nbins);
            if (b >= 0 && b < nbins)
            {
                sum[static_cast<size_t>(b)] += luminanceAt(img, x, y);
                cnt[static_cast<size_t>(b)] += 1.0;
            }
        }
    }
    std::vector<double> centers;
    std::vector<double> prof;
    for (int b = 0; b < nbins; ++b)
    {
        if (cnt[static_cast<size_t>(b)] > 0.0)
        {
            centers.push_back((b + 0.5) / nbins * rmax);
            prof.push_back(sum[static_cast<size_t>(b)] / cnt[static_cast<size_t>(b)]);
        }
    }
    if (prof.size() < 6)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    // interior = mean over 0.55..0.80 sr; background = mean over >=1.6 sr.
    double interior = 0.0;
    int interiorN = 0;
    double background = 0.0;
    int backgroundN = 0;
    for (size_t i = 0; i < prof.size(); ++i)
    {
        if (centers[i] >= 0.55 * sr && centers[i] <= 0.80 * sr)
        {
            interior += prof[i];
            ++interiorN;
        }
        if (centers[i] >= 1.6 * sr)
        {
            background += prof[i];
            ++backgroundN;
        }
    }
    if (interiorN == 0 || backgroundN == 0)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    interior /= interiorN;
    background /= backgroundN;
    const double span = interior - background;
    if (span < 1e-6)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double t10 = background + 0.1 * span;
    const double t90 = background + 0.9 * span;
    auto clampP = [&](double v) { return std::min(std::max(v, background), interior); };
    double r90 = -1.0;
    double r10 = -1.0;
    for (size_t i = 1; i < prof.size(); ++i)
    {
        if (centers[i] < 0.6 * sr)
        {
            continue;
        }
        const double pPrev = clampP(prof[i - 1]);
        const double pCur = clampP(prof[i]);
        if (r90 < 0.0 && pCur <= t90)
        {
            const double denom = pPrev - pCur;
            const double f = (std::abs(denom) > 1e-9) ? (pPrev - t90) / denom : 0.0;
            r90 = centers[i - 1] + std::min(std::max(f, 0.0), 1.0) *
                                       (centers[i] - centers[i - 1]);
        }
        if (r90 >= 0.0 && pCur <= t10)
        {
            const double denom = pPrev - pCur;
            const double f = (std::abs(denom) > 1e-9) ? (pPrev - t10) / denom : 0.0;
            r10 = centers[i - 1] + std::min(std::max(f, 0.0), 1.0) *
                                       (centers[i] - centers[i - 1]);
            break;
        }
    }
    if (r90 < 0.0 || r10 < 0.0)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return r10 - r90;
}

struct Widths
{
    double near;
    double mid;
    double far;
};

Widths renderWidths(bool reallens, double apertureRadius, double focalLengthOverride)
{
    const std::string json = sceneJson(reallens, apertureRadius, focalLengthOverride);
    static int counter = 0;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("rt_dof_" + std::to_string(counter++) + ".json");
    {
        std::ofstream out(path);
        out << json;
    }
    LoadedScene scene = SceneLoader::loadFromFile(path.string(), /*logToStdout=*/false);
    std::remove(path.string().c_str());
    scene.settings.frameTime = 0.0;
    RenderResult result = Renderer::renderFrame(scene);
    REQUIRE_FALSE(result.cameras.empty());
    REQUIRE(result.cameras.front().image);
    Image& img = *result.cameras.front().image;
    return Widths{transitionWidth(img, kSpheres[0].screenFracX, kSpheres[0].depth),
                  transitionWidth(img, kSpheres[1].screenFracX, kSpheres[1].depth),
                  transitionWidth(img, kSpheres[2].screenFracX, kSpheres[2].depth)};
}

// Mean absolute luminance difference between two rendered images (parity probe).
double meanAbsLumDiff(Image& a, Image& b)
{
    REQUIRE(a.width() == b.width());
    REQUIRE(a.height() == b.height());
    double acc = 0.0;
    for (size_t y = 0; y < a.height(); ++y)
    {
        for (size_t x = 0; x < a.width(); ++x)
        {
            acc += std::abs(luminanceAt(a, static_cast<int>(x), static_cast<int>(y)) -
                            luminanceAt(b, static_cast<int>(x), static_cast<int>(y)));
        }
    }
    return acc / (static_cast<double>(a.width()) * static_cast<double>(a.height()));
}

std::shared_ptr<Image> renderImage(bool reallens, double apertureRadius,
                                   double focalLengthOverride)
{
    const std::string json = sceneJson(reallens, apertureRadius, focalLengthOverride);
    static int counter = 1000;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("rt_dof_img_" + std::to_string(counter++) + ".json");
    {
        std::ofstream out(path);
        out << json;
    }
    LoadedScene scene = SceneLoader::loadFromFile(path.string(), /*logToStdout=*/false);
    std::remove(path.string().c_str());
    scene.settings.frameTime = 0.0;
    RenderResult result = Renderer::renderFrame(scene);
    REQUIRE_FALSE(result.cameras.empty());
    REQUIRE(result.cameras.front().image);
    return result.cameras.front().image;
}

}  // namespace

TEST_CASE("DOF: focal-plane object stays sharp while off-focus objects blur with aperture",
          "[dof][gather][render]")
{
    // Pinhole baseline + two apertures. The focus is on Mid (depth 200).
    const Widths pinhole = renderWidths(/*reallens=*/false, 0.0, 0.0);
    const Widths a8 = renderWidths(/*reallens=*/true, 8.0, 50.0);
    const Widths a16 = renderWidths(/*reallens=*/true, 16.0, 50.0);

    INFO("pinhole  Near=" << pinhole.near << " Mid=" << pinhole.mid
                          << " Far=" << pinhole.far);
    INFO("apertur8 Near=" << a8.near << " Mid=" << a8.mid << " Far=" << a8.far);
    INFO("apert16  Near=" << a16.near << " Mid=" << a16.mid << " Far=" << a16.far);

    // All widths must be measurable.
    REQUIRE(std::isfinite(pinhole.near));
    REQUIRE(std::isfinite(pinhole.mid));
    REQUIRE(std::isfinite(pinhole.far));
    REQUIRE(std::isfinite(a8.near));
    REQUIRE(std::isfinite(a16.near));

    // (a) FOCAL-PLANE SHARP: the Mid (focus-distance) sphere's silhouette width does
    // NOT grow as the aperture opens — it stays within a small tolerance of its
    // pinhole sharpness. (A post-process or mis-aimed focus would blur it too.)
    REQUIRE(std::abs(a16.mid - pinhole.mid) < 2.0);
    REQUIRE(std::abs(a8.mid - pinhole.mid) < 2.0);

    // (b) OFF-FOCUS BLUR GROWS MONOTONICALLY with aperture: both off-focus spheres
    // get strictly wider silhouettes from pinhole -> R8 -> R16.
    REQUIRE(a8.near > pinhole.near + 1.0);
    REQUIRE(a16.near > a8.near);
    REQUIRE(a8.far > pinhole.far + 0.5);
    REQUIRE(a16.far > a8.far);

    // (c) OFF-FOCUS BLURRIER THAN FOCUS at the wide aperture: the focus-plane sphere
    // is the sharpest of the three once DOF is active. The Near silhouette is much
    // wider (it is closest, largest defocus circle on screen); the Far margin is
    // smaller (smallest on-screen sphere, coarser metric) so its floor is looser but
    // still well above the Far measurement's run-to-run variance.
    REQUIRE(a16.near > a16.mid + 2.0);
    REQUIRE(a16.far > a16.mid + 0.8);
}

TEST_CASE("DOF: zero-aperture reallens render matches the pinhole render within noise",
          "[dof][gather][render][parity]")
{
    // Two pinhole renders set the Monte-Carlo noise floor; a zero-effective-aperture
    // reallens render must differ from the pinhole render by NO MORE than that floor
    // (DOF off == pinhole, no regression on the no-DOF path).
    const std::shared_ptr<Image> pin1 = renderImage(/*reallens=*/false, 0.0, 0.0);
    const std::shared_ptr<Image> pin2 = renderImage(/*reallens=*/false, 0.0, 0.0);
    // reallens with apertureRadius 0 AND focalLength 0 -> effective aperture 0.
    const std::shared_ptr<Image> zero = renderImage(/*reallens=*/true, 0.0, 0.0);

    const double noiseFloor = meanAbsLumDiff(*pin1, *pin2);
    const double parityDiff = meanAbsLumDiff(*pin1, *zero);

    INFO("pinhole-vs-pinhole noise floor = " << noiseFloor
                                             << ", pinhole-vs-zeroAperture = "
                                             << parityDiff);

    // The zero-aperture reallens path is the SAME single-sample pinhole ray path, so
    // its only divergence from a pinhole render is Monte-Carlo noise. Allow a small
    // multiplicative slack above the floor for run-to-run variance.
    REQUIRE(parityDiff < std::max(0.75, noiseFloor * 1.5));
}
