#include "SceneLoader.h"

#include "Contracts.h"

#include "AreaLight.h"
#include "DielectricMaterial.h"
#include "LambertianMaterial.h"
#include "MeshVolume.h"
#include "MicrofacetMaterial.h"
#include "MirrorMaterial.h"
#include "OmniLight.h"
#include "ParallelLight.h"
#include "PlaneVolume.h"
#include "Quaternion.h"
#include "QuadVolume.h"
#include "SphereVolume.h"
#include "SpotLight.h"
#include "Utility.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace nlohmann;

namespace
{

// Bit-flag helper for optional logging during parse. Kept local to the loader.
enum class LogOption
{
    None,
    WithLog
};

template<typename T>
void setFromJsonIfPresent(T& output, json jsonContainer, const std::string& key, bool log = false)
{
    if (jsonContainer.contains(key))
    {
        output = jsonContainer[key].get<T>();
        if (log)
        {
            std::cout << "  Setting " << key << " to " << output << std::endl;
        }
    }
}

// Boundary contract: the scene file is external input, so a malformed render
// config is a caller (scene-author) error, not an internal invariant. These
// preconditions fail LOUDLY in debug/sanitizer builds instead of letting a
// degenerate value silently produce a broken render (e.g. a 0-dim image, a
// 0-photons-per-light splat that is silently disabled, or a densityCellScale
// that Renderer.cpp clamps up to 0.05 with no warning). Inert in release.
void validateRenderSettings(const RenderSettings& settings)
{
    PRECONDITION_MSG(settings.imageWidth > 0, "$width must be > 0");
    PRECONDITION_MSG(settings.imageHeight > 0, "$height must be > 0");
    PRECONDITION_MSG(settings.photonsPerLight > 0,
                     "$photonsPerLight must be > 0 (0 silently disables the camera splat)");
    PRECONDITION_MSG(settings.workerCount > 0, "$workerCount must be >= 1");
    PRECONDITION_MSG(settings.fetchSize > 0, "$fetchSize must be > 0");
    PRECONDITION_MSG(settings.photonQueueSize > 0, "$photonQueueSize must be > 0");
    PRECONDITION_MSG(settings.bounceThreshold >= 1, "$bounceThreshold must be >= 1");
    PRECONDITION_MSG(settings.terminationThreshold >= 0.0,
                     "$terminationThreshold is an absolute magnitude floor; must be >= 0");
    // densityCellScale is silently clamped to 0.05 in Renderer.cpp. Surface a
    // non-positive value at the input boundary rather than letting the clamp hide it.
    PRECONDITION_MSG(settings.densityCellScale > 0.0,
                     "$densityCellScale must be > 0 (Renderer silently clamps it otherwise)");
    PRECONDITION_MSG(settings.splatMinRadiusScale >= 0.0,
                     "$splatMinRadiusScale must be >= 0 (0 disables the floor)");
    PRECONDITION_MSG(settings.splatLuminanceClamp >= 0.0,
                     "$splatLuminanceClamp must be >= 0 (0 disables the clamp)");
}

class VectorParseError : public std::runtime_error
{
public:
    VectorParseError()
    : std::runtime_error("Vector object must be an array with 3 or 4 number elements")
    {
    }
};

class RotationParseError : public std::runtime_error
{
public:
    RotationParseError()
    : std::runtime_error("Rotation object must contain a $type string and a Vector $value object")
    {
    }
};

class ColorParseError : public std::runtime_error
{
public:
    ColorParseError()
    : std::runtime_error("Color object must be an array with 1 or 3 number elements")
    {
    }
};

Vector parseVectorFromJson(json jsonContainer)
{
    if (!jsonContainer.is_array())
    {
        throw VectorParseError();
    }

    if (jsonContainer.size() < 3 || jsonContainer.size() > 4)
    {
        throw VectorParseError();
    }

    for (auto& element : jsonContainer)
    {
        if (!element.is_number())
        {
            throw VectorParseError();
        }
    }

    Vector result{
        jsonContainer[0].get<double>(),
        jsonContainer[1].get<double>(),
        jsonContainer[2].get<double>()
    };

    if (jsonContainer.size() == 4)
    {
        result._w = jsonContainer[3].get<double>();
    }

    return result;
}

void setVectorFromJsonIfPresent(Vector& output, json jsonContainer, const std::string& key)
{
    if (jsonContainer.contains(key))
    {
        output = parseVectorFromJson(jsonContainer[key]);
    }
}

Quaternion parseRotationFromJson(json jsonContainer)
{
    if (!jsonContainer.is_object())
    {
        throw RotationParseError();
    }

    if (!jsonContainer.contains("$type"))
    {
        throw RotationParseError();
    }

    if (!jsonContainer.contains("$value"))
    {
        throw RotationParseError();
    }

    json typeObject = jsonContainer["$type"];

    if (!typeObject.is_string())
    {
        throw RotationParseError();
    }

    std::string type = typeObject.get<std::string>();
    Vector value = parseVectorFromJson(jsonContainer["$value"]);

    Quaternion result;

    if (type == "PitchYawRollDegrees")
    {
        result = Quaternion::fromPitchYawRoll(Utility::radians(value.x), Utility::radians(value.y), Utility::radians(value.z));
    }
    else if (type == "PitchYawRollRadians")
    {
        result = Quaternion::fromPitchYawRoll(value.x, value.y, value.z);
    }
    else
    {
        throw std::runtime_error(std::string("Unsupported rotation type: ") + type);
    }

    return result;
}

void setRotationFromJsonIfPresent(Quaternion& output, json jsonContainer, const std::string& key)
{
    if (jsonContainer.contains(key))
    {
        output = parseRotationFromJson(jsonContainer[key]);
    }
}

// Parse a keyframe list into a Property<double>. Each entry is an object
// {"t": <seconds>, "value": <number>} with optional explicit tangents
// {"inTangent": <num>, "outTangent": <num>} (per-second slopes; presence makes
// the keyframe use explicit tangents instead of the auto Catmull-Rom tangent).
Property<double> parseScalarKeyframes(const json& keyframeArray)
{
    Property<double> property;
    if (!keyframeArray.is_array())
    {
        throw std::runtime_error("$animation keyframe list must be an array");
    }
    for (const auto& entry : keyframeArray)
    {
        if (!entry.is_object() || !entry.contains("t") || !entry.contains("value"))
        {
            throw std::runtime_error(
                "Each keyframe must be an object with 't' and 'value'");
        }
        Keyframe<double> key;
        key.time = entry["t"].get<double>();
        key.value = entry["value"].get<double>();
        if (entry.contains("inTangent") || entry.contains("outTangent"))
        {
            key.useAutoTangent = false;
            key.inTangent = entry.contains("inTangent") ? entry["inTangent"].get<double>() : 0.0;
            key.outTangent = entry.contains("outTangent") ? entry["outTangent"].get<double>() : 0.0;
        }
        property.addKeyframe(key);
    }
    return property;
}

// Parse a keyframe list into a Property<Vector>. Each entry is
// {"t": <seconds>, "value": [x, y, z]}.
Property<Vector> parseVectorKeyframes(const json& keyframeArray)
{
    Property<Vector> property;
    if (!keyframeArray.is_array())
    {
        throw std::runtime_error("$animation keyframe list must be an array");
    }
    for (const auto& entry : keyframeArray)
    {
        if (!entry.is_object() || !entry.contains("t") || !entry.contains("value"))
        {
            throw std::runtime_error(
                "Each keyframe must be an object with 't' and 'value'");
        }
        Keyframe<Vector> key;
        key.time = entry["t"].get<double>();
        key.value = parseVectorFromJson(entry["value"]);
        property.addKeyframe(key);
    }
    return property;
}

// Parse an object's "$animation" block into an AnimatedObject. The object's
// scene-load transform supplies the base rotation/scale/position that authored
// curves layer onto (a $rotation curve composes onto the load-time orientation;
// an absent $position holds the load-time position).
//
// Schema:
//   "$animation": {
//     "$rotationAxis": [x, y, z],          // optional, default [0,1,0]
//     "$rotation": [ {"t":.., "value":..}, ... ],   // radians about the axis
//     "$position": [ {"t":.., "value":[x,y,z]}, ... ]
//   }
KeyframedAnimationQuery::AnimatedObject parseAnimationBlock(
    const json& animationJson, const Transform& loadTransform)
{
    KeyframedAnimationQuery::AnimatedObject animated;
    animated.baseRotation = loadTransform.rotation;
    animated.scale = loadTransform.scale;

    // Seed position from the scene-load transform as a CONSTANT Property so an
    // object animated by $rotation ONLY (no $position curve) holds its load-time
    // position instead of teleporting to the origin. transformAt reads
    // `position.evaluate(t)` unconditionally; without this seed an absent $position
    // left hasPosition false and the query returned {0,0,0}. A $position curve below
    // overwrites this. (A constant Property is time-independent, so a static-position
    // animated object is unchanged from its load pose.)
    animated.position = Property<Vector>(loadTransform.position);
    animated.hasPosition = true;

    if (animationJson.contains("$rotationAxis"))
    {
        animated.rotationAxis = parseVectorFromJson(animationJson["$rotationAxis"]).normalized();
    }

    if (animationJson.contains("$rotation"))
    {
        animated.rotationAngle = parseScalarKeyframes(animationJson["$rotation"]);
        animated.hasRotation = true;
    }

    if (animationJson.contains("$position"))
    {
        animated.position = parseVectorKeyframes(animationJson["$position"]);
        animated.hasPosition = true;
    }

    return animated;
}

Color parseColorFromJson(json jsonContainer)
{
    if (!jsonContainer.is_array())
    {
        throw ColorParseError();
    }

    if (jsonContainer.size() != 1 && jsonContainer.size() != 3)
    {
        throw ColorParseError();
    }

    for (auto& element : jsonContainer)
    {
        if (!element.is_number())
        {
            throw ColorParseError();
        }
    }

    if (jsonContainer.size() == 1)
    {
        const float grey = jsonContainer[0].get<float>();
        return {grey, grey, grey};
    }
    else
    {
        return {
            jsonContainer[0].get<float>(),
            jsonContainer[1].get<float>(),
            jsonContainer[2].get<float>()
        };
    }
}

void setColorFromJsonIfPresent(Color& output, json jsonContainer, const std::string& key)
{
    if (jsonContainer.contains(key))
    {
        output = parseColorFromJson(jsonContainer[key]);
    }
}

class ObjectFactory
{
public:
    ObjectFactory(std::shared_ptr<MaterialLibrary> materialLibrary, std::shared_ptr<MeshLibrary> meshLibrary)
    : m_materialLibrary(materialLibrary)
    , m_meshLibrary(meshLibrary)
    {
    }

