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
    // A scene camera is a CONFIGURED SHOT: a transform (position/orientation/fov)
    // plus the per-camera render properties the renderer's multi-camera path reads
    // (resolution, exposure, debug filters) and an output-path pattern for the
    // to-disk Render. This is distinct from the editor's roaming ORBIT camera
    // (OrbitCamera), which is the preview viewpoint and is NOT a scene entity.
    //
    // Cinema-4D relationship: the orbit camera is where you're looking right now;
    // the scene cameras are the shots you've set up. Preview renders the orbit
    // view into the viewport; Render renders every scene camera to disk.
    struct CameraDesc
    {
        std::string name = "Camera";
        glm::vec3 position{0.0f, 0.0f, -10.0f};
        glm::vec3 eulerDegrees{0.0f};   // pitch, yaw, roll
        double verticalFovDegrees = 70.0;
        bool present = false;            // legacy flag; see hasCamera()

        // Projection model (renderer $projection). "perspective" (default,
        // rectilinear pinhole), "orthographic", or "reallens" (thin-lens DOF).
        // Stored as the renderer's string key so the serializer round-trips it
        // verbatim and unknown future values survive an edit.
        std::string projection = "perspective";
        double orthoHeight = 100.0;      // $orthoHeight  (orthographic frame height)
        double apertureRadius = 0.0;     // $apertureRadius (<=0 => derive from N/focal)
        double focusDistance = 100.0;    // $focusDistance (reallens plane of focus)
        double focalLength = 50.0;       // $focalLength   (reallens, with fNumber)

        // Per-camera render resolution (renderer $width/$height override).
        int width = 256;
        int height = 256;

        // Per-camera exposure (the renderer's physically-based photographic
        // controls; serialized to $fNumber/$shutterTime/$iso).
        double fNumber = 8.0;
        double shutterTime = 0.01;   // seconds
        double iso = 100.0;

        // Per-camera debug filters (the renderer's debug-camera features).
        // -1 == disabled. bounceFilter isolates deposits at a given bounce depth;
        // lightFilter isolates deposits from a single light index.
        int bounceFilter = -1;
        int lightFilter = -1;

        // Output path pattern for the to-disk Render. Supports a {frame}
        // placeholder for future animation (for now {frame} -> 0). The literal
        // path is computed by resolveOutputPath().
        std::string outputPathPattern = "renders/{frame}.png";
    };

    // The OBJ files referenced by "$meshes". Resolved to absolute paths against
    // the scene directory at load time, so the viewport can load the named
    // sub-shapes for drawing.
    std::vector<std::string> meshFiles;

    // The scene's configured cameras (shots). Migrated from a single CameraDesc;
    // a single-camera scene has exactly one entry. Empty == no authored camera
    // (e.g. a File>New scene before "Add camera from current view").
    std::vector<CameraDesc> cameras;
    std::vector<Material> materials;
    std::vector<ObjectNode> objects;

    // True if the scene has at least one configured camera.
    bool hasCamera() const { return !cameras.empty(); }

    // The primary (first) camera, or nullptr if none. Used where older code
    // assumed a single camera (e.g. framing the orbit rig on load).
    const CameraDesc* primaryCamera() const
    {
        return cameras.empty() ? nullptr : &cameras.front();
    }
    CameraDesc* primaryCameraMutable()
    {
        return cameras.empty() ? nullptr : &cameras.front();
    }

    // Generate a camera name not already used by any camera, of the form "<base>"
    // or "<base>.NNN". Keeps explorer rows / layout names / $scene keys unique.
    std::string uniqueCameraName(const std::string& base) const
    {
        auto taken = [&](const std::string& n) {
            for (const auto& c : cameras)
                if (c.name == n) return true;
            return false;
        };
        if (!taken(base)) return base;
        for (int n = 1; n < 100000; ++n)
        {
            std::string candidate = base + "." + std::to_string(n);
            if (!taken(candidate)) return candidate;
        }
        return base;
    }

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

    // Mutable variant — used by the properties panel to edit a material in place.
    Material* findMaterialMutable(const std::string& materialName)
    {
        for (auto& m : materials)
        {
            if (m.name == materialName)
            {
                return &m;
            }
        }
        return nullptr;
    }

    // Generate a name not already used by any object, of the form "<base>" or
    // "<base>.NNN". Used when inserting objects so explorer rows / layout names
    // stay unique.
    std::string uniqueObjectName(const std::string& base) const
    {
        bool taken = false;
        for (const auto& o : objects)
        {
            if (o.name == base) { taken = true; break; }
        }
        if (!taken) return base;
        for (int n = 1; n < 100000; ++n)
        {
            std::string candidate = base + "." + std::to_string(n);
            bool clash = false;
            for (const auto& o : objects)
            {
                if (o.name == candidate) { clash = true; break; }
            }
            if (!clash) return candidate;
        }
        return base;  // pathological; effectively unreachable
    }

    // As above, for material names.
    std::string uniqueMaterialName(const std::string& base) const
    {
        if (findMaterial(base) == nullptr) return base;
        for (int n = 1; n < 100000; ++n)
        {
            std::string candidate = base + "." + std::to_string(n);
            if (findMaterial(candidate) == nullptr) return candidate;
        }
        return base;
    }

    // Index of a material by name, or -1 if absent.
    int materialIndex(const std::string& materialName) const
    {
        for (std::size_t i = 0; i < materials.size(); ++i)
        {
            if (materials[i].name == materialName) return static_cast<int>(i);
        }
        return -1;
    }

    // Number of objects that reference a material by name. Used to guard deletion
    // and to surface "in use" state in the Material Manager.
    int materialUseCount(const std::string& materialName) const
    {
        int count = 0;
        for (const auto& o : objects)
        {
            if (o.materialName == materialName) ++count;
        }
        return count;
    }

    void reset()
    {
        meshFiles.clear();
        materials.clear();
        objects.clear();
        cameras.clear();
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

// Resolve a camera output-path pattern to a literal path by substituting the
// {frame} placeholder. For now there is no animation, so frame defaults to 0;
// the placeholder is preserved for when the animation system lands.
inline std::string resolveOutputPath(const std::string& pattern, int frame = 0)
{
    const std::string token = "{frame}";
    std::string out = pattern;
    std::string::size_type pos = 0;
    const std::string frameStr = std::to_string(frame);
    while ((pos = out.find(token, pos)) != std::string::npos)
    {
        out.replace(pos, token.size(), frameStr);
        pos += frameStr.size();
    }
    return out;
}

// Backwards-compatible alias: existing call sites refer to `Scene`. The model is
// now the real SceneModel above.
using Scene = SceneModel;
