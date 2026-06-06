#pragma once

#include "Scene.h"

#include <string>

// Parses a renderer scene JSON file (the same format src/SceneLoader.cpp reads)
// DIRECTLY into the editor's SceneModel — camera, materials, and a flat list of
// named objects with transforms + kind-specific data. The full parsed JSON is
// retained in SceneModel::rawJson so render-from-view can re-emit the scene with
// only the camera block rewritten.
//
// Deliberately GL-free (pure JSON -> data), so it is unit-testable without a GL
// context, mirroring the ObjLoader / RasterMesh split.
namespace SceneModelLoader
{

// Load and parse the scene file at `path`. On success returns true and fills
// `out`. On failure returns false and sets `error`. Object/material names,
// mesh references, and transforms are extracted; mesh file paths are resolved to
// absolute against the scene file's directory.
bool loadFromFile(const std::string& path, SceneModel& out, std::string& error);

}  // namespace SceneModelLoader
