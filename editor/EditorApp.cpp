#include "EditorApp.h"

#include "AutomationServer.h"
#include "GlHeaders.h"
#include "PngExport.h"
#include "SceneModelLoader.h"
#include "SceneModelSerializer.h"
#include "Shaders.h"

#include "Image.h"
#include "Renderer.h"
#include "SceneLoader.h"

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

using nlohmann::json;

namespace
{

// Compile a single shader stage and return its GL handle, or 0 on failure
// (logging the compile error to stderr).
GLuint compileShader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint linkProgram(const char* vsSource, const char* fsSource)
{
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSource);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSource);
    if (!vs || !fs)
    {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    // Bind attribute locations to match the VAO layouts. Both the mesh VAO
    // (position/normal) and the line VAO (position/color) put position at 0 and
    // their second attribute at 1, so binding all three names is safe — only the
    // names actually present in a given program take effect.
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aNormal");
    glBindAttribLocation(program, 1, "aColor");
    glLinkProgram(program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Program link error: %s\n", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

// Walk up from a starting directory looking for a file/dir, so the editor works
// whether launched from the repo root or from build/Release. Returns empty path
// if not found within a few levels.
std::filesystem::path findUpwards(const std::string& relative, int maxLevels = 6)
{
    std::filesystem::path dir = std::filesystem::current_path();
    for (int i = 0; i <= maxLevels; ++i)
    {
        std::filesystem::path candidate = dir / relative;
        if (std::filesystem::exists(candidate))
        {
            return candidate;
        }
        if (!dir.has_parent_path() || dir.parent_path() == dir)
        {
            break;
        }
        dir = dir.parent_path();
    }
    return {};
}

// Build the same quaternion the renderer's Quaternion::fromPitchYawRoll produces
// (pitch/yaw/roll in RADIANS), as a glm::quat, so the viewport's model transform
// matches the renderer's transform exactly. Component order here mirrors
// src/Quaternion.cpp::fromPitchYawRoll.
glm::quat rendererQuat(float pitch, float yaw, float roll)
{
    const float p = pitch * 0.5f, y = yaw * 0.5f, r = roll * 0.5f;
    const float cp = std::cos(p), cy = std::cos(y), cr = std::cos(r);
    const float sp = std::sin(p), sy = std::sin(y), sr = std::sin(r);
    glm::quat q;
    q.x = sp * cy * cr + cp * sy * sr;
    q.y = cp * sy * cr - sp * cy * sr;
    q.z = cp * cy * sr + sp * sy * cr;
    q.w = cp * cy * cr - sp * sy * sr;
    return q;
}

// Model matrix for a scene object: translate to world position, then rotate by
// the renderer-convention euler (degrees). Scale is applied last (objects here
// use unit scale, but the seam is kept for later waves).
glm::mat4 objectModelMatrix(const glm::vec3& position, const glm::vec3& eulerDegrees,
                            const glm::vec3& scale)
{
    const glm::quat q = rendererQuat(glm::radians(eulerDegrees.x), glm::radians(eulerDegrees.y),
                                     glm::radians(eulerDegrees.z));
    glm::mat4 m(1.0f);
    m = glm::translate(m, position);
    m = m * glm::mat4_cast(q);
    m = glm::scale(m, scale);
    return m;
}

// Tessellate a UV sphere of `radius` centered at `center` into flat triangle
// MeshData (position + normal), for drawing a SphereVolume proxy in the viewport.
MeshData makeSphereMesh(const glm::vec3& center, float radius, int stacks = 16, int slices = 24)
{
    MeshData out;
    auto pointAt = [&](int i, int j) {
        const float v = static_cast<float>(i) / static_cast<float>(stacks);  // 0..1 top->bottom
        const float u = static_cast<float>(j) / static_cast<float>(slices);  // 0..1 around
        const float phi = v * 3.14159265358979f;            // 0..pi
        const float theta = u * 2.0f * 3.14159265358979f;   // 0..2pi
        const glm::vec3 n{std::sin(phi) * std::cos(theta), std::cos(phi),
                          std::sin(phi) * std::sin(theta)};
        return std::pair<glm::vec3, glm::vec3>{center + n * radius, n};
    };
    for (int i = 0; i < stacks; ++i)
    {
        for (int j = 0; j < slices; ++j)
        {
            auto a = pointAt(i, j);
            auto b = pointAt(i + 1, j);
            auto c = pointAt(i + 1, j + 1);
            auto d = pointAt(i, j + 1);
            out.vertices.push_back(RasterVertex{a.first, a.second});
            out.vertices.push_back(RasterVertex{b.first, b.second});
            out.vertices.push_back(RasterVertex{c.first, c.second});
            out.vertices.push_back(RasterVertex{a.first, a.second});
            out.vertices.push_back(RasterVertex{c.first, c.second});
            out.vertices.push_back(RasterVertex{d.first, d.second});
        }
    }
    out.minBound = center - glm::vec3(radius);
    out.maxBound = center + glm::vec3(radius);
    out.valid = true;
    return out;
}

}  // namespace

EditorApp::EditorApp() = default;

EditorApp::~EditorApp() = default;

void EditorApp::setInitialMeshPath(const std::string& path)
{
    m_meshPath = path;
}

void EditorApp::setAutomationPort(uint16_t port)
{
    m_automationPort = port;
}

void EditorApp::setHeadless(bool headless)
{
    m_headless = headless;
}

bool EditorApp::initialize()
{
    if (!glfwInit())
    {
        std::fprintf(stderr, "glfwInit failed\n");
        return false;
    }

    // OpenGL 3.2 core + forward-compat: the highest profile macOS exposes.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    // Headless: create the window hidden. It still has a real GL context and we
    // still create the FBO + read pixels, so rendering and screenshots work; the
    // window just never appears. This is the default for automation runs.
    glfwWindowHint(GLFW_VISIBLE, m_headless ? GLFW_FALSE : GLFW_TRUE);

    m_window = glfwCreateWindow(1280, 800, "Ray Tracer Editor", nullptr, nullptr);
    if (!m_window)
    {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return false;
    }

    glfwSetWindowUserPointer(m_window, this);
    // Register OUR GLFW callbacks. These only translate raw events into
    // InputEvents and feed the single input path (dispatchInputEvent). ImGui's
    // own glfw callbacks are deliberately NOT installed (see InitForOpenGL
    // below) so this app is the sole consumer of GLFW input — real OS events and
    // port-injected events then flow through the exact same code.
    glfwSetMouseButtonCallback(m_window, &EditorApp::mouseButtonCallback);
    glfwSetCursorPosCallback(m_window, &EditorApp::cursorPosCallback);
    glfwSetScrollCallback(m_window, &EditorApp::scrollCallback);
    glfwSetKeyCallback(m_window, &EditorApp::keyCallback);
    glfwSetCharCallback(m_window, &EditorApp::charCallback);

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);  // vsync

    // ImGui setup.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    // In headless (automation) mode, do NOT load/save imgui.ini. A persisted
    // layout from a prior windowed session can pin the controls window at (0,0),
    // overlapping the main menu bar — which makes the menu bar un-hoverable, so
    // injected clicks can't open menus. Disabling the ini gives automation a
    // deterministic fresh layout (driven by the SetNextWindowPos FirstUseEver
    // defaults below, which sit windows BELOW the menu bar via WorkPos).
    if (m_headless)
    {
        io.IniFilename = nullptr;
    }

    // install_callbacks = false: we do NOT let the backend hook GLFW. Instead we
    // own the GLFW callbacks (above) and feed ImGui's IO ourselves from
    // dispatchInputEvent(). ImGui_ImplGlfw_NewFrame() still runs each frame for
    // display-size / delta-time / cursor-shape / gamepad bookkeeping — it just no
    // longer receives input events; those come exclusively through our path.
    ImGui_ImplGlfw_InitForOpenGL(m_window, false);
    // GLSL 1.50 matches the 3.2 core context.
    ImGui_ImplOpenGL3_Init("#version 150");

    // Build the viewport shaders: the lit mesh shader and the flat line shader
    // used for the ground grid + origin gnomon.
    m_shaderProgram = linkProgram(kViewportVertexShader, kViewportFragmentShader);
    if (!m_shaderProgram)
    {
        std::fprintf(stderr, "Failed to build viewport shader\n");
        return false;
    }
    m_lineProgram = linkProgram(kLineVertexShader, kLineFragmentShader);
    if (!m_lineProgram)
    {
        std::fprintf(stderr, "Failed to build line shader\n");
        return false;
    }

    glEnable(GL_DEPTH_TEST);

    // Bake the static grid + gnomon geometry once (requires a GL context).
    m_grid.build(/*halfExtent=*/20.0f, /*spacing=*/1.0f);

    // Wave 1 foundation: start with an empty scene framed on the origin (the
    // grid + gnomon). Object insertion / scenes come in later waves. An explicit
    // mesh path on the command line still loads (preview + the render panel use
    // it), but the default is the clean empty-scene viewport — not an auto-loaded
    // Cornell box.
    newScene();

    if (!m_meshPath.empty())
    {
        std::string err = loadMeshFromPath(m_meshPath);
        if (!err.empty())
        {
            std::fprintf(stderr, "%s\n", err.c_str());
        }
    }

    // Resolve a scene file to render.
    std::filesystem::path scene = findUpwards("MirrorTest.json");
    if (!scene.empty())
    {
        m_scenePath = scene.string();
    }

    resizeFbo(800, 600);

    // Start the localhost automation port if requested (127.0.0.1 only).
    if (m_automationPort != 0)
    {
        m_automation = std::make_unique<AutomationServer>();
        if (!m_automation->start(m_automationPort,
                                 [this](const json& req) { return handleCommand(req); }))
        {
            std::fprintf(stderr, "Automation port failed to start; continuing without it.\n");
            m_automation.reset();
        }
    }

    return true;
}

// Shared mesh-loading path used by startup, the automation load_mesh command,
// and the script runner. Requires a current GL context (calls m_mesh.upload).
// Returns an empty string on success, else an error description.
std::string EditorApp::loadMeshFromPath(const std::string& path)
{
    MeshData data = loadObjMeshData(path);
    if (!data.valid)
    {
        m_meshLabel = "load failed: " + data.error;
        return data.error;
    }

    m_mesh.upload(data);
    m_camera.frameBounds(data.minBound, data.maxBound);
    m_meshPath = path;
    m_meshLabel = std::filesystem::path(path).filename().string() + " (" +
                  std::to_string(data.triangleCount()) + " tris)";
    return {};
}

void EditorApp::newScene()
{
    // File > New: reset the scene model to a pristine empty scene and frame the
    // camera on the world origin so the grid + gnomon present immediately. This
    // is the seam later waves build on — once object insertion exists, New gives
    // a blank canvas to insert into; once save exists, New clears the path.
    m_scene.reset();

    // Drop any previewed mesh so the empty scene really is empty in the viewport.
    // (Render-panel mesh preview returns when a mesh is explicitly loaded.)
    m_mesh.upload(MeshData{});
    m_meshPath.clear();
    m_meshLabel = "(empty scene)";

    // Drop any uploaded scene-object geometry + selection.
    for (auto& d : m_sceneDrawables)
    {
        if (d.gizmoVbo) glDeleteBuffers(1, &d.gizmoVbo);
        if (d.gizmoVao) glDeleteVertexArrays(1, &d.gizmoVao);
    }
    m_sceneDrawables.clear();
    m_selectedObject = -1;

    m_camera.frameOrigin();
}

// ===== Model mutation (shared by GUI + puppet) =============================

std::string EditorApp::ensureDefaultMaterial(const std::string& typeName,
                                             const glm::vec3& color)
{
    // Always mint a FRESH material per inserted object (unique name). Sharing a
    // material across objects makes the properties panel surprising — editing one
    // object's color/type would silently change every object that shares it. Each
    // object owning its material keeps edits local; the serializer emits them all.
    SceneModel::Material mat;
    mat.name = m_scene.uniqueMaterialName(typeName + "Mat");
    mat.type = typeName;
    mat.color = color;
    m_scene.materials.push_back(mat);
    return mat.name;
}

