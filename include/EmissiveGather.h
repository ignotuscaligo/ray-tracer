#pragma once

#include "AnimationQuery.h"
#include "Buffer.h"
#include "Camera.h"
#include "Object.h"

#include <cstddef>
#include <memory>
#include <vector>

// EMISSIVE GATHER: make light fixtures camera-visible at their true radiance.
//
// In the forward photon pipeline the camera never traces a primary ray that
// "hits a light" — the direct image of ordinary surfaces is produced by the
// splat (a surface deposits its outgoing radiance toward the camera) and mirror
// pixels are filled by the mirror gather (which reads the density grid). A bare
// AreaLight has no scene geometry, so nothing deposits its surface toward the
// camera and the fixture renders as a hole (you see the lit ceiling/walls but
// not the glowing panel itself).
//
// This pass fills that hole with NO special primary-ray-vs-light intersection
// baked into the tracer: it treats the emitter exactly like any other surface
// whose outgoing radiance the camera reads. For each camera pixel it intersects
// the pixel ray with each emissive surface (the AreaLight quad/disc), checks the
// emitter faces the camera and nothing occludes it, and writes the emitter's
// SURFACE RADIANCE L = M / pi (radiant exitance over pi) into the pixel. Because
// L is the view-independent Lambertian radiance, the panel reads as a uniform
// patch at the light's physically-correct brightness — no hot/dark spots, and
// the brightness tracks the light's actual flux, not an arbitrary value.
//
// Like the mirror gather, this only ADDS to pixels the emitter is visible in
// (the emitter has no competing geometry, so those pixels are otherwise black);
// it composites cleanly into the splat buffer. The mechanism is intentionally
// general ("an emissive surface contributes its radiance to the camera") so it
// could extend to emissive materials, with AreaLight as the concrete case.

namespace EmissiveGather
{

struct Result
{
    size_t pixelsEmissive = 0;  // camera pixels an emitter was visible in (and written)
    double maxRadiance = 0.0;   // peak per-pixel emitter luminance written (pre-tonemap)
    double sumRadiance = 0.0;   // sum of per-pixel emitter luminance written (pre-tonemap)
};

// Composite emissive-surface radiance into `buffer` (sized to the camera). Runs
// over `objects` to find emissive surfaces (currently AreaLight). `animation` /
// the object list are used for the occlusion test (an object between the camera
// and the emitter hides the fixture). `workerCount` parallelizes the pixel loop.
Result run(const std::vector<std::shared_ptr<Object>>& objects,
           const std::shared_ptr<Camera>& camera,
           const AnimationQuery* animation,
           size_t workerCount,
           Buffer& buffer);

}  // namespace EmissiveGather
