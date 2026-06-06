#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

// The editor's in-memory scene model.
//
// KEY DESIGN DECISION: the editor's project format IS the renderer's scene JSON
// (the same format `ray-tracer <scene>.json` reads — see src/SceneLoader.cpp for
// the schema). Rather than reuse the renderer's LoadedScene (whose meshes are
// already wrapped in opaque BVH trees and whose object hierarchy is built for
// ray casting, not editing), this model is parsed DIRECTLY from the scene JSON
// into editable, GL-friendly structs:
//
//   * camera   — verticalFOV + position + euler rotation (pitch/yaw/roll degrees)
//   * materials — by name: type (Lambertian/Mirror/Glass) + color (+ ior)
//   * objects  — a flat list of named entries, each with a transform
//                (position/rotation/scale), a Kind, and kind-specific data:
//                  SphereVolume  -> center + radius + materialName
//                  MeshVolume    -> meshName (an OBJ sub-shape) + materialName
//                  AreaLight     -> shape + width/height/radius
//                  OmniLight     -> (gizmo only)
//
// The full parsed JSON is retained (`rawJson`) so render-from-view can re-emit
// the exact scene with only the camera block rewritten to the live orbit camera
// — the model round-trips the objects for display, and the JSON round-trips the
// scene for rendering.
//
// This stays GL-free pure data so it can be parsed/serialized without a GL
// context; the viewport's GL upload of these objects lives in EditorApp.
struct SceneModel
{
    // ----- materials -------------------------------------------------------
    struct Material
    {
        std::string name;
        std::string type;          // "Lambertian" | "Mirror" | "Glass" (normalized)
        glm::vec3 color{0.8f};     // base color / tint
        double ior = 1.5;          // Glass only
    };

    // ----- objects ---------------------------------------------------------
    enum class Kind
    {
        SphereVolume,
        MeshVolume,
        AreaLight,
        OmniLight,
        Group,        // a plain Object container (e.g. "CornellBox")
        Other
    };

    enum class LightShape
    {
        Square,
        Disc
    };

    struct ObjectNode
    {
        std::string name;
        Kind kind = Kind::Other;

        // Transform (renderer convention: euler stored as pitch/yaw/roll degrees,
        // matching the $rotation "PitchYawRollDegrees" form).
        glm::vec3 position{0.0f};
        glm::vec3 eulerDegrees{0.0f};   // pitch, yaw, roll
        glm::vec3 scale{1.0f};

        std::string materialName;       // for volumes

        // SphereVolume
        glm::vec3 center{0.0f};
        double radius = 1.0;

        // MeshVolume — the OBJ sub-shape name ($mesh) this volume draws.
        std::string meshName;

        // AreaLight / OmniLight
        LightShape lightShape = LightShape::Square;
        double lightWidth = 0.0;
        double lightHeight = 0.0;
        double lightRadius = 0.0;
    };

    // ----- camera ----------------------------------------------------------
    struct CameraDesc
    {
        std::string name = "Camera";
        glm::vec3 position{0.0f, 0.0f, -10.0f};
        glm::vec3 eulerDegrees{0.0f};   // pitch, yaw, roll
        double verticalFovDegrees = 70.0;
        bool present = false;
    };

    // The OBJ files referenced by "$meshes". Resolved to absolute paths against
    // the scene directory at load time, so the viewport can load the named
    // sub-shapes for drawing.
    std::vector<std::string> meshFiles;

    CameraDesc camera;
    std::vector<Material> materials;
    std::vector<ObjectNode> objects;

    // The full, unmodified parsed scene JSON. Render-from-view re-serializes this
    // with only the "Camera" block's $position/$rotation/$verticalFieldOfView
    // overwritten by the live orbit camera (see EditorApp::startRender).
    nlohmann::json rawJson;

    std::string name = "untitled";
    std::string path;       // file the scene was loaded from / saved to
    bool dirty = false;

    // Lookup helper. Returns nullptr if no material with that name exists.
    const Material* findMaterial(const std::string& materialName) const
    {
        for (const auto& m : materials)
        {
            if (m.name == materialName)
            {
                return &m;
            }
        }
        return nullptr;
    }

    void reset()
    {
        meshFiles.clear();
        materials.clear();
        objects.clear();
        camera = CameraDesc{};
        rawJson = nlohmann::json::object();
        name = "untitled";
        path.clear();
        dirty = false;
        m_initialized = true;
    }

    bool isInitialized() const { return m_initialized; }
    bool isEmpty() const { return objects.empty(); }

private:
    bool m_initialized = false;
};

// Backwards-compatible alias: existing call sites refer to `Scene`. The model is
// now the real SceneModel above.
using Scene = SceneModel;
