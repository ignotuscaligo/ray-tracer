#pragma once

#include "Scene.h"

#include <nlohmann/json.hpp>

#include <string>

// Serializes the editor's in-memory SceneModel BACK into a renderer scene JSON
// (the same format src/SceneLoader.cpp reads). This is the inverse of
// SceneModelLoader and the reason inserts/edits actually render: render-from-view
// emits the CURRENT model (not the originally-loaded rawJson), so objects added
// or edited in the editor appear in the path-traced image.
//
// Design notes:
//   * Materials map back to renderer $type strings: the model normalizes
//     "Lambertian"/"Mirror"/"Glass"/"Microfacet"; we emit "Diffuse" for
//     Lambertian (the renderer accepts both, "Diffuse" is the canonical scene
//     form) and pass $ior through for Glass.
//   * Objects emit a flat $scene (no nested groups). The loader composed parent
//     positions into world positions, so emitting each object at its world
//     $position round-trips placement for the kinds the editor edits. Group
//     containers (Kind::Group) are skipped — their geometry-bearing children are
//     already flattened with absolute positions.
//   * Area lights get a $luminousFlux so a freshly-inserted light actually lights
//     the scene (a zero-flux light renders black).
//   * If the model carries a rawJson with $renderConfiguration / $meshes /
//     $workerConfiguration, those blocks are preserved; otherwise sensible
//     defaults are synthesized so a from-scratch (File > New) scene renders.
//
// Deliberately GL-free (pure data -> JSON), so it is unit-testable without a GL
// context, mirroring SceneModelLoader.
namespace SceneModelSerializer
{

// Build a renderer scene JSON object from `model`. `meshFiles` are emitted into
// "$meshes" (the absolute/resolved OBJ paths the model references). The camera,
// when present, is emitted; pass overrides via the model's camera fields.
nlohmann::json toJson(const SceneModel& model);

}  // namespace SceneModelSerializer