    void setParametersForObject(std::shared_ptr<Object> object, json jsonContainer)
    {
        setVectorFromJsonIfPresent(object->transform.position, jsonContainer, "$position");
        setRotationFromJsonIfPresent(object->transform.rotation, jsonContainer, "$rotation");
    }

    void setParametersForCamera(std::shared_ptr<Camera> object, json jsonContainer)
    {
        setParametersForObject(object, jsonContainer);

        double verticalFieldOfView = 0;
        setFromJsonIfPresent(verticalFieldOfView, jsonContainer, "$verticalFieldOfView");

        object->verticalFieldOfView(verticalFieldOfView);

        // Camera projection model. "$projection": "perspective" (default) |
        // "orthographic" | "reallens". perspective is rectilinear pinhole; an
        // absent key keeps existing scenes on perspective.
        std::string projection;
        setFromJsonIfPresent(projection, jsonContainer, "$projection");
        if (projection == "orthographic")
        {
            object->projection(Camera::Projection::Orthographic);
        }
        else if (projection == "reallens")
        {
            object->projection(Camera::Projection::RealLens);
        }
        else if (projection.empty() || projection == "perspective")
        {
            object->projection(Camera::Projection::Perspective);
        }
        else
        {
            throw std::runtime_error(
                "Unknown $projection \"" + projection +
                "\" (expected perspective | orthographic | reallens)");
        }

        // Orthographic image-plane height (world units). Only used when
        // $projection == orthographic.
        double orthoHeight = object->orthographicHeight();
        setFromJsonIfPresent(orthoHeight, jsonContainer, "$orthoHeight");
        object->orthographicHeight(orthoHeight);

        // Thin-lens DOF params. Only used when $projection == reallens.
        //   $apertureRadius — explicit lens-disk radius (world units). If absent or
        //                     <= 0 the radius is derived from $focalLength / (2 * N).
        //   $focusDistance  — distance to the plane of perfect focus.
        //   $focalLength     — used with $fNumber to derive an aperture radius.
        double apertureRadius = object->apertureRadius();
        setFromJsonIfPresent(apertureRadius, jsonContainer, "$apertureRadius");
        object->apertureRadius(apertureRadius);

        double focusDistance = object->focusDistance();
        setFromJsonIfPresent(focusDistance, jsonContainer, "$focusDistance");
        object->focusDistance(focusDistance);

        double focalLength = object->focalLength();
        setFromJsonIfPresent(focalLength, jsonContainer, "$focalLength");
        object->focalLength(focalLength);

        // Wave 2: physically-based photographic exposure controls. Omitted keys
        // keep the Camera's neutral defaults (f/8, 1/100s, ISO 100).
        double fNumber = object->fNumber();
        setFromJsonIfPresent(fNumber, jsonContainer, "$fNumber");
        object->fNumber(fNumber);

        double shutterTime = object->shutterTime();
        setFromJsonIfPresent(shutterTime, jsonContainer, "$shutterTime");
        object->shutterTime(shutterTime);

        double iso = object->iso();
        setFromJsonIfPresent(iso, jsonContainer, "$iso");
        object->iso(iso);

        if (jsonContainer.contains("$exposureWindow"))
        {
            json& window = jsonContainer["$exposureWindow"];
            if (window.is_array() && window.size() == 2)
            {
                Camera::ExposureWindow w;
                w.start = window[0].get<float>();
                w.end = window[1].get<float>();
                object->setGlobalExposureWindow(w);
            }
        }

        // Wave 6: multi-camera + debug-camera attributes. All optional; a single-
        // camera scene that sets none of these behaves exactly as before.
        //   $outputName  — base name for this camera's PNG (out_<name>.png).
        //   $width/$height — per-camera resolution override. When both are present
        //                  the camera keeps its own resolution regardless of the
        //                  global $renderConfiguration size (which is otherwise
        //                  imposed at load time below).
        //   $bounceFilter — gather only deposits with bounces == N (debug camera).
        //   $lightFilter  — gather only deposits with light-id == idx (debug camera).
        std::string outputName;
        setFromJsonIfPresent(outputName, jsonContainer, "$outputName");
        object->outputName(outputName);

        if (jsonContainer.contains("$width") && jsonContainer.contains("$height"))
        {
            const size_t w = jsonContainer["$width"].get<size_t>();
            const size_t h = jsonContainer["$height"].get<size_t>();
            object->setResolution(w, h);
        }

        int bounceFilter = -1;
        setFromJsonIfPresent(bounceFilter, jsonContainer, "$bounceFilter");
        object->bounceFilter(bounceFilter);

        int lightFilter = -1;
        setFromJsonIfPresent(lightFilter, jsonContainer, "$lightFilter");
        object->lightFilter(lightFilter);
    }

