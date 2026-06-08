#pragma once

#include "Color.h"
#include "Object.h"
#include "Ray.h"
#include "Vector.h"

#include <memory>
#include <optional>
#include <vector>

// A planar emissive patch resolved from an AreaLight: its plane, in-plane extent,
// and the constant outgoing radiance it shows toward any viewer (L = M / pi for a
// Lambertian emitter). This is the shared geometric description of a light fixture
// used by BOTH:
//   - EmissiveGather (the legacy / $probeGather false fixture-visibility pass), and
//   - the probe-guided unified gather (ProbeGather), where the emitter is treated
//     as a first-class gatherable SURFACE: a camera/specular ray that lands on the
//     patch gathers the emitter's own radiance deposits exactly like any other
//     non-delta surface (so the fixture shows up directly AND in mirrors with no
//     special-case pass).
struct EmitterPatch
{
    Vector center;
    Vector normal;  // emission axis (forward); the lit face
    Vector right;   // in-plane +X (unit)
    Vector up;      // in-plane +Y (unit)
    double halfWidth = 0.0;   // square: half extent along right
    double halfHeight = 0.0;  // square: half extent along up
    double radius = 0.0;      // disc: radius
    bool isDisc = false;
    Color radiance{0.0f, 0.0f, 0.0f};
};

// Collect the emissive patches from the scene objects (currently AreaLight). Skips
// emitters with zero radiance / degenerate area.
std::vector<EmitterPatch> collectEmitterPatches(
    const std::vector<std::shared_ptr<Object>>& objects);

// Intersect a ray with the emissive patch. Returns the hit distance along the ray
// (t > 0) if it strikes the FRONT face (the lit hemisphere) within the patch
// bounds, else nullopt. A Lambertian emitter only emits from its front face, so a
// back-facing view of the fixture shows nothing.
std::optional<double> intersectEmitterPatch(const EmitterPatch& patch, const Ray& ray);