int EditorApp::insertObject(Scene::Kind kind)
{
    if (!m_scene.isInitialized())
    {
        m_scene.reset();
    }

    SceneModel::ObjectNode obj;
    obj.kind = kind;
    obj.scale = glm::vec3(1.0f);

    switch (kind)
    {
        case SceneModel::Kind::SphereVolume:
            obj.name = m_scene.uniqueObjectName("Sphere");
            obj.position = glm::vec3(0.0f, 1.0f, 0.0f);  // sit above the floor
            obj.center = glm::vec3(0.0f);
            obj.radius = 1.0;
            obj.materialName = ensureDefaultMaterial("Lambertian", glm::vec3(0.8f));
            break;
        case SceneModel::Kind::MeshVolume:
        {
            obj.name = m_scene.uniqueObjectName("Mesh");
            obj.position = glm::vec3(0.0f, 1.0f, 0.0f);
            obj.materialName = ensureDefaultMaterial("Lambertian", glm::vec3(0.8f));
            // Default to a bundled mesh. Resolve the OBJ + its first shape and
            // register the OBJ in the model's mesh-file list so render + viewport
            // both find it.
            std::filesystem::path meshPath = findUpwards("meshes/eschers_knot.obj");
            std::string shapeName = "Knot";
            if (meshPath.empty())
            {
                meshPath = findUpwards("meshes/cube.obj");
                shapeName = "Cube";
            }
            if (!meshPath.empty())
            {
                const std::string abs = std::filesystem::absolute(meshPath).string();
                bool have = false;
                for (const auto& f : m_scene.meshFiles)
                {
                    if (f == abs) { have = true; break; }
                }
                if (!have) m_scene.meshFiles.push_back(abs);
                obj.meshName = shapeName;
            }
            break;
        }
        case SceneModel::Kind::AreaLight:
            obj.name = m_scene.uniqueObjectName("AreaLight");
            obj.position = glm::vec3(0.0f, 5.0f, 0.0f);
            obj.eulerDegrees = glm::vec3(90.0f, 0.0f, 0.0f);  // face down at the floor
            obj.lightShape = SceneModel::LightShape::Square;
            obj.lightWidth = 2.0;
            obj.lightHeight = 2.0;
            break;
        case SceneModel::Kind::OmniLight:
            obj.name = m_scene.uniqueObjectName("OmniLight");
            obj.position = glm::vec3(0.0f, 5.0f, 0.0f);
            break;
        case SceneModel::Kind::Group:
        case SceneModel::Kind::Other:
            obj.name = m_scene.uniqueObjectName("Object");
            break;
    }

    m_scene.objects.push_back(std::move(obj));
    m_scene.dirty = true;
    const int index = static_cast<int>(m_scene.objects.size()) - 1;

    // Rebuild viewport geometry so the new object appears immediately, and select
    // it (so the properties panel targets it right away).
    buildSceneGl();
    m_selectedObject = index;
    return index;
}

bool EditorApp::setObjectFloatField(int index, const std::string& field, float value)
{
    if (index < 0 || index >= static_cast<int>(m_scene.objects.size()))
    {
        return false;
    }
    SceneModel::ObjectNode& o = m_scene.objects[static_cast<std::size_t>(index)];

    if (field == "pos_x") o.position.x = value;
    else if (field == "pos_y") o.position.y = value;
    else if (field == "pos_z") o.position.z = value;
    else if (field == "rot_x") o.eulerDegrees.x = value;
    else if (field == "rot_y") o.eulerDegrees.y = value;
    else if (field == "rot_z") o.eulerDegrees.z = value;
    else if (field == "scale_x") o.scale.x = value;
    else if (field == "scale_y") o.scale.y = value;
    else if (field == "scale_z") o.scale.z = value;
    else if (field == "radius") o.radius = value;
    else if (field == "light_width") o.lightWidth = value;
    else if (field == "light_height") o.lightHeight = value;
    else if (field == "light_radius") o.lightRadius = value;
    else if (field == "mat_color_r" || field == "mat_color_g" || field == "mat_color_b" ||
             field == "mat_ior")
    {
        SceneModel::Material* mat = m_scene.findMaterialMutable(o.materialName);
        if (!mat) return false;
        if (field == "mat_color_r") mat->color.r = value;
        else if (field == "mat_color_g") mat->color.g = value;
        else if (field == "mat_color_b") mat->color.b = value;
        else if (field == "mat_ior") mat->ior = value;
    }
    else
    {
        return false;
    }

    m_scene.dirty = true;
    buildSceneGl();  // re-tessellate proxies / re-place model matrices
    return true;
}

bool EditorApp::setObjectMaterialType(int index, const std::string& type)
{
    if (index < 0 || index >= static_cast<int>(m_scene.objects.size()))
    {
        return false;
    }
    const std::string normalized =
        (type == "Mirror" || type == "Glass" || type == "Microfacet") ? type : "Lambertian";
    SceneModel::ObjectNode& o = m_scene.objects[static_cast<std::size_t>(index)];
    SceneModel::Material* mat = m_scene.findMaterialMutable(o.materialName);
    if (!mat)
    {
        // No material yet (e.g. a light): create one and assign it.
        o.materialName = ensureDefaultMaterial(normalized, glm::vec3(0.8f));
    }
    else
    {
        mat->type = normalized;
    }
    m_scene.dirty = true;
    buildSceneGl();
    return true;
}

bool EditorApp::setObjectMaterialName(int index, const std::string& materialName)
{
    if (index < 0 || index >= static_cast<int>(m_scene.objects.size()))
    {
        return false;
    }
    if (m_scene.findMaterial(materialName) == nullptr)
    {
        return false;
    }
    m_scene.objects[static_cast<std::size_t>(index)].materialName = materialName;
    m_scene.dirty = true;
    buildSceneGl();
    return true;
}

bool EditorApp::setObjectMeshFile(int index, const std::string& meshFile)
{
    if (index < 0 || index >= static_cast<int>(m_scene.objects.size()))
    {
        return false;
    }
    if (m_scene.objects[static_cast<std::size_t>(index)].kind != SceneModel::Kind::MeshVolume)
    {
        return false;  // mesh binding only applies to MeshVolume objects
    }
    if (meshFile.empty())
    {
        return false;
    }

    // Resolve relative paths upwards from the working dir (matching how a
    // bundled "meshes/CornellBox.obj" is found), then store absolute so both the
    // viewport's loadObjShapeData and the serialized $meshes entry resolve.
    std::filesystem::path resolved(meshFile);
    if (!resolved.is_absolute())
    {
        std::filesystem::path found = findUpwards(meshFile);
        if (!found.empty())
        {
            resolved = found;
        }
    }
    if (!std::filesystem::exists(resolved))
    {
        return false;
    }
    const std::string abs = std::filesystem::absolute(resolved).string();

    bool have = false;
    for (const auto& f : m_scene.meshFiles)
    {
        if (f == abs) { have = true; break; }
    }
    if (!have) m_scene.meshFiles.push_back(abs);

    m_scene.dirty = true;
    buildSceneGl();
    return true;
}

bool EditorApp::setObjectMeshShape(int index, const std::string& shapeName)
{
    if (index < 0 || index >= static_cast<int>(m_scene.objects.size()))
    {
        return false;
    }
    SceneModel::ObjectNode& o = m_scene.objects[static_cast<std::size_t>(index)];
    if (o.kind != SceneModel::Kind::MeshVolume)
    {
        return false;  // sub-shape selection only applies to MeshVolume objects
    }
    if (shapeName.empty())
    {
        return false;
    }
    o.meshName = shapeName;
    m_scene.dirty = true;
    buildSceneGl();  // re-tessellate from the newly-selected sub-shape
    return true;
}

std::string EditorApp::loadSceneFromPath(const std::string& path)
{
    if (!std::filesystem::exists(path))
    {
        return "scene file not found: " + path;
    }

    SceneModel model;
    std::string err;
    if (!SceneModelLoader::loadFromFile(path, model, err))
    {
        return err;
    }

    // Adopt the parsed model, then (re)build viewport geometry from it.
    m_scene = std::move(model);
    m_scenePath = path;          // render-from-view re-serializes this scene
    m_selectedObject = -1;
    m_meshLabel = m_scene.name;

    buildSceneGl();

    // Frame the orbit camera on the whole scene's bounds so the box + knot + light
    // all sit in view from the orbit camera.
    glm::vec3 minB(std::numeric_limits<float>::max());
    glm::vec3 maxB(std::numeric_limits<float>::lowest());
    bool any = false;
    for (const auto& d : m_sceneDrawables)
    {
        if (!d.mesh || !d.mesh->hasMesh())
        {
            continue;
        }
        // The drawable's bounds are in object space; transform the 8 corners.
        const glm::vec3 lo = d.mesh->minBound();
        const glm::vec3 hi = d.mesh->maxBound();
        for (int c = 0; c < 8; ++c)
        {
            const glm::vec3 corner((c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y,
                                   (c & 4) ? hi.z : lo.z);
            const glm::vec3 w = glm::vec3(d.model * glm::vec4(corner, 1.0f));
            minB = glm::min(minB, w);
            maxB = glm::max(maxB, w);
            any = true;
        }
    }
    if (any)
    {
        m_camera.frameBounds(minB, maxB);
    }
    else
    {
        m_camera.frameOrigin();
    }

    return {};
}

nlohmann::json EditorApp::serializeWithLiveCamera()
{
    // Convention mapping (derived from src/Quaternion.cpp::fromPitchYawRoll and
    // the orbit eye() parameterization) — see startRender for the full rationale:
    //   render $position  = orbit eye()
    //   render yaw(deg)   = orbit yaw(deg) + 180
    //   render roll       = 0
    const glm::vec3 eye = m_camera.eye();
    const double renderPitchDeg = glm::degrees(m_camera.pitch);
    const double renderYawDeg = glm::degrees(m_camera.yaw) + 180.0;
    const double renderFovDeg = glm::degrees(m_camera.fovYRadians);

    json sceneJson = SceneModelSerializer::toJson(m_scene);
    json& sceneBlock = sceneJson["$scene"];

    // The serializer emits the model's camera (if any); from a File>New scene
    // there is none, so synthesize one. Either way the live orbit camera wins.
    bool wroteCamera = false;
    for (auto& [name, node] : sceneBlock.items())
    {
        if (node.is_object() && node.value("$type", std::string{}) == "Camera")
        {
            node["$position"] = {eye.x, eye.y, eye.z};
            node["$rotation"] = {{"$type", "PitchYawRollDegrees"},
                                 {"$value", {renderPitchDeg, renderYawDeg, 0.0}}};
            node["$verticalFieldOfView"] = renderFovDeg;
            wroteCamera = true;
            break;
        }
    }
    if (!wroteCamera)
    {
        sceneBlock["Camera"] = {
            {"$type", "Camera"},
            {"$verticalFieldOfView", renderFovDeg},
            {"$position", {eye.x, eye.y, eye.z}},
            {"$rotation",
             {{"$type", "PitchYawRollDegrees"},
              {"$value", {renderPitchDeg, renderYawDeg, 0.0}}}}};
    }
    return sceneJson;
}

std::string EditorApp::saveScene(const std::string& path)
{
    if (path.empty())
    {
        return "save path is empty";
    }
    if (!m_scene.isInitialized() || m_scene.isEmpty())
    {
        return "scene is empty — nothing to save";
    }

    json sceneJson = serializeWithLiveCamera();

    std::filesystem::path out(path);
    if (out.has_parent_path())
    {
        std::error_code ec;
        std::filesystem::create_directories(out.parent_path(), ec);
        // ec ignored: the ofstream open below reports the real failure.
    }
    std::ofstream f(out);
    if (!f)
    {
        return "could not open file for writing: " + path;
    }
    f << sceneJson.dump(2) << "\n";
    f.close();
    if (!f)
    {
        return "write failed: " + path;
    }

    m_scenePath = path;          // future render-from-view re-serializes here
    m_scene.path = path;
    m_scene.dirty = false;
    return {};
}

void EditorApp::buildSceneGl()
{
    // Release any previous scene geometry.
    for (auto& d : m_sceneDrawables)
    {
        if (d.gizmoVbo) glDeleteBuffers(1, &d.gizmoVbo);
        if (d.gizmoVao) glDeleteVertexArrays(1, &d.gizmoVao);
    }
    m_sceneDrawables.clear();

    for (std::size_t i = 0; i < m_scene.objects.size(); ++i)
    {
        const SceneModel::ObjectNode& obj = m_scene.objects[i];

        // Resolve the object's display color from its material (lights glow,
        // groups/unknowns are skipped for drawing but still listed in explorer).
        glm::vec3 color(0.75f);
        if (const SceneModel::Material* mat = m_scene.findMaterial(obj.materialName))
        {
            color = mat->color;
            if (mat->type == "Mirror")
            {
                color = glm::vec3(0.6f, 0.7f, 0.85f);  // tint so mirror reads as metal
            }
            else if (mat->type == "Glass")
            {
                color = glm::vec3(0.7f, 0.85f, 0.9f);
            }
        }

        if (obj.kind == SceneModel::Kind::MeshVolume)
        {
            // Find the mesh sub-shape across the scene's OBJ files.
            MeshData data;
            data.valid = false;
            for (const auto& objFile : m_scene.meshFiles)
            {
                MeshData candidate = loadObjShapeData(objFile, obj.meshName);
                if (candidate.valid)
                {
                    data = std::move(candidate);
                    break;
                }
            }
            if (!data.valid)
            {
                continue;  // mesh not found; listed in explorer, just not drawn
            }
            SceneDrawable d;
            d.mesh = std::make_unique<RasterMesh>();
            d.mesh->upload(data);
            d.model = objectModelMatrix(obj.position, obj.eulerDegrees, obj.scale);
            d.color = color;
            d.objectIndex = i;
            m_sceneDrawables.push_back(std::move(d));
        }
        else if (obj.kind == SceneModel::Kind::SphereVolume)
        {
            // Sphere proxy: tessellate at the local center/radius; the model
            // matrix places it (the renderer applies $position on top of $center).
            MeshData data = makeSphereMesh(obj.center, static_cast<float>(obj.radius));
            SceneDrawable d;
            d.mesh = std::make_unique<RasterMesh>();
            d.mesh->upload(data);
            d.model = objectModelMatrix(obj.position, obj.eulerDegrees, obj.scale);
            d.color = color;
            d.objectIndex = i;
            m_sceneDrawables.push_back(std::move(d));
        }
        else if (obj.kind == SceneModel::Kind::AreaLight ||
                 obj.kind == SceneModel::Kind::OmniLight)
        {
            // Light gizmo: a wireframe quad (area light) or a small wire octahedron
            // (omni). Built in object/local space and placed by the model matrix.
            std::vector<float> verts;  // interleaved pos(3) + color(3)
            const glm::vec3 gizmoColor(1.0f, 0.95f, 0.4f);
            auto addLine = [&](const glm::vec3& a, const glm::vec3& b) {
                verts.insert(verts.end(), {a.x, a.y, a.z, gizmoColor.r, gizmoColor.g, gizmoColor.b});
                verts.insert(verts.end(), {b.x, b.y, b.z, gizmoColor.r, gizmoColor.g, gizmoColor.b});
            };

            if (obj.kind == SceneModel::Kind::AreaLight)
            {
                const float hw = static_cast<float>(obj.lightWidth) * 0.5f;
                const float hh = static_cast<float>(obj.lightHeight) * 0.5f;
                // Quad in the local XY plane (the renderer's area light spans
                // right/up; the object's rotation orients it).
                const glm::vec3 p0(-hw, -hh, 0.0f), p1(hw, -hh, 0.0f), p2(hw, hh, 0.0f),
                    p3(-hw, hh, 0.0f);
                addLine(p0, p1);
                addLine(p1, p2);
                addLine(p2, p3);
                addLine(p3, p0);
                // Diagonals so a filled-looking marker is visible.
                addLine(p0, p2);
                addLine(p1, p3);
            }
            else
            {
                const float r = 8.0f;  // fixed gizmo size for omni
                const glm::vec3 px(r, 0, 0), nx(-r, 0, 0), py(0, r, 0), ny(0, -r, 0),
                    pz(0, 0, r), nz(0, 0, -r);
                addLine(px, py); addLine(py, nx); addLine(nx, ny); addLine(ny, px);
                addLine(px, pz); addLine(pz, nx); addLine(nx, nz); addLine(nz, px);
                addLine(py, pz); addLine(pz, ny); addLine(ny, nz); addLine(nz, py);
            }

            SceneDrawable d;
            d.isLight = true;
            d.objectIndex = i;
            d.model = objectModelMatrix(obj.position, obj.eulerDegrees, obj.scale);
            d.gizmoVertexCount = verts.size() / 6;

            glGenVertexArrays(1, &d.gizmoVao);
            glGenBuffers(1, &d.gizmoVbo);
            glBindVertexArray(d.gizmoVao);
            glBindBuffer(GL_ARRAY_BUFFER, d.gizmoVbo);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data(),
                         GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                                  reinterpret_cast<void*>(0));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                                  reinterpret_cast<void*>(3 * sizeof(float)));
            glBindVertexArray(0);

            m_sceneDrawables.push_back(std::move(d));
        }
        // Groups / Other: listed in the explorer but draw nothing.
    }
}