    void setParametersForVolume(std::shared_ptr<Volume> object, json jsonContainer)
    {
        setParametersForObject(object, jsonContainer);

        std::string material = "Default";
        setFromJsonIfPresent(material, jsonContainer, "$material");

        object->materialIndex(m_materialLibrary->indexForName(material));
    }

    void setParametersForMeshVolume(std::shared_ptr<MeshVolume> object, json jsonContainer)
    {
        setParametersForVolume(object, jsonContainer);

        std::string mesh = "Default";
        setFromJsonIfPresent(mesh, jsonContainer, "$mesh");

        object->mesh(m_meshLibrary->fetch(mesh));
    }

    void setParametersForPlaneVolume(std::shared_ptr<PlaneVolume> object, json jsonContainer)
    {
        setParametersForVolume(object, jsonContainer);
    }

    void setParametersForSphereVolume(std::shared_ptr<SphereVolume> object, json jsonContainer)
    {
        setParametersForVolume(object, jsonContainer);

        Vector center = object->center();
        setVectorFromJsonIfPresent(center, jsonContainer, "$center");
        object->center(center);

        double radius = object->radius();
        setFromJsonIfPresent(radius, jsonContainer, "$radius");
        object->radius(radius);
    }

    void setParametersForQuadVolume(std::shared_ptr<QuadVolume> object, json jsonContainer)
    {
        setParametersForVolume(object, jsonContainer);

        // Two authoring forms:
        //   1. "$corners": [c0, c1, c2, c3] — four corners in order around the
        //      quad. origin = c0, edgeU = c1 - c0, edgeV = c3 - c0.
        //   2. "$origin" + "$edgeU" + "$edgeV" — origin corner and two edge
        //      vectors spanning the (parallelogram) quad.
        // $corners takes precedence when present.
        if (jsonContainer.contains("$corners"))
        {
            json corners = jsonContainer["$corners"];

            if (!corners.is_array() || corners.size() != 4)
            {
                throw std::runtime_error("Quad \"$corners\" must be an array of exactly 4 vectors");
            }

            const Vector c0 = parseVectorFromJson(corners[0]);
            const Vector c1 = parseVectorFromJson(corners[1]);
            const Vector c2 = parseVectorFromJson(corners[2]);
            const Vector c3 = parseVectorFromJson(corners[3]);

            object->quad(Quad::fromCorners(c0, c1, c2, c3));
        }
        else
        {
            Vector origin = object->origin();
            setVectorFromJsonIfPresent(origin, jsonContainer, "$origin");

            Vector edgeU = object->edgeU();
            setVectorFromJsonIfPresent(edgeU, jsonContainer, "$edgeU");

            Vector edgeV = object->edgeV();
            setVectorFromJsonIfPresent(edgeV, jsonContainer, "$edgeV");

            object->quad(Quad(origin, edgeU, edgeV));
        }
    }

