#pragma once

#include "Camera.h"
#include "MaterialLibrary.h"
#include "MeshLibrary.h"
#include "Object.h"
#include "RenderSettings.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// The fully-parsed contents of a scene file: the object hierarchy, the camera,
// the material/mesh libraries, and the render/worker settings. This is the
// input to Renderer::render.
//
// Previously all of this parsing lived inline in src/main.cpp. It now lives in
// the library so both the `ray-tracer` executable and the `editor` GUI can build
// a scene through one code path.
struct LoadedScene
{
    std::vector<std::shared_ptr<Object>> objects;
    // Back-compat single-camera handle: points at the FIRST camera in `cameras`
    // (the primary camera). Existing single-camera code paths read this.
    std::shared_ptr<Camera> camera;
    // Wave 6: ALL cameras declared in the scene, in declaration order. The photon
    // pass runs once; the gather runs once per camera. For a single-camera scene
    // this holds exactly one entry (== camera).
    std::vector<std::shared_ptr<Camera>> cameras;
    std::shared_ptr<MaterialLibrary> materialLibrary;
    std::shared_ptr<MeshLibrary> meshLibrary;
    RenderSettings settings;

    // Output destination parsed from $renderConfiguration ($renderPath /
    // $renderName). Empty if not specified. The executable uses these to name
    // PNG files; the editor ignores them.
    std::filesystem::path renderPath;
    std::string renderName;
};

namespace SceneLoader
{

// Parse a scene JSON file (the same format the `ray-tracer` executable consumes)
// into a LoadedScene. Resolves mesh paths relative to the scene file's directory.
// Throws std::runtime_error (or a parse-specific subclass) on malformed input.
//
// If `logToStdout` is true, emits the same progress logging main.cpp historically
// printed during parsing; the editor passes false to stay quiet.
LoadedScene loadFromFile(const std::filesystem::path& scenePath, bool logToStdout = false);

}