void EditorApp::drawSceneObjects(const glm::mat4& view, const glm::mat4& proj)
{
    // Shaded meshes + sphere proxies.
    if (m_shaderProgram)
    {
        glUseProgram(m_shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "uView"), 1, GL_FALSE,
                           glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "uProjection"), 1, GL_FALSE,
                           glm::value_ptr(proj));
        const glm::vec3 lightDir = glm::normalize(m_camera.target - m_camera.eye());
        glUniform3fv(glGetUniformLocation(m_shaderProgram, "uLightDir"), 1,
                     glm::value_ptr(lightDir));

        for (const auto& d : m_sceneDrawables)
        {
            if (!d.mesh || !d.mesh->hasMesh())
            {
                continue;
            }
            glm::vec3 color = d.color;
            // Highlight the selected object.
            if (m_selectedObject >= 0 &&
                d.objectIndex == static_cast<std::size_t>(m_selectedObject))
            {
                color = glm::mix(color, glm::vec3(1.0f, 0.55f, 0.1f), 0.6f);
            }
            glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "uModel"), 1, GL_FALSE,
                               glm::value_ptr(d.model));
            glUniform3fv(glGetUniformLocation(m_shaderProgram, "uBaseColor"), 1,
                         glm::value_ptr(color));
            d.mesh->draw();
        }
        glUseProgram(0);
    }

    // Light gizmos (line shader). The line shader has no uModel, so bake the
    // model transform into a combined view matrix (view * model) per gizmo.
    if (m_lineProgram)
    {
        glUseProgram(m_lineProgram);
        glUniformMatrix4fv(glGetUniformLocation(m_lineProgram, "uProjection"), 1, GL_FALSE,
                           glm::value_ptr(proj));
        glDisable(GL_DEPTH_TEST);
        for (const auto& d : m_sceneDrawables)
        {
            if (!d.isLight || d.gizmoVao == 0)
            {
                continue;
            }
            const glm::mat4 vm = view * d.model;
            glUniformMatrix4fv(glGetUniformLocation(m_lineProgram, "uView"), 1, GL_FALSE,
                               glm::value_ptr(vm));
            glBindVertexArray(d.gizmoVao);
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(d.gizmoVertexCount));
        }
        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);
        glUseProgram(0);
    }
}