    void setParametersForLight(std::shared_ptr<Light> object, json jsonContainer)
    {
        setParametersForObject(object, jsonContainer);

        Color color;
        setColorFromJsonIfPresent(color, jsonContainer, "$color");

        object->color(color);

        // Wave 2: lights are defined photometrically in luminous INTENSITY
        // (candela = lumens/steradian) via "$intensityCandela". For back-compat,
        // legacy "$brightness" maps onto intensity directly (same numeric value).
        // Precedence: $intensityCandela > $brightness.
        double intensity = 0.0;
        setFromJsonIfPresent(intensity, jsonContainer, "$brightness");
        setFromJsonIfPresent(intensity, jsonContainer, "$intensityCandela");

        object->intensityCandela(intensity);
    }

    void setParametersForOmniLight(std::shared_ptr<OmniLight> object, json jsonContainer)
    {
        setParametersForLight(object, jsonContainer);

        double innerRadius = 0.0;
        setFromJsonIfPresent(innerRadius, jsonContainer, "$innerRadius");

        object->innerRadius(innerRadius);
    }

    void setParametersForParallelLight(std::shared_ptr<ParallelLight> object, json jsonContainer)
    {
        setParametersForLight(object, jsonContainer);

        double radius = 0.0;
        setFromJsonIfPresent(radius, jsonContainer, "$radius");

        object->radius(radius);
    }

