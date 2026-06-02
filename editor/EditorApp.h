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

#include <nlohmann/json.hpp>

struct GLFWwindow;
class AutomationServer;

// Top-level editor application. Owns the GLFW window + GL context, the ImGui
// context, the raster viewport (offscreen FBO + mesh + orbit camera), and the
// glue to the ray-tracer-lib render API.
class EditorApp
{
public:
    EditorApp();
    ~EditorApp();

    // Render lifecycle state (public so the automation handlers and the
    // get_state introspection can name it).
    enum class RenderState
    {
        Idle,
        Running,
        Done,
        Failed
    };

    // Sets a mesh path to load on startup. If unset, a bundled fallback is used.
    void setInitialMeshPath(const std::string& path);

    // Enable the localhost automation command port (127.0.0.1 only). Off by
    // default; call before initialize(). port 0 leaves it disabled.
    void setAutomationPort(uint16_t port);

    bool initialize();
    void run();
    void shutdown();

    // ===== Automation command handlers (run on the main/GL thread) ==========
    // Each maps a parsed request JSON to a response JSON. These are public so
    // the --script runner and tests can invoke them directly without going
    // through the socket. They assume a current GL context (except where noted)
    // and must only be called on the main thread.

    // Dispatch a single command object {"cmd": "...", ...} to the right handler.
    nlohmann::json handleCommand(const nlohmann::json& request);

    nlohmann::json cmdPing(const nlohmann::json& req);
    nlohmann::json cmdGetState(const nlohmann::json& req);
    nlohmann::json cmdLoadMesh(const nlohmann::json& req);
    nlohmann::json cmdSetCamera(const nlohmann::json& req);
    nlohmann::json cmdSetRenderSettings(const nlohmann::json& req);
    nlohmann::json cmdRender(const nlohmann::json& req);
    nlohmann::json cmdScreenshot(const nlohmann::json& req);

    // Load an OBJ into the viewport (GL upload + reframe camera). Returns an
    // error string on failure, empty on success.
    std::string loadMeshFromPath(const std::string& path);

    // Capture pixels to a PNG. target is "window" | "viewport" | "render".
    // Returns empty on success, else an error string. Requires a GL context.
    std::string captureScreenshot(const std::string& path, const std::string& target);

    // Block (pumping the render poll) until the current render finishes or the
    // timeout elapses. Returns true if a render completed. Main-thread only.
    bool waitForRenderToFinish(double timeoutSeconds);

    // Run a list of command objects (the --script path), printing each
    // response to stdout. Returns process exit code (0 = all ok).
    int runScript(const std::string& scriptPath);

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

    // Automation command port (127.0.0.1 only). Null/0 when disabled.
    std::unique_ptr<AutomationServer> m_automation;
    uint16_t m_automationPort = 0;

    // Last framebuffer size, tracked each frame so the "window" screenshot
    // target and get_state can report it without a GL query mid-handler.
    int m_lastWindowWidth = 0;
    int m_lastWindowHeight = 0;
};