void EditorApp::resizeFbo(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }
    if (width == m_fboWidth && height == m_fboHeight && m_fbo != 0)
    {
        return;
    }

    if (m_fbo)
    {
        glDeleteFramebuffers(1, &m_fbo);
        glDeleteTextures(1, &m_fboColorTex);
        glDeleteRenderbuffers(1, &m_fboDepthRbo);
        m_fbo = m_fboColorTex = m_fboDepthRbo = 0;
    }

    m_fboWidth = width;
    m_fboHeight = height;

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_fboColorTex);
    glBindTexture(GL_TEXTURE_2D, m_fboColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fboColorTex, 0);

    glGenRenderbuffers(1, &m_fboDepthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_fboDepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_fboDepthRbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        std::fprintf(stderr, "Viewport FBO incomplete\n");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void EditorApp::renderViewport()
{
    if (m_fbo == 0)
    {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_fboWidth, m_fboHeight);
    glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspectGrid = static_cast<float>(m_fboWidth) / static_cast<float>(m_fboHeight);
    const glm::mat4 viewMat = m_camera.viewMatrix();
    const glm::mat4 projMat = m_camera.projectionMatrix(aspectGrid);

    // Spatial reference overlay: the XZ ground grid (depth-tested so the scene
    // can occlude it once objects exist) and the origin gnomon (drawn over the
    // top with depth-test off so it's always legible). This renders for the
    // empty scene too — it's the substrate the next waves build on.
    if (m_lineProgram && m_grid.isBuilt())
    {
        glUseProgram(m_lineProgram);
        glUniformMatrix4fv(glGetUniformLocation(m_lineProgram, "uView"), 1, GL_FALSE,
                           glm::value_ptr(viewMat));
        glUniformMatrix4fv(glGetUniformLocation(m_lineProgram, "uProjection"), 1, GL_FALSE,
                           glm::value_ptr(projMat));

        m_grid.drawGrid();

        glDisable(GL_DEPTH_TEST);
        m_grid.drawGnomon();
        glEnable(GL_DEPTH_TEST);

        glUseProgram(0);
    }

    // Draw the in-memory scene's objects (meshes, sphere proxies, light gizmos).
    drawSceneObjects(viewMat, projMat);

    if (m_mesh.hasMesh() && m_shaderProgram)
    {
        glUseProgram(m_shaderProgram);

        const float aspect = static_cast<float>(m_fboWidth) / static_cast<float>(m_fboHeight);
        const glm::mat4 model(1.0f);
        const glm::mat4 view = m_camera.viewMatrix();
        const glm::mat4 proj = m_camera.projectionMatrix(aspect);

        glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "uView"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "uProjection"), 1, GL_FALSE, glm::value_ptr(proj));
        glUniform3fv(glGetUniformLocation(m_shaderProgram, "uBaseColor"), 1, m_meshColor);

        // Headlight: light comes from the camera's eye toward the target.
        glm::vec3 lightDir = glm::normalize(m_camera.target - m_camera.eye());
        glUniform3fv(glGetUniformLocation(m_shaderProgram, "uLightDir"), 1, glm::value_ptr(lightDir));

        m_mesh.draw();
        glUseProgram(0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void EditorApp::drawMenuBar()
{
    // Top menu bar. Wave 1 wires File > New (creates an empty scene + viewport).
    // Save / Insert are stubbed as disabled — they land in later waves (save
    // serializes m_scene to the renderer JSON; insert populates m_scene.objects).
    if (ImGui::BeginMainMenuBar())
    {
        // Record the whole menu bar rect so an agent can find it.
        m_layout.record("menu_bar", ImGui::GetWindowPos().x, ImGui::GetWindowPos().y,
                        ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);

        // Records the clickable rect of the menu-bar header item that was just
        // submitted (by BeginMenu). The header is laid out whether or not its
        // popup is open, so this captures a stable, queryable rect every frame.
        auto recordMenuHeader = [this](const char* name) {
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mx = ImGui::GetItemRectMax();
            m_layout.record(std::string("menu_") + name, mn.x, mn.y, mx.x - mn.x, mx.y - mn.y);
        };

        const bool fileOpen = ImGui::BeginMenu("File");
        recordMenuHeader("File");
        if (fileOpen)
        {
            if (ImGui::MenuItem("New", "Cmd+N"))
            {
                newScene();
            }
            // Open the bundled Cornell-box mirror-knot proof scene. (A native
            // file picker isn't wired in this environment; automation loads
            // arbitrary scenes via the load_scene command. This convenience item
            // opens the canonical scene so the menu path is exercised.)
            if (ImGui::MenuItem("Open CornellBoxMirrorKnot", "Cmd+O"))
            {
                std::filesystem::path scene = findUpwards("CornellBoxMirrorKnot.json");
                if (!scene.empty())
                {
                    std::string err = loadSceneFromPath(scene.string());
                    if (!err.empty())
                    {
                        std::fprintf(stderr, "Open scene failed: %s\n", err.c_str());
                    }
                }
            }
            ImGui::Separator();
            // Save serializes the in-memory model to renderer scene JSON via the
            // same saveScene() path the save_scene automation command uses. With
            // no native file picker in this environment, Save writes to the
            // scene's existing path (Open/load_scene), or to "untitled_scene.json"
            // in the working dir for a from-scratch (File > New) scene. Disabled
            // only when the scene is empty (nothing to serialize).
            const bool canSave = m_scene.isInitialized() && !m_scene.isEmpty();
            ImGui::BeginDisabled(!canSave);
            if (ImGui::MenuItem("Save", "Cmd+S"))
            {
                const std::string target =
                    !m_scenePath.empty() ? m_scenePath : std::string("untitled_scene.json");
                std::string err = saveScene(target);
                if (!err.empty())
                {
                    std::fprintf(stderr, "Save failed: %s\n", err.c_str());
                }
                else
                {
                    std::fprintf(stderr, "Saved scene to %s\n", target.c_str());
                }
            }
            ImGui::EndDisabled();
            ImGui::Separator();
            if (ImGui::MenuItem("Quit"))
            {
                glfwSetWindowShouldClose(m_window, GLFW_TRUE);
            }
            ImGui::EndMenu();
        }

        const bool insertOpen = ImGui::BeginMenu("Insert");
        recordMenuHeader("Insert");
        if (insertOpen)
        {
            // Each item inserts a new object into the model via the shared
            // insertObject() path, which builds its viewport geometry and selects
            // it. The same path backs the explorer right-click menu and the
            // insert_object puppet command.
            // Record each item's rect (only valid while the popup is open) so a
            // puppet can open the menu, then click an item's center across frames.
            auto recordMenuItem = [this](const char* name) {
                const ImVec2 mn = ImGui::GetItemRectMin();
                const ImVec2 mx = ImGui::GetItemRectMax();
                m_layout.record(std::string("menu_insert_") + name, mn.x, mn.y, mx.x - mn.x,
                                mx.y - mn.y);
            };
            if (ImGui::MenuItem("Sphere"))
            {
                insertObject(SceneModel::Kind::SphereVolume);
            }
            recordMenuItem("sphere");
            if (ImGui::MenuItem("Mesh"))
            {
                insertObject(SceneModel::Kind::MeshVolume);
            }
            recordMenuItem("mesh");
            if (ImGui::MenuItem("Area Light"))
            {
                insertObject(SceneModel::Kind::AreaLight);
            }
            recordMenuItem("area_light");
            if (ImGui::MenuItem("Omni Light"))
            {
                insertObject(SceneModel::Kind::OmniLight);
            }
            recordMenuItem("omni_light");
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

namespace
{
const char* kindLabel(SceneModel::Kind k)
{
    switch (k)
    {
        case SceneModel::Kind::SphereVolume: return "Sphere";
        case SceneModel::Kind::MeshVolume: return "Mesh";
        case SceneModel::Kind::AreaLight: return "AreaLight";
        case SceneModel::Kind::OmniLight: return "OmniLight";
        case SceneModel::Kind::Group: return "Group";
        case SceneModel::Kind::Other: return "Object";
    }
    return "Object";
}
}  // namespace

void EditorApp::drawSceneExplorer()
{
    ImGui::Text("Scene Explorer");

    // Wrap the list in a child region so it scrolls when the scene is large, and
    // so the panel rect we register is the list's own region.
    ImGui::BeginChild("scene_explorer", ImVec2(0, 220), true);
    {
        // The child window's own rect (its scroll region) — the clickable panel
        // an agent targets. GetWindowPos/Size inside the child report exactly that.
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        m_layout.record("panel_explorer", wp.x, wp.y, ws.x, ws.y);
    }

    // Camera row first (always present once a scene is loaded).
    if (m_scene.camera.present)
    {
        ImGui::Selectable(("[Camera] " + m_scene.camera.name).c_str(), false);
        const ImVec2 mn = ImGui::GetItemRectMin();
        const ImVec2 mx = ImGui::GetItemRectMax();
        m_layout.record("explorer_row_camera", mn.x, mn.y, mx.x - mn.x, mx.y - mn.y);
    }

    // One selectable row per object. The row label carries the kind so an agent
    // (and a human) can tell walls from the knot from the light.
    for (std::size_t i = 0; i < m_scene.objects.size(); ++i)
    {
        const SceneModel::ObjectNode& obj = m_scene.objects[i];
        const bool selected = (m_selectedObject == static_cast<int>(i));
        ImGui::PushID(static_cast<int>(i));
        const std::string label =
            std::string("[") + kindLabel(obj.kind) + "] " + obj.name;
        if (ImGui::Selectable(label.c_str(), selected))
        {
            m_selectedObject = static_cast<int>(i);
        }
        // Register each row's pixel rect by name so the puppet port can click it.
        const ImVec2 mn = ImGui::GetItemRectMin();
        const ImVec2 mx = ImGui::GetItemRectMax();
        m_layout.record("explorer_row_" + obj.name, mn.x, mn.y, mx.x - mn.x, mx.y - mn.y);
        m_layout.record("explorer_row_index_" + std::to_string(i), mn.x, mn.y, mx.x - mn.x,
                        mx.y - mn.y);
        ImGui::PopID();
    }

    if (m_scene.objects.empty() && !m_scene.camera.present)
    {
        ImGui::TextDisabled("(empty scene — Insert > ... or right-click)");
    }

    // Right-click anywhere in the explorer region opens an Insert context menu,
    // routing through the same insertObject() path as the Insert menu. ImGui's
    // BeginPopupContextWindow targets the current child window's empty space.
    if (ImGui::BeginPopupContextWindow("explorer_context",
                                       ImGuiPopupFlags_MouseButtonRight |
                                           ImGuiPopupFlags_NoOpenOverItems))
    {
        ImGui::TextDisabled("Insert");
        ImGui::Separator();
        if (ImGui::MenuItem("Sphere")) insertObject(SceneModel::Kind::SphereVolume);
        if (ImGui::MenuItem("Mesh")) insertObject(SceneModel::Kind::MeshVolume);
        if (ImGui::MenuItem("Area Light")) insertObject(SceneModel::Kind::AreaLight);
        if (ImGui::MenuItem("Omni Light")) insertObject(SceneModel::Kind::OmniLight);
        ImGui::EndPopup();
    }

    ImGui::EndChild();

    if (m_selectedObject >= 0 && m_selectedObject < static_cast<int>(m_scene.objects.size()))
    {
        const SceneModel::ObjectNode& sel = m_scene.objects[m_selectedObject];
        ImGui::Text("Selected: %s (%s)", sel.name.c_str(), kindLabel(sel.kind));
    }
    else
    {
        ImGui::TextDisabled("Selected: (none)");
    }
}

namespace
{
// Record the rect of the widget just submitted under `name` so the puppet port
// can locate it. ImGui's GetItemRectMin/Max report screen-space (window-pixel)
// coordinates, matching the LayoutRegistry / injected-input space.
void recordItemRect(LayoutRegistry& layout, const char* name)
{
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    layout.record(name, mn.x, mn.y, mx.x - mn.x, mx.y - mn.y);
}

const char* materialTypeItems[] = {"Lambertian", "Mirror", "Glass", "Microfacet"};
int materialTypeIndex(const std::string& t)
{
    if (t == "Mirror") return 1;
    if (t == "Glass") return 2;
    if (t == "Microfacet") return 3;
    return 0;
}
}  // namespace

void EditorApp::drawPropertiesPanel()
{
    ImGui::Separator();
    ImGui::Text("Properties");

    ImGui::BeginChild("properties_panel", ImVec2(0, 300), true);
    {
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        m_layout.record("panel_properties", wp.x, wp.y, ws.x, ws.y);
    }

    if (m_selectedObject < 0 || m_selectedObject >= static_cast<int>(m_scene.objects.size()))
    {
        ImGui::TextDisabled("(select an object to edit)");
        ImGui::EndChild();
        return;
    }

    SceneModel::ObjectNode& obj = m_scene.objects[static_cast<std::size_t>(m_selectedObject)];
    ImGui::Text("%s  [%s]", obj.name.c_str(), kindLabel(obj.kind));
    ImGui::Separator();

    // ----- Transform: position / rotation / scale --------------------------
    // Each component is its own DragFloat so the puppet can register/find a rect
    // per axis. Edits flow through setObjectFloatField (the same path the
    // set_property command uses), so GUI + puppet mutate the model identically.
    auto dragField = [&](const char* label, const char* layoutName, const char* field,
                         float current, float speed) {
        float v = current;
        ImGui::SetNextItemWidth(90.0f);
        const bool changed = ImGui::DragFloat(label, &v, speed);
        recordItemRect(m_layout, layoutName);
        if (changed)
        {
            setObjectFloatField(m_selectedObject, field, v);
        }
    };

    ImGui::TextDisabled("Position");
    dragField("X##pos", "prop_pos_x", "pos_x", obj.position.x, 0.05f); ImGui::SameLine();
    dragField("Y##pos", "prop_pos_y", "pos_y", obj.position.y, 0.05f); ImGui::SameLine();
    dragField("Z##pos", "prop_pos_z", "pos_z", obj.position.z, 0.05f);

    ImGui::TextDisabled("Orientation (pitch/yaw/roll deg)");
    dragField("P##rot", "prop_rot_x", "rot_x", obj.eulerDegrees.x, 0.5f); ImGui::SameLine();
    dragField("Yw##rot", "prop_rot_y", "rot_y", obj.eulerDegrees.y, 0.5f); ImGui::SameLine();
    dragField("R##rot", "prop_rot_z", "rot_z", obj.eulerDegrees.z, 0.5f);

    ImGui::TextDisabled("Scale");
    dragField("X##scale", "prop_scale_x", "scale_x", obj.scale.x, 0.01f); ImGui::SameLine();
    dragField("Y##scale", "prop_scale_y", "scale_y", obj.scale.y, 0.01f); ImGui::SameLine();
    dragField("Z##scale", "prop_scale_z", "scale_z", obj.scale.z, 0.01f);

    // ----- Kind-specific fields -------------------------------------------
    if (obj.kind == SceneModel::Kind::SphereVolume)
    {
        ImGui::Separator();
        float r = static_cast<float>(obj.radius);
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragFloat("Radius", &r, 0.02f, 0.0f, 1e6f))
        {
            setObjectFloatField(m_selectedObject, "radius", r);
        }
        recordItemRect(m_layout, "prop_radius");
    }
    else if (obj.kind == SceneModel::Kind::AreaLight)
    {
        ImGui::Separator();
        if (obj.lightShape == SceneModel::LightShape::Disc)
        {
            float lr = static_cast<float>(obj.lightRadius);
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::DragFloat("Light radius", &lr, 0.05f, 0.0f, 1e6f))
            {
                setObjectFloatField(m_selectedObject, "light_radius", lr);
            }
            recordItemRect(m_layout, "prop_light_radius");
        }
        else
        {
            float w = static_cast<float>(obj.lightWidth);
            float h = static_cast<float>(obj.lightHeight);
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::DragFloat("W##lw", &w, 0.05f, 0.0f, 1e6f))
            {
                setObjectFloatField(m_selectedObject, "light_width", w);
            }
            recordItemRect(m_layout, "prop_light_width");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::DragFloat("H##lh", &h, 0.05f, 0.0f, 1e6f))
            {
                setObjectFloatField(m_selectedObject, "light_height", h);
            }
            recordItemRect(m_layout, "prop_light_height");
        }
    }

    // ----- Material -------------------------------------------------------
    const bool hasMaterialSlot = (obj.kind == SceneModel::Kind::SphereVolume ||
                                  obj.kind == SceneModel::Kind::MeshVolume);
    if (hasMaterialSlot)
    {
        ImGui::Separator();
        ImGui::TextDisabled("Material: %s",
                            obj.materialName.empty() ? "(none)" : obj.materialName.c_str());

        SceneModel::Material* mat = m_scene.findMaterialMutable(obj.materialName);
        if (mat)
        {
            int typeIdx = materialTypeIndex(mat->type);
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::Combo("Type", &typeIdx, materialTypeItems,
                             IM_ARRAYSIZE(materialTypeItems)))
            {
                setObjectMaterialType(m_selectedObject, materialTypeItems[typeIdx]);
            }
            recordItemRect(m_layout, "prop_material_type");

            float color[3] = {mat->color.r, mat->color.g, mat->color.b};
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::ColorEdit3("Color", color))
            {
                setObjectFloatField(m_selectedObject, "mat_color_r", color[0]);
                setObjectFloatField(m_selectedObject, "mat_color_g", color[1]);
                setObjectFloatField(m_selectedObject, "mat_color_b", color[2]);
            }
            recordItemRect(m_layout, "prop_material_color");

            if (mat->type == "Glass")
            {
                float ior = static_cast<float>(mat->ior);
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::DragFloat("IOR", &ior, 0.01f, 1.0f, 3.0f))
                {
                    setObjectFloatField(m_selectedObject, "mat_ior", ior);
                }
                recordItemRect(m_layout, "prop_material_ior");
            }
        }
    }

    ImGui::EndChild();
}

