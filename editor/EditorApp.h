#pragma once

#include "OrbitCamera.h"
#include "RasterMesh.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

struct GLFWwindow;

// Top-level editor application. Owns the GLFW window + GL context, the ImGui
// context, the raster viewport (offscreen FBO + mesh + orbit camera), and the
// glue to the ray-tracer-lib render API.
class EditorApp
{
public:
    EditorApp();
    ~EditorApp();

    // Sets a mesh path to load on startup. If unset, a bundled fallback is used.
    void setInitialMeshPath(const std::string& path);

    bool initialize();
    void run();
    void shutdown();

private:
    void drawUi();
    void renderViewport();
    void resizeFbo(int width, int height);

    // Phase 3: kick off a path-traced render of the current scene on a worker
    // thread and poll its completion in the UI loop.
    void startRender();
    void pollRender();
    void uploadRenderTexture();

    // GLFW input callbacks dispatch into these.
    void onMouseButton(int button, int action, int mods);
    void onCursorPos(double x, double y);
    void onScroll(double xoffset, double yoffset);

    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double x, double y);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

    GLFWwindow* m_window = nullptr;

    std::string m_meshPath;
    std::string m_meshLabel = "(none)";

    // Raster viewport state.
    RasterMesh m_mesh;
    OrbitCamera m_camera;
    unsigned int m_shaderProgram = 0;
    unsigned int m_fbo = 0;
    unsigned int m_fboColorTex = 0;
    unsigned int m_fboDepthRbo = 0;
    int m_fboWidth = 0;
    int m_fboHeight = 0;
    float m_meshColor[3] = {0.75f, 0.75f, 0.78f};

    // Orbit input tracking.
    bool m_dragging = false;
    double m_lastCursorX = 0.0;
    double m_lastCursorY = 0.0;
    bool m_cursorOverViewport = false;

    // Phase 3: render-to-image state.
    enum class RenderState
    {
        Idle,
        Running,
        Done,
        Failed
    };
    std::thread m_renderThread;
    std::atomic<RenderState> m_renderState{RenderState::Idle};
    std::atomic<bool> m_renderTextureDirty{false};
    std::string m_renderError;
    std::string m_renderStatus = "No render yet.";
    std::vector<uint8_t> m_renderRgba;  // populated by render thread, uploaded on main thread
    int m_renderWidth = 0;
    int m_renderHeight = 0;
    unsigned int m_renderTex = 0;
    std::string m_scenePath;  // scene JSON to render (defaults to MirrorTest.json if found)
    int m_renderResolution = 256;
    int m_renderPhotonsMillions = 4;
};