    void setParametersForSpotLight(std::shared_ptr<SpotLight> object, json jsonContainer)
    {
        setParametersForLight(object, jsonContainer);

        double innerRadius = 0.0;
        setFromJsonIfPresent(innerRadius, jsonContainer, "$innerRadius");

        object->innerRadius(innerRadius);

        double angle = 0.0;
        setFromJsonIfPresent(angle, jsonContainer, "$angle");

        object->angle(angle);
    }

    void setParametersForAreaLight(std::shared_ptr<AreaLight> object, json jsonContainer)
    {
        setParametersForLight(object, jsonContainer);

        // Shape selector: "$shape" is "Square" (default) or "Disc".
        std::string shape = "Square";
        setFromJsonIfPresent(shape, jsonContainer, "$shape");
        object->shape(shape == "Disc" ? AreaLight::Shape::Disc : AreaLight::Shape::Square);

        // Square/rectangle extents. "$size" sets both width and height; "$width"
        // / "$height" override individually for a non-square rectangle.
        double size = 0.0;
        setFromJsonIfPresent(size, jsonContainer, "$size");
        double width = size;
        double height = size;
        setFromJsonIfPresent(width, jsonContainer, "$width");
        setFromJsonIfPresent(height, jsonContainer, "$height");
        object->width(width);
        object->height(height);

        // Disc radius.
        double radius = 0.0;
        setFromJsonIfPresent(radius, jsonContainer, "$radius");
        object->radius(radius);

        // Optional direct total-flux override (lumens). When present it replaces
        // the I*pi convention, making this light energy-comparable to a point
        // light reporting the same luminousFlux().
        double luminousFlux = 0.0;
        setFromJsonIfPresent(luminousFlux, jsonContainer, "$luminousFlux");
        object->luminousFluxOverride(luminousFlux);
    }

