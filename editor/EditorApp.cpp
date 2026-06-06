#include "EditorApp.h"

#include "AutomationServer.h"
#include "GlHeaders.h"
#include "PngExport.h"
#include "Shaders.h"

#include "Image.h"
#include "Renderer.h"
#include "SceneLoader.h"

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

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

    m_window = glfwCreateWindow(1280, 800, "Ray Tracer Editor", nullptr, nullptr);
    if (!m_window)
    {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return false;
    }

    glfwSetWindowUserPointer(m_window, this);
    glfwSetMouseButtonCallback(m_window, &EditorApp::mouseButtonCallback);
    glfwSetCursorPosCallback(m_window, &EditorApp::cursorPosCallback);
    glfwSetScrollCallback(m_window, &EditorApp::scrollCallback);

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);  // vsync

    // ImGui setup.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
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

    m_camera.frameOrigin();
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
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New", "Cmd+N"))
            {
                newScene();
            }
            ImGui::Separator();
            // Stubs for later waves — visible so the shape is clear, disabled so
            // they can't be triggered before they're implemented.
            ImGui::BeginDisabled();
            ImGui::MenuItem("Open...", "Cmd+O");
            ImGui::MenuItem("Save", "Cmd+S");
            ImGui::MenuItem("Save As...", "Shift+Cmd+S");
            ImGui::EndDisabled();
            ImGui::Separator();
            if (ImGui::MenuItem("Quit"))
            {
                glfwSetWindowShouldClose(m_window, GLFW_TRUE);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Insert"))
        {
            // Object-insertion lands in the next wave; surfaced disabled here so
            // the menu seam is visible.
            ImGui::BeginDisabled();
            ImGui::MenuItem("Sphere");
            ImGui::MenuItem("Mesh...");
            ImGui::MenuItem("Area Light");
            ImGui::MenuItem("Omni Light");
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void EditorApp::drawUi()
{
    drawMenuBar();

    // Give each window an explicit, non-overlapping initial layout. Without this
    // all three windows default to (0,0) and stack on top of each other: the
    // Viewport window (which shows the live FBO) ends up hidden behind the
    // controls panel — so the window looks like a black viewport — and the
    // Render window gets crushed to a sliver on the left edge, which makes its
    // help text wrap one character per line. FirstUseEver lets the user freely
    // move/resize afterwards (and respects any saved imgui.ini layout).
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    const ImVec2 origin = mainViewport->WorkPos;
    const ImVec2 size = mainViewport->WorkSize;
    const float controlsWidth = 360.0f;
    const float renderHeight = 260.0f;

    ImGui::SetNextWindowPos(origin, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(controlsWidth, size.y), ImGuiCond_FirstUseEver);

    // Controls + scene panel.
    ImGui::Begin("Ray Tracer Editor");
    ImGui::TextWrapped("GUI model/scene editor for the photon path tracer.");
    ImGui::Separator();
    ImGui::Text("Scene: %s%s", m_scene.name.c_str(), m_scene.dirty ? " *" : "");
    ImGui::Text("Objects: %zu   Lights: %zu", m_scene.objects.size(), m_scene.lights.size());
    ImGui::Text("Mesh: %s", m_meshLabel.c_str());
    ImGui::TextWrapped("Viewport: orbit = left-drag, pan = middle-drag or "
                       "shift+left-drag, zoom = scroll");
    ImGui::ColorEdit3("Preview color", m_meshColor);
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
    if (m_scenePath.empty())
    {
        m_renderState.store(RenderState::Failed);
        m_renderError = "No scene file (MirrorTest.json) found.";
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

    const std::string scenePath = m_scenePath;
    const int resolution = m_renderResolution;
    const size_t photons = static_cast<size_t>(m_renderPhotonsMillions) * 1000000;

    // TODO (Phase 4 — progressive preview, not yet wired): renderFrame already
    // accepts a Renderer::ProgressCallback and the Buffer is pollable mid-render.
    // To show accumulation, capture result.buffer here, and on a UI timer call
    // Renderer::tonemapBufferToImage on a snapshot + re-upload m_renderTex while
    // RenderState::Running. Camera moves would reset/restart the render. Left as
    // a clean follow-up rather than half-wired.
    m_renderThread = std::thread([this, scenePath, resolution, photons]() {
        try
        {
            LoadedScene scene = SceneLoader::loadFromFile(scenePath, /*logToStdout=*/false);
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
    if (cmd == "set_camera") return cmdSetCamera(request);
    if (cmd == "set_render_settings") return cmdSetRenderSettings(request);
    if (cmd == "render") return cmdRender(request);
    if (cmd == "screenshot") return cmdScreenshot(request);
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

void EditorApp::run()
{
    while (!glfwWindowShouldClose(m_window))
    {
        glfwPollEvents();

        pollRender();

        // Execute any queued automation commands on this (main/GL) thread.
        if (m_automation)
        {
            m_automation->drain();
            if (m_automation->shouldQuit())
            {
                break;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
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

// ===== Input handling

void EditorApp::onMouseButton(int button, int action, int mods)
{
    // DCC navigation conventions:
    //   left-drag                -> orbit
    //   middle-drag              -> pan
    //   shift + left-drag        -> pan (for mice/trackpads without a middle
    //                               button)
    // A drag only begins when the press lands over the viewport; releases always
    // clear the active gesture so a release outside the viewport still ends it.
    const bool shift = (mods & GLFW_MOD_SHIFT) != 0;

    if (action == GLFW_PRESS && m_cursorOverViewport)
    {
        if (button == GLFW_MOUSE_BUTTON_MIDDLE ||
            (button == GLFW_MOUSE_BUTTON_LEFT && shift))
        {
            m_panning = true;
            m_orbiting = false;
            glfwGetCursorPos(m_window, &m_lastCursorX, &m_lastCursorY);
        }
        else if (button == GLFW_MOUSE_BUTTON_LEFT)
        {
            m_orbiting = true;
            m_panning = false;
            glfwGetCursorPos(m_window, &m_lastCursorX, &m_lastCursorY);
        }
    }
    else if (action == GLFW_RELEASE)
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
    if (!m_cursorOverViewport)
    {
        return;
    }
    // Scroll up -> zoom in (smaller distance).
    const float factor = (yoffset > 0) ? 0.9f : 1.0f / 0.9f;
    m_camera.dolly(factor);
}

void EditorApp::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    auto* app = static_cast<EditorApp*>(glfwGetWindowUserPointer(window));
    if (app) app->onMouseButton(button, action, mods);
}

void EditorApp::cursorPosCallback(GLFWwindow* window, double x, double y)
{
    auto* app = static_cast<EditorApp*>(glfwGetWindowUserPointer(window));
    if (app) app->onCursorPos(x, y);
}

void EditorApp::scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    auto* app = static_cast<EditorApp*>(glfwGetWindowUserPointer(window));
    // ImGui's glfw backend chained to this callback at init time (we registered
    // ours before ImGui_ImplGlfw_InitForOpenGL), so ImGui already saw the event.
    if (app) app->onScroll(xoffset, yoffset);
}
