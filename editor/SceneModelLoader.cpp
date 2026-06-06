#include "SceneModelLoader.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using nlohmann::json;

namespace
{

glm::vec3 parseVec3(const json& arr, const glm::vec3& fallback)
{
    if (arr.is_array() && arr.size() >= 3)
    {
        return glm::vec3(arr[0].get<float>(), arr[1].get<float>(), arr[2].get<float>());
    }
    return fallback;
}

glm::vec3 parseColor(const json& arr, const glm::vec3& fallback)
{
    if (arr.is_array())
    {
        if (arr.size() == 1)
        {
            const float g = arr[0].get<float>();
            return glm::vec3(g, g, g);
        }
        if (arr.size() >= 3)
        {
            return glm::vec3(arr[0].get<float>(), arr[1].get<float>(), arr[2].get<float>());
        }
    }
    return fallback;
}

// Parse a "$rotation": {"$type": "PitchYawRoll(Degrees|Radians)", "$value": [...]}
// into pitch/yaw/roll degrees. Radians are converted to degrees so the model
// stores one consistent unit.
glm::vec3 parseRotationDegrees(const json& container)
{
    if (!container.is_object() || !container.contains("$value"))
    {
        return glm::vec3(0.0f);
    }
    glm::vec3 value = parseVec3(container["$value"], glm::vec3(0.0f));
    const std::string type = container.value("$type", std::string{"PitchYawRollDegrees"});
    if (type == "PitchYawRollRadians")
    {
        value = glm::degrees(value);
    }
    return value;
}

SceneModel::Kind kindFromType(const std::string& type)
{
    if (type == "SphereVolume") return SceneModel::Kind::SphereVolume;
    if (type == "MeshVolume") return SceneModel::Kind::MeshVolume;
    if (type == "AreaLight") return SceneModel::Kind::AreaLight;
    if (type == "OmniLight") return SceneModel::Kind::OmniLight;
    if (type == "Object") return SceneModel::Kind::Group;
    return SceneModel::Kind::Other;
}

// Recursively walk a $scene entry, emitting an ObjectNode for each typed object
// and recursing into non-$ children (the renderer's nested-group convention).
// `parentPos` accumulates parent positions so a child of a positioned group is
// placed in world space for the viewport (the renderer composes transforms down
// the hierarchy; for the proxies the editor draws, position composition is the
// part that matters visually).
void walk(const std::string& name, const json& node, const glm::vec3& parentPos,
          SceneModel& out)
{
    if (!node.is_object())
    {
        return;
    }

    const std::string type = node.value("$type", std::string{"Object"});
    const glm::vec3 localPos = node.contains("$position")
                                   ? parseVec3(node["$position"], glm::vec3(0.0f))
                                   : glm::vec3(0.0f);
    const glm::vec3 worldPos = parentPos + localPos;

    // The Camera is captured into the dedicated camera slot, not the object list.
    if (type == "Camera")
    {
        out.camera.present = true;
        out.camera.name = name;
        out.camera.position = worldPos;
        if (node.contains("$rotation"))
        {
            out.camera.eulerDegrees = parseRotationDegrees(node["$rotation"]);
        }
        out.camera.verticalFovDegrees = node.value("$verticalFieldOfView", 70.0);
    }
    else
    {
        SceneModel::ObjectNode obj;
        obj.name = name;
        obj.kind = kindFromType(type);
        obj.position = worldPos;
        if (node.contains("$rotation"))
        {
            obj.eulerDegrees = parseRotationDegrees(node["$rotation"]);
        }
        obj.materialName = node.value("$material", std::string{});

        switch (obj.kind)
        {
            case SceneModel::Kind::SphereVolume:
                obj.center = node.contains("$center")
                                 ? parseVec3(node["$center"], glm::vec3(0.0f))
                                 : glm::vec3(0.0f);
                obj.radius = node.value("$radius", 1.0);
                break;
            case SceneModel::Kind::MeshVolume:
                obj.meshName = node.value("$mesh", std::string{});
                break;
            case SceneModel::Kind::AreaLight:
            {
                const std::string shape = node.value("$shape", std::string{"Square"});
                obj.lightShape = (shape == "Disc") ? SceneModel::LightShape::Disc
                                                   : SceneModel::LightShape::Square;
                const double size = node.value("$size", 0.0);
                obj.lightWidth = node.value("$width", size);
                obj.lightHeight = node.value("$height", size);
                obj.lightRadius = node.value("$radius", 0.0);
                break;
            }
            case SceneModel::Kind::OmniLight:
            case SceneModel::Kind::Group:
            case SceneModel::Kind::Other:
                break;
        }

        // Emit groups too (they appear in the explorer as containers); they draw
        // nothing in the viewport but anchor their children's world positions.
        out.objects.push_back(std::move(obj));
    }

    // Recurse into non-$ children.
    for (const auto& [childName, childNode] : node.items())
    {
        if (!childName.empty() && childName[0] != '$')
        {
            walk(childName, childNode, worldPos, out);
        }
    }
}

}  // namespace

namespace SceneModelLoader
{

bool loadFromFile(const std::string& path, SceneModel& out, std::string& error)
{
    std::ifstream in(path);
    if (!in)
    {
        error = "could not open scene file: " + path;
        return false;
    }

    json data;
    try
    {
        in >> data;
    }
    catch (const std::exception& e)
    {
        error = std::string("scene JSON parse error: ") + e.what();
        return false;
    }

    out.reset();
    out.rawJson = data;
    out.path = path;
    out.name = std::filesystem::path(path).filename().string();

    const std::filesystem::path baseDir = std::filesystem::absolute(path).parent_path();

    // Mesh files (resolved absolute against the scene directory).
    if (data.contains("$meshes") && data["$meshes"].is_array())
    {
        for (const auto& m : data["$meshes"])
        {
            std::filesystem::path p = m.get<std::string>();
            if (p.is_relative())
            {
                p = baseDir / p;
            }
            out.meshFiles.push_back(p.string());
        }
    }

    // Materials.
    if (data.contains("$materials") && data["$materials"].is_object())
    {
        for (const auto& [matName, mat] : data["$materials"].items())
        {
            SceneModel::Material material;
            material.name = matName;
            const std::string type = mat.value("$type", std::string{"Lambertian"});
            if (type == "Mirror")
            {
                material.type = "Mirror";
            }
            else if (type == "Glass" || type == "Dielectric")
            {
                material.type = "Glass";
                material.ior = mat.value("$ior", 1.5);
            }
            else if (type == "Microfacet" || type == "GGX")
            {
                material.type = "Microfacet";
            }
            else
            {
                material.type = "Lambertian";
            }
            if (mat.contains("$color"))
            {
                material.color = parseColor(mat["$color"], glm::vec3(0.8f));
            }
            out.materials.push_back(std::move(material));
        }
    }

    // Scene objects.
    if (data.contains("$scene") && data["$scene"].is_object())
    {
        for (const auto& [objName, objNode] : data["$scene"].items())
        {
            walk(objName, objNode, glm::vec3(0.0f), out);
        }
    }

    return true;
}

}  // namespace SceneModelLoader