    std::shared_ptr<Object> createObjectFromJson(json jsonContainer)
    {
        std::string type = "Object";
        setFromJsonIfPresent(type, jsonContainer, "$type");

        if (type == "Object")
        {
            std::shared_ptr<Object> object = std::make_shared<Object>();
            setParametersForObject(object, jsonContainer);
            return object;
        }
        else if (type == "Camera")
        {
            std::shared_ptr<Camera> object = std::make_shared<Camera>();
            setParametersForCamera(object, jsonContainer);
            return object;
        }
        else if (type == "MeshVolume")
        {
            std::shared_ptr<MeshVolume> object = std::make_shared<MeshVolume>();
            setParametersForMeshVolume(object, jsonContainer);
            return object;
        }
        else if (type == "PlaneVolume")
        {
            std::shared_ptr<PlaneVolume> object = std::make_shared<PlaneVolume>();
            setParametersForPlaneVolume(object, jsonContainer);
            return object;
        }
        else if (type == "SphereVolume")
        {
            std::shared_ptr<SphereVolume> object = std::make_shared<SphereVolume>();
            setParametersForSphereVolume(object, jsonContainer);
            return object;
        }
        else if (type == "Quad" || type == "QuadVolume")
        {
            std::shared_ptr<QuadVolume> object = std::make_shared<QuadVolume>();
            setParametersForQuadVolume(object, jsonContainer);
            return object;
        }
        else if (type == "OmniLight")
        {
            std::shared_ptr<OmniLight> object = std::make_shared<OmniLight>();
            setParametersForOmniLight(object, jsonContainer);
            return object;
        }
        else if (type == "ParallelLight")
        {
            std::shared_ptr<ParallelLight> object = std::make_shared<ParallelLight>();
            setParametersForParallelLight(object, jsonContainer);
            return object;
        }
        else if (type == "SpotLight")
        {
            std::shared_ptr<SpotLight> object = std::make_shared<SpotLight>();
            setParametersForSpotLight(object, jsonContainer);
            return object;
        }
        else if (type == "AreaLight")
        {
            std::shared_ptr<AreaLight> object = std::make_shared<AreaLight>();
            setParametersForAreaLight(object, jsonContainer);
            return object;
        }
        else
        {
            throw std::runtime_error(std::string("Unrecognized object type: ") + type);
        }
    }

private:
    std::shared_ptr<MaterialLibrary> m_materialLibrary;
    std::shared_ptr<MeshLibrary> m_meshLibrary;
};

std::vector<std::shared_ptr<Object>> parseObjectFromJson(const std::string& name, json jsonContainer, std::shared_ptr<Object> parent, ObjectFactory& objectFactory, KeyframedAnimationQuery& animation)
{
    std::vector<std::shared_ptr<Object>> parsedObjects;
    std::shared_ptr<Object> parsedObject = objectFactory.createObjectFromJson(jsonContainer);
    parsedObjects.push_back(parsedObject);
    parsedObject->name(name);
    Object::setParent(parsedObject, parent);

    // Per-object keyframe animation. The block layers onto the object's just-
    // parsed scene-load transform; registering it makes the Renderer evaluate this
    // object's pose per-photon-time. Objects without a $animation block stay
    // static (no entry -> transformAt returns nullopt -> scene-load transform).
    if (jsonContainer.contains("$animation"))
    {
        KeyframedAnimationQuery::AnimatedObject animated =
            parseAnimationBlock(jsonContainer["$animation"], parsedObject->transform);
        animation.setObject(name, animated);
    }

    for (const auto& [childName, childObject] : jsonContainer.items())
    {
        if (childName[0] != '$')
        {
            std::vector<std::shared_ptr<Object>> childObjects = parseObjectFromJson(childName, childObject, parsedObject, objectFactory, animation);
            parsedObjects.insert(parsedObjects.end(), childObjects.begin(), childObjects.end());
        }
    }

    return parsedObjects;
}

std::filesystem::path resolvePath(const std::filesystem::path& path, const std::filesystem::path& basePath)
{
    if (path.is_relative())
    {
        return std::filesystem::absolute(basePath / path);
    }
    else
    {
        return path;
    }
}

}