void EditorApp::drawUi()
{
    // Reset the per-frame element-rect registry before any UI is built. Each
    // panel/menu records its rect as it's submitted; query_layout reads the
    // result. (See LayoutRegistry.h / recordLayout().)
    m_layout.beginFrame();

    drawMenuBar();

    // Give each window an explicit, non-overlapping initial layout. Without this
    // all three windows default to (0,0) and stack on top of each other: the
    // Viewport window (which shows the live FBO) ends up hidden behind the
    // controls panel — so the window looks like a black viewport — and the
    // Render window gets crushed to a sliver on the left edge, which makes its
    // help text wrap one character per line. FirstUseEver lets the user freely
    // move/resize afterwards (and respects any saved imgui.ini layout).
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    // Reserve the main menu bar height in the windows' initial Y so they never
    // overlap (and thus never steal hover from) the menu bar. mainViewport->WorkPos
    // already excludes the menu bar AFTER its first submitted frame, but the
    // FirstUseEver positions below are snapshotted on frame 0 — when WorkPos.y is
    // still 0 because the menu bar height isn't known yet. Without this, the
    // controls window pins itself at y=0, covering the menu bar and making
    // injected menu clicks impossible. GetFrameHeight() is the menu bar's height.
    const float menuBarH = ImGui::GetFrameHeight();
    ImVec2 origin = mainViewport->WorkPos;
    ImVec2 size = mainViewport->WorkSize;
    if (origin.y < menuBarH)
    {
        const float delta = menuBarH - origin.y;
        origin.y = menuBarH;
        size.y -= delta;
    }
    const float controlsWidth = 360.0f;
    const float renderHeight = 260.0f;

    ImGui::SetNextWindowPos(origin, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(controlsWidth, size.y), ImGuiCond_FirstUseEver);

    // Controls + scene panel.
    ImGui::Begin("Ray Tracer Editor");
    m_layout.record("panel_controls", ImGui::GetWindowPos().x, ImGui::GetWindowPos().y,
                    ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
    ImGui::TextWrapped("GUI model/scene editor for the photon path tracer.");
    ImGui::Separator();
    ImGui::Text("Scene: %s%s", m_scene.name.c_str(), m_scene.dirty ? " *" : "");
    ImGui::Text("Objects: %zu   Materials: %zu", m_scene.objects.size(),
                m_scene.materials.size());
    ImGui::TextWrapped("Viewport: orbit = left-drag, pan = middle-drag or "
                       "shift+left-drag, zoom = scroll");
    ImGui::Separator();

    // Scene explorer: the tree of named objects + the camera. Selecting a row
    // sets m_selectedObject (highlights it in the viewport; drives the properties
    // panel next wave). Each row registers its pixel rect in the LayoutRegistry.
    drawSceneExplorer();

    // Properties panel: editable transform / kind fields / material for the
    // selected object. Lives in the controls window beneath the explorer.
    drawPropertiesPanel();
    ImGui::Separator();

    ImGui::Text("Scene file: %s",
                m_scenePath.empty() ? "(MirrorTest.json not found)" : m_scenePath.c_str());
    ImGui::SliderInt("Render resolution", &m_renderResolution, 64, 720);
    ImGui::SliderInt("Photons (millions)", &m_renderPhotonsMillions, 1, 20);

    const RenderState state = m_renderState.load();
    const bool running = (state == RenderState::Running);

    if (running)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Render"))
    {
        startRender();
    }
    // Record the Render button's rect (useful for click-driven validation of the
    // controls panel; the next wave's buttons register the same way).
    {
        const ImVec2 mn = ImGui::GetItemRectMin();
        const ImVec2 mx = ImGui::GetItemRectMax();
        m_layout.record("button_render", mn.x, mn.y, mx.x - mn.x, mx.y - mn.y);
    }
    if (running)
    {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("Rendering...");
    }

    ImGui::TextWrapped("Status: %s", m_renderStatus.c_str());
    if (state == RenderState::Failed && !m_renderError.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", m_renderError.c_str());
    }
    ImGui::End();

    // Raster viewport panel — fills the area to the right of the controls,
    // leaving room for the Render panel at the bottom.
    ImGui::SetNextWindowPos(ImVec2(origin.x + controlsWidth, origin.y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(size.x - controlsWidth, size.y - renderHeight),
                             ImGuiCond_FirstUseEver);

    // Raster viewport panel.
    ImGui::Begin("Viewport");
    {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        int w = static_cast<int>(avail.x);
        int h = static_cast<int>(avail.y);
        if (w > 16 && h > 16)
        {
            resizeFbo(w, h);
            m_cursorOverViewport = ImGui::IsWindowHovered();
            // FBO texture is bottom-left origin; flip V so it displays upright.
            ImGui::Image(
                static_cast<ImTextureID>(static_cast<intptr_t>(m_fboColorTex)),
                ImVec2(static_cast<float>(m_fboWidth), static_cast<float>(m_fboHeight)),
                ImVec2(0, 1), ImVec2(1, 0));

            // Record the viewport IMAGE rect (the clickable/draggable region) in
            // window pixels. This is both the query_layout "viewport" rect an
            // agent clicks into AND the gate the nav handlers use to decide
            // whether a press/scroll counts as a viewport gesture — so injected
            // and real input are gated against the same rect.
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mx = ImGui::GetItemRectMax();
            m_viewportScreenRect = LayoutRect{mn.x, mn.y, mx.x - mn.x, mx.y - mn.y};
            m_layout.record("viewport", m_viewportScreenRect);
        }
    }
    ImGui::End();

    // Path-traced render output panel — sits beneath the viewport, wide enough
    // that its help text wraps normally instead of one character per line.
    ImGui::SetNextWindowPos(ImVec2(origin.x + controlsWidth, origin.y + size.y - renderHeight),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(size.x - controlsWidth, renderHeight), ImGuiCond_FirstUseEver);

    // Path-traced render output panel.
    ImGui::Begin("Render");
    m_layout.record("panel_render", ImGui::GetWindowPos().x, ImGui::GetWindowPos().y,
                    ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
    if (m_renderTex != 0 && m_renderWidth > 0)
    {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float scale = avail.x / static_cast<float>(m_renderWidth);
        if (scale <= 0.0f || scale > 4.0f) scale = 1.0f;
        ImGui::Image(
            static_cast<ImTextureID>(static_cast<intptr_t>(m_renderTex)),
            ImVec2(m_renderWidth * scale, m_renderHeight * scale));
    }
    else
    {
        ImGui::TextWrapped("Click Render to path-trace the scene. The result appears here.");
    }
    ImGui::End();
}

void EditorApp::startRender()
{
    if (m_renderState.load() == RenderState::Running)
    {
        return;
    }
    if (m_scene.objects.empty())
    {
        m_renderState.store(RenderState::Failed);
        m_renderError = "Scene is empty — insert an object before rendering.";
        m_renderStatus = "Render failed.";
        return;
    }

    if (m_renderThread.joinable())
    {
        m_renderThread.join();
    }

    m_renderState.store(RenderState::Running);
    m_renderStatus = "Rendering scene (this can take a while)...";
    m_renderError.clear();

    const int resolution = m_renderResolution;
    const size_t photons = static_cast<size_t>(m_renderPhotonsMillions) * 1000000;

    // RENDER-FROM-CURRENT-VIEW: serialize the CURRENT in-memory model (so inserts
    // and edits made in the editor are in the rendered scene — not just the
    // originally-loaded rawJson), then overwrite the Camera block's $position /
    // $rotation / $verticalFieldOfView with the LIVE orbit camera so the
    // path-trace frames the scene exactly as the viewport shows it.
    //
    // Convention mapping (derived from src/Quaternion.cpp::fromPitchYawRoll and
    // the orbit eye() parameterization):
    //   render $position  = orbit eye()
    //   render pitch(deg) = orbit pitch(deg)
    //   render yaw(deg)   = orbit yaw(deg) + 180   (orbit eye is BEHIND the target
    //                       along -forward; the renderer looks down +Z, so the yaw
    //                       is flipped by 180 to point eye -> target)
    //   render roll       = 0
    //   render $verticalFieldOfView = orbit fovY(deg)
    // These are read on the main thread (camera state) and baked into the temp
    // scene file the worker thread renders.
    json sceneJson = serializeWithLiveCamera();

    // Write the rewritten scene to a temp file. $meshes are absolute paths (the
    // model resolves them at load/insert time), so the temp file's directory does
    // not matter for mesh resolution; place it next to the source scene when one
    // exists, else in the system temp dir.
    std::filesystem::path tempPath;
    if (!m_scenePath.empty())
    {
        std::filesystem::path base(m_scenePath);
        tempPath = base.parent_path() / (".editor_view_" + base.filename().string());
    }
    else
    {
        tempPath = std::filesystem::temp_directory_path() / ".editor_view_scene.json";
    }
    std::string renderScenePath;
    {
        std::ofstream out(tempPath);
        if (!out)
        {
            m_renderState.store(RenderState::Failed);
            m_renderError = "could not write temp scene: " + tempPath.string();
            m_renderStatus = "Render failed.";
            return;
        }
        out << sceneJson.dump(2);
        out.close();
        renderScenePath = tempPath.string();
        m_lastRenderScenePath = renderScenePath;
    }

    // TODO (Phase 4 — progressive preview, not yet wired): renderFrame already
    // accepts a Renderer::ProgressCallback and the Buffer is pollable mid-render.
    // To show accumulation, capture result.buffer here, and on a UI timer call
    // Renderer::tonemapBufferToImage on a snapshot + re-upload m_renderTex while
    // RenderState::Running. Camera moves would reset/restart the render. Left as
    // a clean follow-up rather than half-wired.
    m_renderThread = std::thread([this, renderScenePath, resolution, photons]() {
        try
        {
            LoadedScene scene = SceneLoader::loadFromFile(renderScenePath, /*logToStdout=*/false);
            // Override resolution + photon budget for an interactive-speed render.
            scene.settings.imageWidth = static_cast<size_t>(resolution);
            scene.settings.imageHeight = static_cast<size_t>(resolution);
            scene.settings.photonsPerLight = photons;
            if (scene.camera)
            {
                scene.camera->setFromRenderConfiguration(scene.settings.imageWidth, scene.settings.imageHeight);
            }

            RenderResult result = Renderer::renderFrame(scene);

            // Convert the 16-bit Image to 8-bit RGBA for GL upload. The Image is
            // already tonemapped + flipped to display orientation.
            const Image& image = *result.image;
            const int w = static_cast<int>(image.width());
            const int h = static_cast<int>(image.height());
            std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
            Image& mutableImage = const_cast<Image&>(image);
            for (int y = 0; y < h; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    const Pixel& p = mutableImage.getPixel(x, y);
                    const size_t i = (static_cast<size_t>(y) * w + x) * 4;
                    rgba[i + 0] = static_cast<uint8_t>(p.red >> 8);
                    rgba[i + 1] = static_cast<uint8_t>(p.green >> 8);
                    rgba[i + 2] = static_cast<uint8_t>(p.blue >> 8);
                    rgba[i + 3] = 255;
                }
            }

            m_renderRgba = std::move(rgba);
            m_renderWidth = w;
            m_renderHeight = h;
            m_renderTextureDirty.store(true);
            m_renderState.store(RenderState::Done);
        }
        catch (const std::exception& e)
        {
            m_renderError = e.what();
            m_renderState.store(RenderState::Failed);
        }
    });
}

void EditorApp::pollRender()
{
    const RenderState state = m_renderState.load();
    if (state == RenderState::Done)
    {
        if (m_renderTextureDirty.exchange(false))
        {
            uploadRenderTexture();
            m_renderStatus = "Render complete (" + std::to_string(m_renderWidth) + "x" +
                             std::to_string(m_renderHeight) + ").";
        }
    }
    else if (state == RenderState::Failed)
    {
        m_renderStatus = "Render failed.";
    }
}

void EditorApp::uploadRenderTexture()
{
    if (m_renderRgba.empty() || m_renderWidth <= 0 || m_renderHeight <= 0)
    {
        return;
    }

    if (m_renderTex == 0)
    {
        glGenTextures(1, &m_renderTex);
    }
    glBindTexture(GL_TEXTURE_2D, m_renderTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_renderWidth, m_renderHeight, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, m_renderRgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ===== Automation command handlers (main/GL thread) =======================

namespace
{
const char* renderStateName(EditorApp::RenderState s)
{
    switch (s)
    {
        case EditorApp::RenderState::Idle: return "idle";
        case EditorApp::RenderState::Running: return "rendering";
        case EditorApp::RenderState::Done: return "done";
        case EditorApp::RenderState::Failed: return "failed";
    }
    return "unknown";
}
}  // namespace

nlohmann::json EditorApp::handleCommand(const nlohmann::json& request)
{
    const std::string cmd = request.value("cmd", std::string{});
    if (cmd == "ping") return cmdPing(request);
    if (cmd == "get_state") return cmdGetState(request);
    if (cmd == "load_mesh") return cmdLoadMesh(request);
    if (cmd == "load_scene") return cmdLoadScene(request);
    if (cmd == "save_scene") return cmdSaveScene(request);
    if (cmd == "set_camera") return cmdSetCamera(request);
    if (cmd == "set_render_settings") return cmdSetRenderSettings(request);
    if (cmd == "render") return cmdRender(request);
    if (cmd == "screenshot") return cmdScreenshot(request);
    if (cmd == "inject_input") return cmdInjectInput(request);
    if (cmd == "play_input") return cmdPlayInput(request);
    if (cmd == "query_layout") return cmdQueryLayout(request);
    if (cmd == "insert_object") return cmdInsertObject(request);
    if (cmd == "set_property") return cmdSetProperty(request);
    if (cmd == "new_scene")
    {
        newScene();
        return json{{"ok", true}, {"object_count", m_scene.objects.size()}};
    }
    if (cmd == "quit")
    {
        if (m_automation) m_automation->requestQuit();
        return json{{"ok", true}, {"quitting", true}};
    }
    return json{{"ok", false}, {"error", "unknown command: " + cmd}};
}

nlohmann::json EditorApp::cmdPing(const nlohmann::json&)
{
    return json{{"ok", true}, {"pong", true}, {"version", "editor-automation/1"}};
}

nlohmann::json EditorApp::cmdGetState(const nlohmann::json&)
{
    const glm::vec3 eye = m_camera.eye();
    json j;
    j["ok"] = true;
    j["mesh_path"] = m_meshPath;
    j["mesh_label"] = m_meshLabel;
    j["camera"] = {
        {"eye", {eye.x, eye.y, eye.z}},
        {"target", {m_camera.target.x, m_camera.target.y, m_camera.target.z}},
        {"fov_y_degrees", glm::degrees(m_camera.fovYRadians)},
        {"yaw", m_camera.yaw},
        {"pitch", m_camera.pitch},
        {"distance", m_camera.distance},
    };
    j["render_settings"] = {
        {"resolution", m_renderResolution},
        {"photons_millions", m_renderPhotonsMillions},
        {"scene_path", m_scenePath},
    };
    j["render_status"] = renderStateName(m_renderState.load());
    j["render_message"] = m_renderStatus;
    j["render_width"] = m_renderWidth;
    j["render_height"] = m_renderHeight;
    j["viewport"] = {{"width", m_fboWidth}, {"height", m_fboHeight}};
    j["window"] = {{"width", m_lastWindowWidth}, {"height", m_lastWindowHeight}};

    // Scene model summary + current selection (so automation can verify the
    // explorer contents and which row is selected without screenshotting).
    json objectNames = json::array();
    for (const auto& obj : m_scene.objects)
    {
        objectNames.push_back(obj.name);
    }
    j["scene"] = {
        {"name", m_scene.name},
        {"path", m_scenePath},
        {"object_count", m_scene.objects.size()},
        {"material_count", m_scene.materials.size()},
        {"camera_present", m_scene.camera.present},
        {"objects", objectNames},
    };
    j["selected_index"] = m_selectedObject;
    j["selected_object"] =
        (m_selectedObject >= 0 && m_selectedObject < static_cast<int>(m_scene.objects.size()))
            ? json(m_scene.objects[static_cast<std::size_t>(m_selectedObject)].name)
            : json(nullptr);

    // Full detail of the selected object (transform + material) so a puppet can
    // verify an edit landed without screenshotting. objectDetailJson lives in the
    // command section below; forward-declared there, defined before use at link.
    if (m_selectedObject >= 0 && m_selectedObject < static_cast<int>(m_scene.objects.size()))
    {
        const SceneModel::ObjectNode& o =
            m_scene.objects[static_cast<std::size_t>(m_selectedObject)];
        json sd;
        sd["index"] = m_selectedObject;
        sd["name"] = o.name;
        sd["kind"] = kindLabel(o.kind);
        sd["position"] = {o.position.x, o.position.y, o.position.z};
        sd["rotation"] = {o.eulerDegrees.x, o.eulerDegrees.y, o.eulerDegrees.z};
        sd["scale"] = {o.scale.x, o.scale.y, o.scale.z};
        sd["radius"] = o.radius;
        sd["light_width"] = o.lightWidth;
        sd["light_height"] = o.lightHeight;
        sd["material_name"] = o.materialName;
        if (const SceneModel::Material* m = m_scene.findMaterial(o.materialName))
        {
            sd["material"] = {{"type", m->type},
                              {"color", {m->color.r, m->color.g, m->color.b}},
                              {"ior", m->ior}};
        }
        j["selected_detail"] = sd;
    }
    else
    {
        j["selected_detail"] = json(nullptr);
    }
    return j;
}

nlohmann::json EditorApp::cmdLoadMesh(const nlohmann::json& req)
{
    if (!req.contains("path") || !req["path"].is_string())
    {
        return json{{"ok", false}, {"error", "load_mesh requires string 'path'"}};
    }
    const std::string path = req["path"].get<std::string>();
    if (!std::filesystem::exists(path))
    {
        return json{{"ok", false}, {"error", "file not found: " + path}};
    }
    std::string err = loadMeshFromPath(path);
    if (!err.empty())
    {
        return json{{"ok", false}, {"error", err}};
    }
    return json{{"ok", true}, {"mesh_path", m_meshPath}, {"mesh_label", m_meshLabel}};
}

nlohmann::json EditorApp::cmdLoadScene(const nlohmann::json& req)
{
    if (!req.contains("path") || !req["path"].is_string())
    {
        return json{{"ok", false}, {"error", "load_scene requires string 'path'"}};
    }
    const std::string path = req["path"].get<std::string>();
    std::string err = loadSceneFromPath(path);
    if (!err.empty())
    {
        return json{{"ok", false}, {"error", err}};
    }

    // Summarize the loaded model so an agent can confirm the explorer contents.
    json objects = json::array();
    for (const auto& obj : m_scene.objects)
    {
        objects.push_back(obj.name);
    }
    return json{{"ok", true},
                {"scene_path", m_scenePath},
                {"name", m_scene.name},
                {"object_count", m_scene.objects.size()},
                {"material_count", m_scene.materials.size()},
                {"camera_present", m_scene.camera.present},
                {"objects", objects}};
}

nlohmann::json EditorApp::cmdSaveScene(const nlohmann::json& req)
{
    if (!req.contains("path") || !req["path"].is_string())
    {
        return json{{"ok", false}, {"error", "save_scene requires string 'path'"}};
    }
    const std::string path = req["path"].get<std::string>();
    std::string err = saveScene(path);
    if (!err.empty())
    {
        return json{{"ok", false}, {"error", err}};
    }
    return json{{"ok", true},
                {"scene_path", m_scenePath},
                {"object_count", m_scene.objects.size()},
                {"material_count", m_scene.materials.size()}};
}

nlohmann::json EditorApp::cmdSetCamera(const nlohmann::json& req)
{
    auto readVec3 = [](const json& arr, glm::vec3& out) -> bool {
        if (!arr.is_array() || arr.size() != 3) return false;
        out = glm::vec3(arr[0].get<float>(), arr[1].get<float>(), arr[2].get<float>());
        return true;
    };

    if (req.contains("target"))
    {
        glm::vec3 t;
        if (!readVec3(req["target"], t))
            return json{{"ok", false}, {"error", "target must be [x,y,z]"}};
        m_camera.target = t;
    }
    if (req.contains("eye"))
    {
        // Derive yaw/pitch/distance from an explicit eye relative to target so
        // the orbit parameterization stays consistent.
        glm::vec3 e;
        if (!readVec3(req["eye"], e))
            return json{{"ok", false}, {"error", "eye must be [x,y,z]"}};
        const glm::vec3 d = e - m_camera.target;
        const float dist = glm::length(d);
        if (dist > 1e-6f)
        {
            m_camera.distance = dist;
            m_camera.pitch = std::asin(glm::clamp(d.y / dist, -1.0f, 1.0f));
            m_camera.yaw = std::atan2(d.x, d.z);
            if (m_camera.pitch > OrbitCamera::kPitchLimit) m_camera.pitch = OrbitCamera::kPitchLimit;
            if (m_camera.pitch < -OrbitCamera::kPitchLimit) m_camera.pitch = -OrbitCamera::kPitchLimit;
        }
    }
    if (req.contains("fov"))
    {
        m_camera.fovYRadians = glm::radians(req["fov"].get<float>());
    }
    if (req.contains("orbit_yaw") || req.contains("orbit_pitch"))
    {
        const float dy = req.value("orbit_yaw", 0.0f);
        const float dp = req.value("orbit_pitch", 0.0f);
        m_camera.orbit(dy, dp);
    }
    if (req.contains("dolly"))
    {
        m_camera.dolly(req["dolly"].get<float>());
    }

    const glm::vec3 eye = m_camera.eye();
    return json{{"ok", true},
                {"camera", {{"eye", {eye.x, eye.y, eye.z}},
                            {"target", {m_camera.target.x, m_camera.target.y, m_camera.target.z}},
                            {"fov_y_degrees", glm::degrees(m_camera.fovYRadians)},
                            {"yaw", m_camera.yaw},
                            {"pitch", m_camera.pitch},
                            {"distance", m_camera.distance}}}};
}

nlohmann::json EditorApp::cmdSetRenderSettings(const nlohmann::json& req)
{
    if (req.contains("resolution"))
    {
        int r = req["resolution"].get<int>();
        if (r < 16 || r > 4096)
            return json{{"ok", false}, {"error", "resolution out of range [16,4096]"}};
        m_renderResolution = r;
    }
    if (req.contains("photons"))
    {
        // Accept photons in millions to match the UI slider semantics.
        int p = req["photons"].get<int>();
        if (p < 1) p = 1;
        m_renderPhotonsMillions = p;
    }
    if (req.contains("scene_path"))
    {
        m_scenePath = req["scene_path"].get<std::string>();
    }
    return json{{"ok", true},
                {"render_settings", {{"resolution", m_renderResolution},
                                     {"photons_millions", m_renderPhotonsMillions},
                                     {"scene_path", m_scenePath}}}};
}

nlohmann::json EditorApp::cmdRender(const nlohmann::json& req)
{
    if (m_scenePath.empty())
    {
        return json{{"ok", false}, {"error", "no scene file set (scene_path)"}};
    }
    if (m_renderState.load() == RenderState::Running)
    {
        return json{{"ok", false}, {"error", "render already in progress"}};
    }

    startRender();

    const bool wait = req.value("wait", true);
    if (!wait)
    {
        return json{{"ok", true}, {"status", "rendering"}, {"waited", false}};
    }

    const double timeout = req.value("timeout", 600.0);
    const bool finished = waitForRenderToFinish(timeout);
    const RenderState st = m_renderState.load();
    if (!finished)
    {
        return json{{"ok", false}, {"error", "render timed out"}, {"status", renderStateName(st)}};
    }
    if (st == RenderState::Failed)
    {
        return json{{"ok", false}, {"error", m_renderError}, {"status", "failed"}};
    }
    return json{{"ok", true},
                {"status", "done"},
                {"waited", true},
                {"render_scene_path", m_lastRenderScenePath},
                {"render_width", m_renderWidth},
                {"render_height", m_renderHeight}};
}

bool EditorApp::waitForRenderToFinish(double timeoutSeconds)
{
    const auto start = std::chrono::steady_clock::now();
    while (true)
    {
        // pollRender() uploads the texture + updates status when Done. It must
        // run on the main thread (GL), which is where this handler executes.
        pollRender();
        const RenderState st = m_renderState.load();
        if (st == RenderState::Done || st == RenderState::Failed)
        {
            return true;
        }
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (elapsed > timeoutSeconds)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

nlohmann::json EditorApp::cmdScreenshot(const nlohmann::json& req)
{
    if (!req.contains("path") || !req["path"].is_string())
    {
        return json{{"ok", false}, {"error", "screenshot requires string 'path'"}};
    }
    const std::string path = req["path"].get<std::string>();
    const std::string target = req.value("target", std::string{"window"});

    std::string err = captureScreenshot(path, target);
    if (!err.empty())
    {
        return json{{"ok", false}, {"error", err}};
    }
    return json{{"ok", true}, {"path", path}, {"target", target}};
}

// ===== Puppet commands: inject input + query layout ========================

bool EditorApp::applyInputEventJson(const nlohmann::json& e, std::string& errOut)
{
    // Translate one {"type": ...} JSON event into an InputEvent and feed it
    // through dispatchInputEvent() on this (the main/GL) thread — the exact same
    // path a real OS event takes — so injected input is indistinguishable
    // downstream.
    //
    // Supported types (coordinates in window/framebuffer pixels, top-left origin):
    //   mouse_move   {x, y}
    //   mouse_button {button (0=left,1=right,2=middle), down (bool), mods?}
    //   scroll       {dx?, dy}
    //   key          {key (GLFW key code), down (bool), mods?}
    //   char         {codepoint (int)}
    const std::string type = e.value("type", std::string{});
    if (type == "mouse_move")
    {
        if (!e.contains("x") || !e.contains("y"))
        {
            errOut = "mouse_move requires x and y";
            return false;
        }
        dispatchInputEvent(InputEvent::mouseMove(e["x"].get<double>(), e["y"].get<double>()));
        return true;
    }
    if (type == "mouse_button")
    {
        if (!e.contains("button") || !e.contains("down"))
        {
            errOut = "mouse_button requires button and down";
            return false;
        }
        // Accept ImGui-style indices (0/1/2) and map to GLFW button codes so
        // the nav handlers (which compare against GLFW_MOUSE_BUTTON_*) match.
        int btn = e["button"].get<int>();
        int glfwBtn = (btn == 0)   ? GLFW_MOUSE_BUTTON_LEFT
                      : (btn == 1) ? GLFW_MOUSE_BUTTON_RIGHT
                      : (btn == 2) ? GLFW_MOUSE_BUTTON_MIDDLE
                                   : btn;
        dispatchInputEvent(
            InputEvent::mouseButton(glfwBtn, e["down"].get<bool>(), e.value("mods", 0)));
        return true;
    }
    if (type == "scroll")
    {
        dispatchInputEvent(InputEvent::scroll(e.value("dx", 0.0), e.value("dy", 0.0)));
        return true;
    }
    if (type == "key")
    {
        if (!e.contains("key") || !e.contains("down"))
        {
            errOut = "key requires key and down";
            return false;
        }
        const int key = e["key"].get<int>();
        const bool down = e["down"].get<bool>();
        const int mods = e.value("mods", 0);
        // Feed ImGui IO through the backend's translator (it owns the
        // GLFW->ImGuiKey table), then run the event through our abstraction.
        ImGui_ImplGlfw_KeyCallback(m_window, key, 0, down ? GLFW_PRESS : GLFW_RELEASE, mods);
        dispatchInputEvent(InputEvent::key(key, down, mods));
        return true;
    }
    if (type == "char")
    {
        if (!e.contains("codepoint"))
        {
            errOut = "char requires codepoint";
            return false;
        }
        dispatchInputEvent(
            InputEvent::character(static_cast<unsigned int>(e["codepoint"].get<int>())));
        return true;
    }
    errOut = "unknown event type '" + type + "'";
    return false;
}

nlohmann::json EditorApp::cmdInjectInput(const nlohmann::json& req)
{
    // Accepts either a single event object or {"events": [ ... ]}. All events are
    // applied within THIS frame's drain() — matching how a real drag's events
    // arrive between frames. For a multi-frame interaction (menu popup, DragFloat
    // drag) use play_input, which schedules events across successive frames.
    int applied = 0;
    std::string err;
    if (req.contains("events") && req["events"].is_array())
    {
        for (const auto& e : req["events"])
        {
            if (!applyInputEventJson(e, err))
            {
                return json{{"ok", false}, {"error", err}, {"applied", applied}};
            }
            ++applied;
        }
    }
    else
    {
        if (!applyInputEventJson(req, err))
        {
            return json{{"ok", false}, {"error", err}};
        }
        applied = 1;
    }

    const glm::vec3 eye = m_camera.eye();
    return json{{"ok", true},
                {"applied", applied},
                {"cursor", {m_cursorX, m_cursorY}},
                {"camera", {{"yaw", m_camera.yaw}, {"pitch", m_camera.pitch}}},
                {"eye", {eye.x, eye.y, eye.z}}};
}

nlohmann::json EditorApp::cmdPlayInput(const nlohmann::json& req)
{
    // Replay a TIMED sequence of input events across REAL frames, so multi-frame
    // ImGui interactions (opening a menu popup, hovering to an item, dragging a
    // DragFloat) progress exactly as they would under a human's hand. This is the
    // piece a single batched inject_input cannot do: that lands all events in one
    // frame, but a popup only appears the frame AFTER its menu header is clicked,
    // and a DragFloat only integrates motion across successive NewFrame() deltas.
    //
    // Request: {"actions": [ {"t_ms": <relative ms>, "event": {<inject event>}}, ... ],
    //           "fps": <frames/sec to simulate, default 60>,
    //           "tail_ms": <extra time to keep stepping after the last action,
    //                       default 50> }
    //
    // Scheduling model (identical in windowed + headless): we walk a virtual
    // clock in fixed steps of 1000/fps ms. Each step we fire every action whose
    // t_ms is <= the current virtual time (in t_ms order), then advance ONE real
    // frame via stepFrames(1) — which runs NewFrame()/drawUi()/Render()/present so
    // ImGui sees the elapsed frame and popups/drags make progress. We do NOT
    // re-drain the automation queue while stepping (we're already inside a
    // handler running from drain()), so the in-flight sequence owns the frames it
    // needs and other commands wait until it returns. The handler returns when
    // all actions have fired and the tail has elapsed — so the caller's single
    // request blocks for the whole gesture, then can immediately query results.
    if (!req.contains("actions") || !req["actions"].is_array())
    {
        return json{{"ok", false}, {"error", "play_input requires 'actions' array"}};
    }

    const double fps = req.value("fps", 60.0);
    if (fps <= 0.0)
    {
        return json{{"ok", false}, {"error", "fps must be > 0"}};
    }
    const double frameMs = 1000.0 / fps;
    const double tailMs = req.value("tail_ms", 50.0);

    // Collect + sort actions by time (stable: preserve input order at equal t).
    struct Action
    {
        double tMs;
        json event;
        std::size_t order;
    };
    std::vector<Action> actions;
    actions.reserve(req["actions"].size());
    std::size_t order = 0;
    for (const auto& a : req["actions"])
    {
        if (!a.contains("event") || !a["event"].is_object())
        {
            return json{{"ok", false},
                        {"error", "each action requires an 'event' object"},
                        {"fired", 0}};
        }
        actions.push_back(Action{a.value("t_ms", 0.0), a["event"], order++});
    }
    std::stable_sort(actions.begin(), actions.end(), [](const Action& x, const Action& y) {
        if (x.tMs != y.tMs) return x.tMs < y.tMs;
        return x.order < y.order;
    });

    const double lastT = actions.empty() ? 0.0 : actions.back().tMs;
    const double endMs = lastT + tailMs;

    int fired = 0;
    int frames = 0;
    std::size_t next = 0;          // index of the next un-fired action
    double virtualMs = 0.0;
    // Step the virtual clock frame-by-frame, firing due events before each frame
    // advances, until every action has fired and the tail window has elapsed.
    while (true)
    {
        // Fire every action due at or before the current virtual time.
        while (next < actions.size() && actions[next].tMs <= virtualMs)
        {
            std::string err;
            if (!applyInputEventJson(actions[next].event, err))
            {
                return json{{"ok", false},
                            {"error", err},
                            {"fired", fired},
                            {"frames", frames}};
            }
            ++fired;
            ++next;
        }

        if (next >= actions.size() && virtualMs >= endMs)
        {
            break;
        }

        // Advance one real frame so ImGui integrates the elapsed time (popups
        // open, drags accumulate motion, hovers register).
        stepFrames(1);
        ++frames;
        virtualMs += frameMs;

        // Safety bound: never loop unbounded if fps/tail are pathological.
        if (frames > 100000)
        {
            break;
        }
    }

    const glm::vec3 eye = m_camera.eye();
    return json{{"ok", true},
                {"fired", fired},
                {"frames", frames},
                {"cursor", {m_cursorX, m_cursorY}},
                {"camera", {{"yaw", m_camera.yaw}, {"pitch", m_camera.pitch}}},
                {"eye", {eye.x, eye.y, eye.z}}};
}

nlohmann::json EditorApp::cmdQueryLayout(const nlohmann::json& req)
{
    // Returns the pixel rect(s) of named UI elements recorded during the last
    // frame's UI build (see LayoutRegistry / drawUi). With "name", returns that
    // one element's rect (+ center). Without, returns the whole registry. Rects
    // are window pixels, top-left origin — the same space inject_input uses, so
    // an agent can click a returned center directly.
    auto rectJson = [](const LayoutRect& r) {
        return json{{"x", r.x},
                    {"y", r.y},
                    {"width", r.width},
                    {"height", r.height},
                    {"center", {r.centerX(), r.centerY()}}};
    };

    if (req.contains("name") && req["name"].is_string())
    {
        const std::string name = req["name"].get<std::string>();
        LayoutRect rect;
        if (!m_layout.find(name, rect))
        {
            return json{{"ok", false}, {"error", "no element named '" + name + "'"}};
        }
        return json{{"ok", true}, {"name", name}, {"rect", rectJson(rect)}};
    }

    json elements = json::object();
    for (const auto& [name, rect] : m_layout.all())
    {
        elements[name] = rectJson(rect);
    }
    return json{{"ok", true}, {"elements", elements}};
}

namespace
{
// Map a puppet "kind" string to the model Kind. Returns true on a known kind.
bool parseKind(const std::string& s, SceneModel::Kind& out)
{
    if (s == "SphereVolume" || s == "sphere") { out = SceneModel::Kind::SphereVolume; return true; }
    if (s == "MeshVolume" || s == "mesh") { out = SceneModel::Kind::MeshVolume; return true; }
    if (s == "AreaLight" || s == "area_light") { out = SceneModel::Kind::AreaLight; return true; }
    if (s == "OmniLight" || s == "omni_light") { out = SceneModel::Kind::OmniLight; return true; }
    return false;
}

// A detailed JSON view of one object — its transform, kind fields, and resolved
// material — so the puppet can verify edits without screenshotting.
json objectDetailJson(const SceneModel& scene, int index)
{
    if (index < 0 || index >= static_cast<int>(scene.objects.size()))
    {
        return json(nullptr);
    }
    const SceneModel::ObjectNode& o = scene.objects[static_cast<std::size_t>(index)];
    json j;
    j["index"] = index;
    j["name"] = o.name;
    j["position"] = {o.position.x, o.position.y, o.position.z};
    j["rotation"] = {o.eulerDegrees.x, o.eulerDegrees.y, o.eulerDegrees.z};
    j["scale"] = {o.scale.x, o.scale.y, o.scale.z};
    j["radius"] = o.radius;
    j["light_width"] = o.lightWidth;
    j["light_height"] = o.lightHeight;
    j["light_radius"] = o.lightRadius;
    j["mesh"] = o.meshName;
    j["material_name"] = o.materialName;
    if (const SceneModel::Material* m = scene.findMaterial(o.materialName))
    {
        j["material"] = {{"type", m->type},
                         {"color", {m->color.r, m->color.g, m->color.b}},
                         {"ior", m->ior}};
    }
    return j;
}
}  // namespace

nlohmann::json EditorApp::cmdInsertObject(const nlohmann::json& req)
{
    // Insert an object via the SAME path the Insert menu / right-click use, so a
    // puppet builds a scene exactly as a human would. Optional "select" (default
    // true) leaves it selected for a following set_property.
    if (!req.contains("kind") || !req["kind"].is_string())
    {
        return json{{"ok", false},
                    {"error", "insert_object requires string 'kind' "
                              "(SphereVolume|MeshVolume|AreaLight|OmniLight)"}};
    }
    SceneModel::Kind kind;
    if (!parseKind(req["kind"].get<std::string>(), kind))
    {
        return json{{"ok", false}, {"error", "unknown kind: " + req["kind"].get<std::string>()}};
    }

    const int index = insertObject(kind);
    return json{{"ok", true},
                {"index", index},
                {"name", m_scene.objects[static_cast<std::size_t>(index)].name},
                {"object", objectDetailJson(m_scene, index)},
                {"object_count", m_scene.objects.size()}};
}

nlohmann::json EditorApp::cmdSetProperty(const nlohmann::json& req)
{
    // Set a field on an object — the programmatic twin of the properties-panel
    // widgets (both call setObjectFloatField / setObjectMaterialType, so GUI and
    // puppet edits are identical). Targets the selected object by default, or an
    // explicit "index". Recognized fields:
    //   pos_x/y/z, rot_x/y/z, scale_x/y/z, radius,
    //   light_width, light_height, light_radius,
    //   mat_color_r/g/b, mat_ior  -> numeric "value"
    //   material_type            -> string "value" (Lambertian|Mirror|Glass|Microfacet)
    //   material_name            -> string "value" (assign an existing material)
    //   mesh_file                -> string "value" (register an OBJ in $meshes; MeshVolume)
    //   mesh_shape               -> string "value" (bind to an OBJ sub-shape; MeshVolume)
    int index = m_selectedObject;
    if (req.contains("index") && req["index"].is_number_integer())
    {
        index = req["index"].get<int>();
    }
    if (index < 0 || index >= static_cast<int>(m_scene.objects.size()))
    {
        return json{{"ok", false}, {"error", "no object selected / index out of range"}};
    }
    if (!req.contains("field") || !req["field"].is_string())
    {
        return json{{"ok", false}, {"error", "set_property requires string 'field'"}};
    }
    const std::string field = req["field"].get<std::string>();

    bool ok = false;
    if (field == "material_type")
    {
        if (!req.contains("value") || !req["value"].is_string())
        {
            return json{{"ok", false}, {"error", "material_type needs a string 'value'"}};
        }
        ok = setObjectMaterialType(index, req["value"].get<std::string>());
    }
    else if (field == "material_name")
    {
        if (!req.contains("value") || !req["value"].is_string())
        {
            return json{{"ok", false}, {"error", "material_name needs a string 'value'"}};
        }
        ok = setObjectMaterialName(index, req["value"].get<std::string>());
        if (!ok)
        {
            return json{{"ok", false}, {"error", "no material named '" +
                                                     req["value"].get<std::string>() + "'"}};
        }
    }
    else if (field == "mesh_file")
    {
        if (!req.contains("value") || !req["value"].is_string())
        {
            return json{{"ok", false}, {"error", "mesh_file needs a string 'value'"}};
        }
        ok = setObjectMeshFile(index, req["value"].get<std::string>());
        if (!ok)
        {
            return json{{"ok", false},
                        {"error", "could not bind mesh_file '" +
                                      req["value"].get<std::string>() +
                                      "' (not a MeshVolume, or file not found)"}};
        }
    }
    else if (field == "mesh_shape")
    {
        if (!req.contains("value") || !req["value"].is_string())
        {
            return json{{"ok", false}, {"error", "mesh_shape needs a string 'value'"}};
        }
        ok = setObjectMeshShape(index, req["value"].get<std::string>());
        if (!ok)
        {
            return json{{"ok", false},
                        {"error", "could not set mesh_shape '" +
                                      req["value"].get<std::string>() +
                                      "' (not a MeshVolume?)"}};
        }
    }
    else
    {
        if (!req.contains("value") || !req["value"].is_number())
        {
            return json{{"ok", false}, {"error", "field '" + field + "' needs a numeric 'value'"}};
        }
        ok = setObjectFloatField(index, field, req["value"].get<float>());
    }

    if (!ok)
    {
        return json{{"ok", false}, {"error", "could not set field '" + field + "'"}};
    }
    return json{{"ok", true}, {"object", objectDetailJson(m_scene, index)}};
}

std::string EditorApp::captureScreenshot(const std::string& path, const std::string& target)
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;

    if (target == "window")
    {
        glfwGetFramebufferSize(m_window, &width, &height);
        if (width <= 0 || height <= 0) return "window has zero size";
        pixels.resize(static_cast<size_t>(width) * height * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        PngExport::flipVertical(pixels, width, height);  // GL origin is bottom-left
    }
    else if (target == "viewport")
    {
        if (m_fbo == 0 || m_fboWidth <= 0) return "viewport FBO not ready";
        width = m_fboWidth;
        height = m_fboHeight;
        pixels.resize(static_cast<size_t>(width) * height * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        PngExport::flipVertical(pixels, width, height);
    }
    else if (target == "render")
    {
        // The path-traced result lives in m_renderRgba (already in display
        // orientation, top-left origin) — no GL read or flip needed.
        if (m_renderRgba.empty() || m_renderWidth <= 0)
            return "no render available (run 'render' first)";
        width = m_renderWidth;
        height = m_renderHeight;
        pixels = m_renderRgba;
    }
    else
    {
        return "unknown target '" + target + "' (want window|viewport|render)";
    }

    if (!PngExport::writeRgba(path, width, height, pixels))
    {
        return "failed to write PNG to " + path;
    }
    return {};
}

int EditorApp::runScript(const std::string& scriptPath)
{
    std::ifstream in(scriptPath);
    if (!in)
    {
        std::fprintf(stderr, "Could not open script: %s\n", scriptPath.c_str());
        return 2;
    }

    json script;
    try
    {
        in >> script;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "Invalid script JSON: %s\n", e.what());
        return 2;
    }

    // Accept either a bare array of commands or {"commands": [...]}.
    const json* commands = nullptr;
    if (script.is_array())
    {
        commands = &script;
    }
    else if (script.is_object() && script.contains("commands") && script["commands"].is_array())
    {
        commands = &script["commands"];
    }
    else
    {
        std::fprintf(stderr, "Script must be an array or {\"commands\": [...]}\n");
        return 2;
    }

    int exitCode = 0;
    for (const auto& command : *commands)
    {
        json response = handleCommand(command);
        std::printf("%s\n", response.dump().c_str());
        std::fflush(stdout);
        if (!response.value("ok", false))
        {
            exitCode = 1;
        }
        if (m_automation && m_automation->shouldQuit())
        {
            break;
        }
    }
    return exitCode;
}

bool EditorApp::renderOneFrame(bool drainAutomation)
{
    if (glfwWindowShouldClose(m_window))
    {
        return false;
    }

    glfwPollEvents();

    pollRender();

    // Execute any queued automation commands on this (main/GL) thread. Skipped
    // when called re-entrantly from a command handler (the timed-input
    // scheduler): we are already inside drain(), so draining again would run
    // queued commands out of order relative to the in-flight one.
    if (drainAutomation && m_automation)
    {
        m_automation->drain();
        if (m_automation->shouldQuit())
        {
            return false;
        }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();

    // Headless fix: with a HIDDEN window and the backend's auto-callbacks
    // disabled, ImGui_ImplGlfw_NewFrame()'s UpdateMouseData() falls into its
    // "focused + MouseWindow==null" fallback and OVERWRITES io.MousePos with the
    // real OS cursor (glfwGetCursorPos) every frame — clobbering the position we
    // injected. That's why injected clicks orbit the camera (app state reads our
    // own m_cursorX) yet never drive ImGui widgets/menus (hover is computed from
    // the clobbered io.MousePos). Re-assert the abstraction's cursor AFTER the
    // backend frame and BEFORE ImGui::NewFrame() so our queued position wins.
    // Only in headless mode: a visible focused window reports the true cursor, so
    // there's nothing to correct there.
    if (m_headless)
    {
        ImGui::GetIO().AddMousePosEvent(static_cast<float>(m_cursorX),
                                        static_cast<float>(m_cursorY));
    }

    ImGui::NewFrame();

    drawUi();

    // Render the raster viewport into its FBO (its texture is shown by the
    // Viewport panel above, sampled this frame — one frame of latency is
    // imperceptible).
    renderViewport();

    ImGui::Render();

    int displayW, displayH;
    glfwGetFramebufferSize(m_window, &displayW, &displayH);
    m_lastWindowWidth = displayW;
    m_lastWindowHeight = displayH;
    glViewport(0, 0, displayW, displayH);
    glClearColor(0.06f, 0.06f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(m_window);
    return true;
}

void EditorApp::stepFrames(int count)
{
    for (int i = 0; i < count; ++i)
    {
        // drainAutomation=false: we're already inside a handler running from
        // drain(); advancing frames here must not pop+run other queued commands.
        if (!renderOneFrame(/*drainAutomation=*/false))
        {
            break;
        }
    }
}

void EditorApp::run()
{
    while (renderOneFrame(/*drainAutomation=*/true))
    {
    }
}

void EditorApp::shutdown()
{
    // Stop accepting automation commands before tearing down GL state, so a
    // late command can't touch a destroyed context.
    if (m_automation)
    {
        m_automation->stop();
        m_automation.reset();
    }

    if (m_renderThread.joinable())
    {
        m_renderThread.join();
    }

    if (m_window)
    {
        // Free scene-object gizmo GL buffers before tearing down the context.
        for (auto& d : m_sceneDrawables)
        {
            if (d.gizmoVbo) glDeleteBuffers(1, &d.gizmoVbo);
            if (d.gizmoVao) glDeleteVertexArrays(1, &d.gizmoVao);
        }
        m_sceneDrawables.clear();

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
        if (m_fboColorTex) glDeleteTextures(1, &m_fboColorTex);
        if (m_fboDepthRbo) glDeleteRenderbuffers(1, &m_fboDepthRbo);
        if (m_renderTex) glDeleteTextures(1, &m_renderTex);
        if (m_shaderProgram) glDeleteProgram(m_shaderProgram);
        if (m_lineProgram) glDeleteProgram(m_lineProgram);

        glfwDestroyWindow(m_window);
        m_window = nullptr;
        glfwTerminate();
    }
}

// ===== The single input path ==============================================
//
// dispatchInputEvent() is the ONE place input enters the app. Both the GLFW
// callbacks (real OS events) and the automation port (injected events) call it.
// It does two things, in order:
//   1. Feeds ImGui's IO (AddMousePosEvent/AddMouseButtonEvent/AddMouseWheelEvent
//      /AddKeyEvent/AddInputCharacter) so ImGui sees the event identically
//      regardless of source. (The imgui_impl_glfw backend's own callbacks are
//      NOT installed; this is the only feeder.)
//   2. Routes to the app-state handlers (camera nav, future UI handlers).
//
// Subtlety solved: ImGui's IO needs key events as ImGuiKey enum values, not raw
// GLFW key codes. The imgui_impl_glfw backend has an internal translation table,
// but it's not exported. We reuse the backend's own KeyCallback for key/char so
// the translation stays correct and centralized, while still treating the event
// as having come through our abstraction (we build the InputEvent first, then
// hand the equivalent to the backend). Mouse pos/button/scroll we feed directly
// via the public AddXxx IO functions.

namespace
{
// Map a GLFW mouse button to ImGui's mouse button index. ImGui only tracks the
// first few buttons by index; left=0, right=1, middle=2 matches GLFW's values.
int imguiMouseButton(int glfwButton)
{
    switch (glfwButton)
    {
        case GLFW_MOUSE_BUTTON_LEFT: return 0;
        case GLFW_MOUSE_BUTTON_RIGHT: return 1;
        case GLFW_MOUSE_BUTTON_MIDDLE: return 2;
        default: return glfwButton;  // ImGui clamps internally
    }
}
}  // namespace

void EditorApp::dispatchInputEvent(const InputEvent& event)
{
    ImGuiIO& io = ImGui::GetIO();

    switch (event.type)
    {
        case InputEvent::Type::MouseMove:
            m_cursorX = event.x;
            m_cursorY = event.y;
            io.AddMousePosEvent(static_cast<float>(event.x), static_cast<float>(event.y));
            onCursorPos(event.x, event.y);
            break;

        case InputEvent::Type::MouseButton:
            io.AddMouseButtonEvent(imguiMouseButton(event.button), event.down);
            onMouseButton(event.button, event.down, event.mods);
            break;

        case InputEvent::Type::Scroll:
            io.AddMouseWheelEvent(static_cast<float>(event.dx), static_cast<float>(event.dy));
            onScroll(event.dx, event.dy);
            break;

        case InputEvent::Type::Key:
            // Defer ImGui IO key feeding to the backend's KeyCallback (it owns
            // the GLFW->ImGuiKey translation table). We've already captured the
            // event shape; app-level key handling (none yet) would go here.
            break;

        case InputEvent::Type::Char:
            io.AddInputCharacter(event.codepoint);
            break;
    }
}

// ===== App-state input handlers (fed only from dispatchInputEvent) =========

void EditorApp::onMouseButton(int button, bool down, int mods)
{
    // DCC navigation conventions:
    //   left-drag                -> orbit
    //   middle-drag              -> pan
    //   shift + left-drag        -> pan (for mice/trackpads without a middle
    //                               button)
    // A drag only begins when the press lands over the viewport; releases always
    // clear the active gesture so a release outside the viewport still ends it.
    // "Over the viewport" is computed from the recorded viewport screen rect and
    // the abstraction's cursor position, so injected presses are gated the same
    // way real ones are (independent of ImGui's frame-lagged hover state).
    const bool shift = (mods & GLFW_MOD_SHIFT) != 0;
    const bool overViewport = m_viewportScreenRect.valid() &&
                              m_cursorX >= m_viewportScreenRect.x &&
                              m_cursorX <= m_viewportScreenRect.x + m_viewportScreenRect.width &&
                              m_cursorY >= m_viewportScreenRect.y &&
                              m_cursorY <= m_viewportScreenRect.y + m_viewportScreenRect.height;

    if (down && overViewport)
    {
        if (button == GLFW_MOUSE_BUTTON_MIDDLE ||
            (button == GLFW_MOUSE_BUTTON_LEFT && shift))
        {
            m_panning = true;
            m_orbiting = false;
            m_lastCursorX = m_cursorX;
            m_lastCursorY = m_cursorY;
        }
        else if (button == GLFW_MOUSE_BUTTON_LEFT)
        {
            m_orbiting = true;
            m_panning = false;
            m_lastCursorX = m_cursorX;
            m_lastCursorY = m_cursorY;
        }
    }
    else if (!down)
    {
        if (button == GLFW_MOUSE_BUTTON_LEFT)
        {
            m_orbiting = false;
            m_panning = false;
        }
        else if (button == GLFW_MOUSE_BUTTON_MIDDLE)
        {
            m_panning = false;
        }
    }
}

void EditorApp::onCursorPos(double x, double y)
{
    if (!m_orbiting && !m_panning)
    {
        return;
    }

    const double dx = x - m_lastCursorX;
    const double dy = y - m_lastCursorY;
    m_lastCursorX = x;
    m_lastCursorY = y;

    if (m_orbiting)
    {
        // Drag-right -> orbit right; drag-up -> tilt up.
        const float orbitSpeed = 0.008f;
        m_camera.orbit(static_cast<float>(-dx) * orbitSpeed,
                       static_cast<float>(dy) * orbitSpeed);
    }
    else if (m_panning)
    {
        // Scale pan by distance so the cursor tracks the scene at any zoom:
        // farther out, each pixel covers more world. The vertical FOV and the
        // viewport height set the world-units-per-pixel at the target depth.
        const float vpHeight = m_fboHeight > 0 ? static_cast<float>(m_fboHeight) : 600.0f;
        const float worldPerPixel =
            2.0f * m_camera.distance * std::tan(m_camera.fovYRadians * 0.5f) / vpHeight;
        // Drag-right moves the view content right (target moves left), drag-up
        // moves content down (target moves up) — standard grab-the-scene pan.
        m_camera.pan(static_cast<float>(-dx) * worldPerPixel,
                     static_cast<float>(dy) * worldPerPixel);
    }
}

void EditorApp::onScroll(double /*xoffset*/, double yoffset)
{
    const bool overViewport = m_viewportScreenRect.valid() &&
                              m_cursorX >= m_viewportScreenRect.x &&
                              m_cursorX <= m_viewportScreenRect.x + m_viewportScreenRect.width &&
                              m_cursorY >= m_viewportScreenRect.y &&
                              m_cursorY <= m_viewportScreenRect.y + m_viewportScreenRect.height;
    if (!overViewport)
    {
        return;
    }
    // Scroll up -> zoom in (smaller distance).
    const float factor = (yoffset > 0) ? 0.9f : 1.0f / 0.9f;
    m_camera.dolly(factor);
}

// ===== GLFW raw callbacks: TRANSLATE OS events into InputEvents only ========
// These contain no app logic. They build an InputEvent and feed the single
// input path. The only exception is key/char, where we additionally forward to
// the imgui_impl_glfw backend's own callback so ImGui gets a correctly
// translated ImGuiKey (the backend owns the GLFW->ImGuiKey table); the event
// still conceptually travels through our abstraction (we build the InputEvent
// and call dispatchInputEvent for symmetry / future app-level key handling).

void EditorApp::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    auto* app = static_cast<EditorApp*>(glfwGetWindowUserPointer(window));
    if (app) app->dispatchInputEvent(InputEvent::mouseButton(button, action == GLFW_PRESS, mods));
}

void EditorApp::cursorPosCallback(GLFWwindow* window, double x, double y)
{
    auto* app = static_cast<EditorApp*>(glfwGetWindowUserPointer(window));
    if (app) app->dispatchInputEvent(InputEvent::mouseMove(x, y));
}

void EditorApp::scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    auto* app = static_cast<EditorApp*>(glfwGetWindowUserPointer(window));
    if (app) app->dispatchInputEvent(InputEvent::scroll(xoffset, yoffset));
}

void EditorApp::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    auto* app = static_cast<EditorApp*>(glfwGetWindowUserPointer(window));
    if (!app) return;
    // Let the backend translate + feed ImGui IO (it owns the key table).
    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
    // Also run it through our abstraction (app-level key handling lives here).
    if (action != GLFW_REPEAT)
    {
        app->dispatchInputEvent(InputEvent::key(key, action == GLFW_PRESS, mods));
    }
}

void EditorApp::charCallback(GLFWwindow* window, unsigned int codepoint)
{
    auto* app = static_cast<EditorApp*>(glfwGetWindowUserPointer(window));
    if (app) app->dispatchInputEvent(InputEvent::character(codepoint));
}
