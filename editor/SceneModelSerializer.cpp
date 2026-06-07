#include "SceneModelSerializer.h"

#include <glm/glm.hpp>

using nlohmann::json;

namespace
{

json vec3Json(const glm::vec3& v)
{
    return json::array({v.x, v.y, v.z});
}

json rotationJson(const glm::vec3& eulerDegrees)
{
    return json{{"$type", "PitchYawRollDegrees"},
                {"$value", json::array({eulerDegrees.x, eulerDegrees.y, eulerDegrees.z})}};
}

// Map the model's normalized material type back to a renderer scene $type. The
// renderer accepts "Diffuse" and "Lambertian" interchangeably; "Diffuse" is the
// canonical scene-file form, so we emit that for Lambertian.
const char* materialTypeJson(const std::string& modelType)
{
    if (modelType == "Mirror") return "Mirror";
    if (modelType == "Glass") return "Glass";
    if (modelType == "Microfacet") return "Microfacet";
    return "Diffuse";
}

}  // namespace

namespace SceneModelSerializer
{

json toJson(const SceneModel& model)
{
    json out = json::object();

    // ----- $meshes ---------------------------------------------------------
    if (!model.meshFiles.empty())
    {
        json meshes = json::array();
        for (const auto& f : model.meshFiles)
        {
            meshes.push_back(f);
        }
        out["$meshes"] = std::move(meshes);
    }

    // ----- $materials ------------------------------------------------------
    json materials = json::object();
    for (const auto& mat : model.materials)
    {
        json m = json::object();
        m["$type"] = materialTypeJson(mat.type);
        m["$color"] = vec3Json(mat.color);
        if (mat.type == "Glass")
        {
            m["$ior"] = mat.ior;
        }
        materials[mat.name] = std::move(m);
    }
    out["$materials"] = std::move(materials);

    // ----- preserved config blocks (or synthesized defaults) ---------------
    // Reuse the originally-loaded worker/render config when present so renders of
    // an edited scene match the scene's tuning; otherwise synthesize defaults so
    // a from-scratch (File > New) scene renders. Resolution + photon budget are
    // overridden by the editor at render time regardless.
    if (model.rawJson.is_object() && model.rawJson.contains("$workerConfiguration"))
    {
        out["$workerConfiguration"] = model.rawJson["$workerConfiguration"];
    }
    if (model.rawJson.is_object() && model.rawJson.contains("$renderConfiguration"))
    {
        out["$renderConfiguration"] = model.rawJson["$renderConfiguration"];
    }
    else
    {
        out["$renderConfiguration"] = json{
            {"$width", 256},          {"$height", 256},
            {"$photonsPerLight", 4000000}, {"$startFrame", 0},
            {"$endFrame", 0},         {"$renderPath", "renders"},
            {"$renderName", "editor_render"}, {"$bounceThreshold", 4}};
    }

    // ----- $scene ----------------------------------------------------------
    json scene = json::object();

    // Cameras. Each scene camera is emitted as a renderer Camera node with its
    // per-camera resolution / exposure / debug filters / output name, matching the
    // renderer's multi-camera format (see MultiCameraTest.json + SceneLoader).
    int cameraOrdinal = 0;
    for (const auto& camDesc : model.cameras)
    {
        json cam = json::object();
        cam["$type"] = "Camera";
        cam["$verticalFieldOfView"] = camDesc.verticalFovDegrees;
        cam["$position"] = vec3Json(camDesc.position);
        cam["$rotation"] = rotationJson(camDesc.eulerDegrees);

        // Projection model + params. Always emit $projection so editor-authored
        // cameras carry the type; emit the per-projection params unconditionally
        // (they are cheap and keep DOF/ortho edits round-tripping).
        cam["$projection"] = camDesc.projection;
        cam["$orthoHeight"] = camDesc.orthoHeight;
        cam["$apertureRadius"] = camDesc.apertureRadius;
        cam["$focusDistance"] = camDesc.focusDistance;
        cam["$focalLength"] = camDesc.focalLength;

        // Per-camera resolution override.
        cam["$width"] = camDesc.width;
        cam["$height"] = camDesc.height;

        // Per-camera exposure.
        cam["$fNumber"] = camDesc.fNumber;
        cam["$shutterTime"] = camDesc.shutterTime;
        cam["$iso"] = camDesc.iso;

        // Per-camera debug filters (only when enabled; the loader defaults to -1).
        if (camDesc.bounceFilter >= 0)
        {
            cam["$bounceFilter"] = camDesc.bounceFilter;
        }
        if (camDesc.lightFilter >= 0)
        {
            cam["$lightFilter"] = camDesc.lightFilter;
        }

        // Output: round-trip the editor's pattern AND emit an $outputName derived
        // from the camera name so the renderer's own naming stays sensible.
        cam["$outputPathPattern"] = camDesc.outputPathPattern;
        cam["$outputName"] = camDesc.name;

        const std::string key =
            camDesc.name.empty() ? ("Camera." + std::to_string(cameraOrdinal)) : camDesc.name;
        scene[key] = std::move(cam);
        ++cameraOrdinal;
    }

    for (const auto& obj : model.objects)
    {
        // Group containers carry no geometry of their own; their children were
        // flattened with absolute world positions at load time, so the group
        // itself would only re-introduce a double offset. Skip it.
        if (obj.kind == SceneModel::Kind::Group || obj.kind == SceneModel::Kind::Other)
        {
            continue;
        }

        json node = json::object();
        node["$position"] = vec3Json(obj.position);
        node["$rotation"] = rotationJson(obj.eulerDegrees);

        switch (obj.kind)
        {
            case SceneModel::Kind::SphereVolume:
                node["$type"] = "SphereVolume";
                node["$material"] = obj.materialName;
                node["$center"] = vec3Json(obj.center);
                node["$radius"] = obj.radius;
                break;
            case SceneModel::Kind::MeshVolume:
                node["$type"] = "MeshVolume";
                node["$material"] = obj.materialName;
                node["$mesh"] = obj.meshName;
                break;
            case SceneModel::Kind::AreaLight:
            {
                node["$type"] = "AreaLight";
                node["$shape"] =
                    (obj.lightShape == SceneModel::LightShape::Disc) ? "Disc" : "Square";
                if (obj.lightShape == SceneModel::LightShape::Disc)
                {
                    node["$radius"] = obj.lightRadius;
                }
                else
                {
                    node["$width"] = obj.lightWidth;
                    node["$height"] = obj.lightHeight;
                }
                node["$color"] = json::array({1.0, 1.0, 1.0});
                // A luminous-flux override so an inserted light actually emits.
                // 4*pi^2 * size^2 keeps brightness roughly area-proportional and
                // matches the magnitude used in the bundled Cornell proof scene.
                const double area = (obj.lightShape == SceneModel::LightShape::Disc)
                                        ? (3.14159265358979 * obj.lightRadius * obj.lightRadius)
                                        : (obj.lightWidth * obj.lightHeight);
                node["$luminousFlux"] = area > 0.0 ? area * 8700.0 : 125663706.0;
                break;
            }
            case SceneModel::Kind::OmniLight:
                node["$type"] = "OmniLight";
                node["$color"] = json::array({1.0, 1.0, 1.0});
                node["$intensityCandela"] = 1000000.0;
                break;
            case SceneModel::Kind::Group:
            case SceneModel::Kind::Other:
                break;  // handled above
        }

        scene[obj.name] = std::move(node);
    }

    out["$scene"] = std::move(scene);
    return out;
}

}  // namespace SceneModelSerializer
