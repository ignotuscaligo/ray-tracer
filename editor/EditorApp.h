#pragma once

#include "InputEvent.h"
#include "LayoutRegistry.h"
#include "OrbitCamera.h"
#include "RasterMesh.h"
#include "Scene.h"
#include "ViewportGrid.h"

#include <glm/glm.hpp>

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

    // Create the GLFW window hidden (GLFW_VISIBLE=false) so automation runs
    // without popping a visible window. A real GL context + FBO are still
    // created, so rendering and screenshots work. Call before initialize().
    void setHeadless(bool headless);

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
    nlohmann::json cmdInjectInput(const nlohmann::json& req);
    nlohmann::json cmdQueryLayout(const nlohmann::json& req);
    nlohmann::json cmdInsertObject(const nlohmann::json& req);
    nlohmann::json cmdSetProperty(const nlohmann::json& req);

    // Load an OBJ into the viewport (GL upload + reframe camera). Returns an
    // error string on failure, empty on success.
    std::string loadMeshFromPath(const std::string& path);

    // Load a renderer scene JSON INTO the in-memory model (camera, materials,
    // objects), upload each object's geometry for the viewport, frame the orbit
    // camera on the scene, and remember the path for render-from-view. Returns
    // an error string on failure, empty on success. Requires a GL context.
    std::string loadSceneFromPath(const std::string& path);

    // Serialize the current in-memory model to renderer scene JSON (via
    // SceneModelSerializer) and write it to `path`. The live orbit camera is
    // baked into the saved Camera block (same mapping render-from-view uses) so
    // the saved file renders framed exactly as the viewport shows it. Updates
    // m_scenePath + clears the dirty flag. Returns empty on success, else an
    // error string. This backs both the File > Save menu item and the
    // save_scene automation command.
    std::string saveScene(const std::string& path);

    // Serialize the current model to renderer scene JSON with the LIVE orbit
    // camera baked into the Camera block (synthesizing one if the model has
    // none). Shared by render-from-view and saveScene so a saved scene and a
    // rendered scene frame identically.
    nlohmann::json serializeWithLiveCamera();

    nlohmann::json cmdLoadScene(const nlohmann::json& req);
    nlohmann::json cmdSaveScene(const nlohmann::json& req);

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
    void drawMenuBar();
    void renderViewport();
    void resizeFbo(int width, int height);

    // File > New: initialize an empty scene and show the viewport. The substrate
    // for later waves (object insertion, render-from-view, save) — see Scene.h.
    void newScene();

    // Rebuild the GL geometry for the current m_scene model: load each
    // MeshVolume's named OBJ sub-shape, tessellate a proxy sphere per
    // SphereVolume, and build a wireframe gizmo per light. Requires a GL context.
    void buildSceneGl();

    // Draw all uploaded scene objects (meshes + sphere proxies) and light gizmos
    // into the bound viewport FBO using the given view/projection.
    void drawSceneObjects(const glm::mat4& view, const glm::mat4& proj);

    // The scene-explorer panel: a tree/list of the scene's objects + camera.
    // Selecting a row sets m_selectedObject and records each row's pixel rect.
    void drawSceneExplorer();

    // The properties panel: when an object is selected, an editable form of its
    // transform + kind-specific fields + material. Each editable widget registers
    // its pixel rect in the LayoutRegistry (prop_*). Live edits mutate the model
    // and rebuild the affected viewport geometry.
    void drawPropertiesPanel();

    // ===== Model mutation (the single path both GUI + puppet go through) =====
    // Insert a new object of `kind` into the model with a sensible default
    // transform + material, rebuild its viewport geometry, and select it. Returns
    // the index of the new object (always valid). Requires a GL context
    // (buildSceneGl uploads geometry).
    int insertObject(Scene::Kind kind);

    // Ensure a material exists for a freshly-inserted object, returning its name.
    // Reuses a default material per type if one already exists, else creates one.
    std::string ensureDefaultMaterial(const std::string& typeName,
                                      const glm::vec3& color);

    // Set a named scalar/vector field on the object at `index`. `field` is one of:
    //   pos_x|pos_y|pos_z, rot_x|rot_y|rot_z, scale_x|scale_y|scale_z,
    //   radius, light_width, light_height, light_radius,
    //   mat_color_r|mat_color_g|mat_color_b, mat_ior.
    // Returns true if applied. Rebuilds the object's viewport geometry. This is
    // the code both the properties-panel widgets and the set_property command
    // call, so GUI and puppet edits are identical.
    bool setObjectFloatField(int index, const std::string& field, float value);

    // Set the object's material TYPE (Lambertian|Mirror|Glass|Microfacet) and
    // its material's name selection. Returns true if applied.
    bool setObjectMaterialType(int index, const std::string& type);
    bool setObjectMaterialName(int index, const std::string& materialName);

    // Bind a MeshVolume object at `index` to a specific OBJ sub-shape (and,
    // optionally, register a specific OBJ file). `mesh_shape` sets the $mesh
    // sub-shape name the volume draws (e.g. "Left"/"Right"/"Ceiling" in
    // CornellBox.obj); `mesh_file` ensures the given OBJ (resolved upwards if
    // relative) is in the model's $meshes list so the sub-shape resolves in both
    // the viewport and the serialized render. Returns true if applied. Same
    // mutator the (future) properties-panel mesh picker would call.
    bool setObjectMeshFile(int index, const std::string& meshFile);
    bool setObjectMeshShape(int index, const std::string& shapeName);

    // Phase 3: kick off a path-traced render of the current scene on a worker
    // thread and poll its completion in the UI loop.
    void startRender();
    void pollRender();
    void uploadRenderTexture();

    // ===== The single input path ==========================================
    // Both the OS layer (GLFW callbacks) and the automation port feed events
    // here. This is the ONLY place input enters the app. dispatchInputEvent()
    // (a) feeds ImGui's IO and (b) routes to the app-state handlers below, so
    // injected and real events are indistinguishable downstream.
    void dispatchInputEvent(const InputEvent& event);

    // App-state input handlers, fed exclusively from dispatchInputEvent(). They
    // never read raw GLFW state — m_cursorX/m_cursorY track the abstraction's
    // notion of the cursor so a button-press knows where the pointer is even
    // when injected events arrive out of band.
    void onMouseButton(int button, bool down, int mods);
    void onCursorPos(double x, double y);
    void onScroll(double xoffset, double yoffset);

    // GLFW raw callbacks: these only TRANSLATE the OS event into an InputEvent
    // and hand it to dispatchInputEvent(). No app logic lives here. ImGui's own
    // glfw callbacks are NOT installed (InitForOpenGL(window, false)) so this is
    // the sole consumer of GLFW input — see EditorApp.cpp.
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double x, double y);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void charCallback(GLFWwindow* window, unsigned int codepoint);

    GLFWwindow* m_window = nullptr;

    std::string m_meshPath;
    std::string m_meshLabel = "(none)";

    // The in-memory scene model (maps to the renderer's scene JSON). Empty until
    // File > New (or, later, a load) runs. See Scene.h.
    Scene m_scene;

    // ----- scene-object GL display ----------------------------------------
    // One drawable per scene object. Meshes + sphere proxies share the lit mesh
    // shader and carry a model matrix + base color; light gizmos are wireframe
    // line geometry drawn with the flat line shader. Index into m_scene.objects
    // is kept so the explorer's selected object can be highlighted.
    struct SceneDrawable
    {
        std::unique_ptr<RasterMesh> mesh;  // shaded geometry (mesh or sphere proxy)
        glm::mat4 model{1.0f};
        glm::vec3 color{0.75f};
        std::size_t objectIndex = 0;       // index into m_scene.objects
        bool isLight = false;              // lights also get a line gizmo (below)

        // Line gizmo geometry (lights): a wireframe quad/disc in world space,
        // interleaved position+color, drawn with the line shader. Empty for
        // non-light drawables.
        unsigned int gizmoVao = 0;
        unsigned int gizmoVbo = 0;
        std::size_t gizmoVertexCount = 0;
    };
    std::vector<SceneDrawable> m_sceneDrawables;

    // The currently selected scene object (index into m_scene.objects), or -1.
    // Set by the scene explorer; used to highlight the row + (next wave) drive
    // the properties panel.
    int m_selectedObject = -1;

    // Raster viewport state.
    RasterMesh m_mesh;
    OrbitCamera m_camera;
    ViewportGrid m_grid;             // XZ ground grid + origin gnomon
    unsigned int m_shaderProgram = 0;  // mesh shader
    unsigned int m_lineProgram = 0;    // flat line shader (grid/gnomon)
    unsigned int m_fbo = 0;
    unsigned int m_fboColorTex = 0;
    unsigned int m_fboDepthRbo = 0;
    int m_fboWidth = 0;
    int m_fboHeight = 0;
    float m_meshColor[3] = {0.75f, 0.75f, 0.78f};

    // Viewport navigation input tracking. Orbit = left-drag, pan = middle-drag
    // or shift+left-drag, zoom = scroll (DCC conventions).
    bool m_orbiting = false;
    bool m_panning = false;
    double m_lastCursorX = 0.0;
    double m_lastCursorY = 0.0;
    bool m_cursorOverViewport = false;

    // The abstraction's notion of the cursor position (window pixels), updated by
    // every MouseMove InputEvent regardless of source. App input handlers read
    // this instead of glfwGetCursorPos so injected drags work identically to
    // real ones. m_shiftDown mirrors the modifier so pan-on-shift works for
    // injected input too (injected button events carry mods, but we also track
    // it from key events as a fallback).
    double m_cursorX = 0.0;
    double m_cursorY = 0.0;

    // Per-frame registry of named UI element rects, rebuilt each frame in
    // drawUi()/recordLayout() and read by the query_layout command. See
    // LayoutRegistry.h.
    LayoutRegistry m_layout;

    // Pixel rect of the menu-bar item rects we record by name, plus the viewport
    // image rect — captured during drawUi(). The viewport rect is also used to
    // decide whether a cursor position is "over the viewport" for nav gating
    // when an event is injected (ImGui::IsWindowHovered only reflects ImGui's
    // own hover state, which an injected move does update once it reaches IO).
    LayoutRect m_viewportScreenRect;

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
    std::string m_lastRenderScenePath;  // temp scene file emitted by the last render
    int m_renderResolution = 256;
    int m_renderPhotonsMillions = 4;

    // Automation command port (127.0.0.1 only). Null/0 when disabled.
    std::unique_ptr<AutomationServer> m_automation;
    uint16_t m_automationPort = 0;

    // Headless mode: create the GLFW window hidden so automation doesn't pop a
    // visible window. GL context + FBO + screenshots still work.
    bool m_headless = false;

    // Last framebuffer size, tracked each frame so the "window" screenshot
    // target and get_state can report it without a GL query mid-handler.
    int m_lastWindowWidth = 0;
    int m_lastWindowHeight = 0;
};
