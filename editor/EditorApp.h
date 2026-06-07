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

    // The active viewport tool (set by the top toolbar). Select picks objects;
    // Move/Rotate/Scale show the matching transform gizmo on the selected object.
    enum class Tool
    {
        Select,
        Move,
        Rotate,
        Scale
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
    nlohmann::json cmdPlayInput(const nlohmann::json& req);
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
    void drawToolbar();
    void renderViewport();
    void resizeFbo(int width, int height);

    // Run exactly one frame of the editor: poll OS events, drain queued
    // automation commands, build + render the UI, render the viewport FBO, and
    // present. This is the single unit of work the main loop repeats, factored
    // out so the timed-input scheduler (cmdPlayInput) can advance ImGui across
    // real frames from inside a command handler. `drainAutomation` is false when
    // called re-entrantly from a handler (we're already inside drain(); draining
    // again would deadlock / reorder commands) and true for the normal loop.
    // Returns false if the window has been asked to close.
    bool renderOneFrame(bool drainAutomation);

    // Advance `count` whole frames (renderOneFrame without re-draining the
    // automation queue). Used by the timed-input scheduler so popups/drags make
    // multi-frame progress while a command handler is running.
    void stepFrames(int count);

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

    // ===== Viewport ray-picking + transform gizmos ========================
    // Build a world-space ray from a click at (winX, winY) in window pixels,
    // using the orbit camera and the current viewport FBO rect. Fills `origin`
    // and `dir` (normalized). Returns false if the point is outside the viewport.
    bool viewportRay(double winX, double winY, glm::vec3& origin, glm::vec3& dir) const;

    // Project a world point to window pixels via the orbit camera + viewport rect.
    // Returns false if behind the camera. Used to place/pick gizmo handles.
    bool projectToWindow(const glm::vec3& world, glm::vec2& outWin) const;

    // Ray-pick the scene objects at a window-pixel click: nearest object whose
    // world-space proxy (sphere for SphereVolume, AABB for meshes/lights) the ray
    // hits. Returns the object index or -1 on a miss (empty space). Selecting/
    // deselecting is the caller's job (so it can sync the explorer).
    int pickObject(double winX, double winY) const;

    // World-space origin of the selected object's gizmo (its transform position).
    glm::vec3 selectedGizmoOrigin() const;

    // Pixel length of one gizmo axis on screen, used to size handles + scale the
    // drag-to-world projection consistently regardless of zoom.
    float gizmoWorldScale() const;

    // Hit-test a gizmo handle at the drag-start (winX, winY) for the active tool.
    // Returns the axis index (0=X,1=Y,2=Z) the drag grabbed, 3 for the uniform/
    // center handle (Scale only), or -1 if no handle was hit. Move/Scale test
    // proximity to the projected axis segment; Rotate tests proximity to the
    // projected ring radius. Records the handle rects in the LayoutRegistry.
    int pickGizmoHandle(double winX, double winY) const;

    // Apply a gizmo drag (active tool + grabbed axis) given the cursor motion
    // from the press point. Mutates the selected object's transform and rebuilds
    // its viewport geometry. `startWin` is the press position, `curWin` the
    // current cursor; the deltas are projected onto the axis (Move/Scale) or
    // mapped to an angle around the gizmo origin (Rotate).
    void applyGizmoDrag(const glm::vec2& startWin, const glm::vec2& curWin);

    // Draw the transform gizmo for the selected object (Move/Rotate/Scale) into
    // the bound viewport FBO and register handle rects. No-op for Select / no
    // selection. Uses the line shader (world-space line geometry).
    void drawGizmo(const glm::mat4& view, const glm::mat4& proj);

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

    // Translate one {"type": ...} JSON event (the inject_input / play_input
    // schema) into an InputEvent and feed it through dispatchInputEvent on the
    // main/GL thread. Returns false and fills `errOut` on a malformed event.
    // Shared by inject_input (batch into one frame) and play_input (one event at
    // its scheduled time across frames).
    bool applyInputEventJson(const nlohmann::json& e, std::string& errOut);

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

    // The active viewport tool (top toolbar). Defaults to Select.
    Tool m_tool = Tool::Select;

    // ----- transform gizmo drag state -------------------------------------
    // When a left-press over the viewport lands on a gizmo handle, we capture the
    // grabbed axis and the object's transform at press time, then mutate the
    // transform as the cursor moves (suppressing camera orbit for the drag).
    bool m_gizmoDragging = false;
    int m_gizmoAxis = -1;                 // 0=X,1=Y,2=Z, 3=uniform/center
    glm::vec2 m_gizmoDragStart{0.0f};     // press position, window pixels
    glm::vec3 m_gizmoStartPosition{0.0f}; // selected object's transform at press
    glm::vec3 m_gizmoStartEuler{0.0f};
    glm::vec3 m_gizmoStartScale{1.0f};
    glm::vec3 m_gizmoOriginAtPress{0.0f}; // world-space gizmo origin at press

    // ----- click-vs-drag discrimination for left-button selection ---------
    // A left-press over the viewport that releases with little cursor motion is a
    // CLICK (ray-pick select); one that moves past a threshold is a DRAG (orbit).
    bool m_leftPressInViewport = false;   // a left-press landed over the viewport
    glm::vec2 m_leftPressStart{0.0f};     // its position, window pixels
    bool m_leftDragMoved = false;         // moved past the click threshold

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
