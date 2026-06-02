#include "EditorApp.h"

#include "GlHeaders.h"
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

#include <cstdio>
#include <filesystem>
#include <stdexcept>

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
    // Bind attribute locations to match RasterMesh's VAO layout.
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aNormal");
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

    // Build the viewport shader.
    m_shaderProgram = linkProgram(kViewportVertexShader, kViewportFragmentShader);
    if (!m_shaderProgram)
    {
        std::fprintf(stderr, "Failed to build viewport shader\n");
        return false;
    }

    glEnable(GL_DEPTH_TEST);

    // Resolve a mesh to load: explicit path, else MirrorTest's first mesh, else
    // the bundled cube.
    if (m_meshPath.empty())
    {
        std::filesystem::path cornell = findUpwards("meshes/CornellBox.obj");
        if (!cornell.empty())
        {
            m_meshPath = cornell.string();
        }
        else
        {
            std::filesystem::path cube = findUpwards("editor/assets/cube.obj");
            if (cube.empty())
            {
                cube = findUpwards("meshes/cube.obj");
            }
            m_meshPath = cube.empty() ? std::string{} : cube.string();
        }
    }

    if (!m_meshPath.empty())
    {
        MeshData data = loadObjMeshData(m_meshPath);
        if (data.valid)
        {
            m_mesh.upload(data);
            m_camera.frameBounds(data.minBound, data.maxBound);
            m_meshLabel = std::filesystem::path(m_meshPath).filename().string() +
                          " (" + std::to_string(data.triangleCount()) + " tris)";
        }
        else
        {
            m_meshLabel = "load failed: " + data.error;
            std::fprintf(stderr, "%s\n", data.error.c_str());
        }
    }

    // Resolve a scene file to render.
    std::filesystem::path scene = findUpwards("MirrorTest.json");
    if (!scene.empty())
    {
        m_scenePath = scene.string();
    }

    resizeFbo(800, 600);

    return true;
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

void EditorApp::drawUi()
{
    // Controls + scene panel.
    ImGui::Begin("Ray Tracer Editor");
    ImGui::TextWrapped("GUI model/scene editor for the photon path tracer.");
    ImGui::Separator();
    ImGui::Text("Mesh: %s", m_meshLabel.c_str());
    ImGui::Text("Viewport: orbit = left-drag, zoom = scroll");
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

void EditorApp::run()
{
    while (!glfwWindowShouldClose(m_window))
    {
        glfwPollEvents();

        pollRender();

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
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.06f, 0.06f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(m_window);
    }
}

void EditorApp::shutdown()
{
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

        glfwDestroyWindow(m_window);
        m_window = nullptr;
        glfwTerminate();
    }
}

// ===== Input handling

void EditorApp::onMouseButton(int button, int action, int /*mods*/)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        if (action == GLFW_PRESS && m_cursorOverViewport)
        {
            m_dragging = true;
            glfwGetCursorPos(m_window, &m_lastCursorX, &m_lastCursorY);
        }
        else if (action == GLFW_RELEASE)
        {
            m_dragging = false;
        }
    }
}

void EditorApp::onCursorPos(double x, double y)
{
    if (m_dragging)
    {
        const double dx = x - m_lastCursorX;
        const double dy = y - m_lastCursorY;
        m_lastCursorX = x;
        m_lastCursorY = y;

        // Drag-right -> orbit right; drag-up -> tilt up.
        const float orbitSpeed = 0.008f;
        m_camera.orbit(static_cast<float>(-dx) * orbitSpeed,
                       static_cast<float>(dy) * orbitSpeed);
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