namespace SceneLoader
{

LoadedScene loadFromFile(const std::filesystem::path& scenePath, bool logToStdout)
{
    const std::filesystem::path absoluteScenePath = std::filesystem::absolute(scenePath);

    if (logToStdout)
    {
        std::cout << "Loading project file " << absoluteScenePath.generic_string() << std::endl;
    }

    std::ifstream jsonFile(absoluteScenePath);
    json jsonData = json::parse(jsonFile);
    jsonFile.close();

    LoadedScene scene;
    RenderSettings& settings = scene.settings;

    if (jsonData.contains("$workerConfiguration"))
    {
        if (logToStdout)
        {
            std::cout << "---" << std::endl;
            std::cout << "Parsing $workerConfiguration" << std::endl;
        }
        json& workerConfiguration = jsonData["$workerConfiguration"];
        setFromJsonIfPresent(settings.workerCount, workerConfiguration, "$workerCount", logToStdout);
        setFromJsonIfPresent(settings.fetchSize, workerConfiguration, "$fetchSize", logToStdout);
        setFromJsonIfPresent(settings.photonQueueSize, workerConfiguration, "$photonQueueSize", logToStdout);
        // $emittingQueueSize is accepted-and-ignored: the emitter/back-pressure
        // queue it sized was removed with the single-photon trace-to-completion
        // pipeline. Left unparsed so legacy scenes that still specify it load.
    }

    if (jsonData.contains("$renderConfiguration"))
    {
        if (logToStdout)
        {
            std::cout << "---" << std::endl;
            std::cout << "Parsing $renderConfiguration" << std::endl;
        }
        json& renderConfiguration = jsonData["$renderConfiguration"];
        setFromJsonIfPresent(settings.imageWidth, renderConfiguration, "$width", logToStdout);
        setFromJsonIfPresent(settings.imageHeight, renderConfiguration, "$height", logToStdout);
        setFromJsonIfPresent(settings.photonsPerLight, renderConfiguration, "$photonsPerLight", logToStdout);
        setFromJsonIfPresent(settings.startFrame, renderConfiguration, "$startFrame", logToStdout);
        setFromJsonIfPresent(settings.endFrame, renderConfiguration, "$endFrame", logToStdout);
        setFromJsonIfPresent(settings.bounceThreshold, renderConfiguration, "$bounceThreshold", logToStdout);

        // Animation / motion-blur timing. $frameRate maps frame index -> time;
        // $shutterTime is the per-frame shutter-open duration (0 = no motion blur,
        // an instantaneous sample); $frameOffset shifts every frame's open time.
        setFromJsonIfPresent(settings.frameRate, renderConfiguration, "$frameRate", logToStdout);
        setFromJsonIfPresent(settings.shutterTime, renderConfiguration, "$shutterTime", logToStdout);
        setFromJsonIfPresent(settings.frameOffset, renderConfiguration, "$frameOffset", logToStdout);

        // Single-photon decay termination: kill a photon once its magnitude drops
        // below this ABSOLUTE floor (in photon-magnitude / flux units, scene-
        // dependent). bounceThreshold is the hard per-photon depth cap. Default 1.0.
        setFromJsonIfPresent(settings.terminationThreshold, renderConfiguration, "$terminationThreshold", logToStdout);

        // Density-grid cell-size fallback (world units). Cell size is normally
        // derived from FOV/depth by the Renderer; this override is the fallback
        // when the footprint can't be derived (and is for diagnostics).
        setFromJsonIfPresent(settings.hashGridCellSize, renderConfiguration, "$hashGridCellSize", logToStdout);
        setFromJsonIfPresent(settings.densityCellScale, renderConfiguration, "$densityCellScale", logToStdout);

        // Firefly fix: minimum splat-footprint radius as a fraction of the
        // scene-depth pixel footprint. Floors the per-hit splat radius so a
        // photon landing close to the camera can't blow up 1/(pi r^2). Default
        // 0.5; 0 disables.
        setFromJsonIfPresent(settings.splatMinRadiusScale, renderConfiguration, "$splatMinRadiusScale", logToStdout);

        // Optional extreme-firefly guard: a generous upper bound on a single
        // splat's contributed luminance. Default 0 disables it (image unchanged).
        // Set high so it only trims pathological outliers (e.g. the collinear
        // point-light/sphere-top/camera dot) without altering the normal image.
        setFromJsonIfPresent(settings.splatLuminanceClamp, renderConfiguration, "$splatLuminanceClamp", logToStdout);

        // Phase 2a: probe-guided unified gather. $probeGather true replaces the
        // splat + density-grid with one raw-bounce gather (retires the grid). The
        // store capacity, keep-radius scale, and probe sub-sample are tunables.
        setFromJsonIfPresent(settings.useProbeGather, renderConfiguration, "$probeGather", logToStdout);
        setFromJsonIfPresent(settings.bounceStoreCapacity, renderConfiguration, "$bounceStoreCapacity", logToStdout);
        setFromJsonIfPresent(settings.probeKeepRadiusScale, renderConfiguration, "$probeKeepRadiusScale", logToStdout);
        setFromJsonIfPresent(settings.probeSubSample, renderConfiguration, "$probeSubSample", logToStdout);
        // Animation temporal-coverage tunables (probe time slices + camera motion-
        // blur samples). Ignored when shutterTime == 0 (static baseline).
        setFromJsonIfPresent(settings.probeTimeSlices, renderConfiguration, "$probeTimeSlices", logToStdout);
        setFromJsonIfPresent(settings.cameraTimeSamples, renderConfiguration, "$cameraTimeSamples", logToStdout);

        if (renderConfiguration.contains("$renderPath"))
        {
            scene.renderPath = resolvePath(renderConfiguration["$renderPath"].get<std::string>(), absoluteScenePath.parent_path());
        }

        if (renderConfiguration.contains("$renderName"))
        {
            scene.renderName = renderConfiguration["$renderName"].get<std::string>();
        }
    }

    scene.materialLibrary = std::make_shared<MaterialLibrary>();

    if (jsonData.contains("$materials"))
    {
        if (logToStdout)
        {
            std::cout << "---" << std::endl;
            std::cout << "Parsing $materials" << std::endl;
        }
        json& materials = jsonData["$materials"];

        for (const auto& [name, material] : materials.items())
        {
            if (!material.contains("$type"))
            {
                throw std::runtime_error(std::string("Material \"") + name + std::string("\" requires $type"));
            }

            const std::string& type = material["$type"].get<std::string>();
            Color color;

            if (material.contains("$color"))
            {
                json& colorData = material["$color"];

                if (colorData.size() == 1)
                {
                    color = Color(colorData[0].get<float>());
                }
                else if (colorData.size() == 3)
                {
                    color = Color(colorData[0].get<float>(), colorData[1].get<float>(), colorData[2].get<float>());
                }
            }

            if (logToStdout)
            {
                std::cout << "  Creating " << type << " material named " << name << " with color (" << color.red << ", " << color.green << ", " << color.blue << ")" << std::endl;
            }

            if (type == "Diffuse" || type == "Lambertian")
            {
                scene.materialLibrary->add(std::make_shared<LambertianMaterial>(name, color));
            }
            else if (type == "Mirror")
            {
                scene.materialLibrary->add(std::make_shared<MirrorMaterial>(name, color));
            }
            else if (type == "Microfacet" || type == "GGX")
            {
                double roughness = MicrofacetMaterial::kDefaultRoughness;
                if (material.contains("$roughness"))
                {
                    roughness = material["$roughness"].get<double>();
                }
                scene.materialLibrary->add(std::make_shared<MicrofacetMaterial>(name, color, roughness));
            }
            else if (type == "Glass" || type == "Dielectric")
            {
                double ior = 1.5;
                if (material.contains("$ior"))
                {
                    ior = material["$ior"].get<double>();
                }
                // $color (if present) is the clear-glass tint; default white.
                Color tint = material.contains("$color") ? color : Color{1.0f, 1.0f, 1.0f};
                scene.materialLibrary->add(std::make_shared<DielectricMaterial>(name, ior, tint));
            }
            else if (logToStdout)
            {
                std::cout << "WARNING: Unsupported material type \"" << type << "\"" << std::endl;
            }
        }
    }

    if (logToStdout)
    {
        std::cout << "materialLibrary contains " << scene.materialLibrary->size() << " materials" << std::endl;
        for (size_t i = 0; i < scene.materialLibrary->size(); ++i)
        {
            std::cout << i << ": " << scene.materialLibrary->fetchByIndex(i)->name() << std::endl;
        }
    }

    scene.meshLibrary = std::make_shared<MeshLibrary>();

    if (jsonData.contains("$meshes"))
    {
        if (logToStdout)
        {
            std::cout << "---" << std::endl;
            std::cout << "Parsing $meshes" << std::endl;
        }
        json& meshes = jsonData["$meshes"];

        for (const auto& meshFilename : meshes)
        {
            std::filesystem::path meshPath = resolvePath(meshFilename.get<std::string>(), absoluteScenePath.parent_path());
            scene.meshLibrary->addFromFile(meshPath);
        }
    }

    if (logToStdout)
    {
        std::cout << "meshLibrary contains " << scene.meshLibrary->size() << " meshes" << std::endl;
        for (size_t i = 0; i < scene.meshLibrary->size(); ++i)
        {
            std::cout << i << ": " << scene.meshLibrary->fetchByIndex(i)->name() << std::endl;
        }
    }

    ObjectFactory objectFactory{scene.materialLibrary, scene.meshLibrary};

    if (jsonData.contains("$scene"))
    {
        if (logToStdout)
        {
            std::cout << "---" << std::endl;
            std::cout << "Parsing $scene" << std::endl;
        }
        json& sceneJson = jsonData["$scene"];

        for (const auto& [name, object] : sceneJson.items())
        {
            std::vector<std::shared_ptr<Object>> parsedObjects = parseObjectFromJson(name, object, nullptr, objectFactory, *scene.animation);
            scene.objects.insert(scene.objects.end(), parsedObjects.begin(), parsedObjects.end());
        }
    }

    // Wave 6: collect ALL cameras (in declaration order). The photon pass runs
    // once; each camera then runs its own gather. A camera that declared its own
    // $width/$height keeps that resolution; one that did not inherits the global
    // $renderConfiguration resolution (single-camera back-compat). scene.camera is
    // the first/primary camera so existing single-camera code keeps working.
    for (auto& object : scene.objects)
    {
        if (object->hasType<Camera>())
        {
            std::shared_ptr<Camera> camera = std::static_pointer_cast<Camera>(object);
            if (!camera->hasResolutionOverride())
            {
                camera->setFromRenderConfiguration(settings.imageWidth, settings.imageHeight);
            }
            scene.cameras.push_back(camera);
        }
    }

    if (!scene.cameras.empty())
    {
        scene.camera = scene.cameras.front();
    }

    if (logToStdout)
    {
        std::cout << "Parsed " << scene.objects.size() << " object(s) from json" << std::endl;
    }

    // Boundary contract: reject degenerate render config (silently-clamped or
    // illegal values) once both config blocks have been merged with defaults.
    validateRenderSettings(settings);

    return scene;
}

}
