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
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

struct GLFWwindow;
class AutomationServer;
struct LoadedScene;  // renderer-side scene (SceneLoader.h), used for accurate picking

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
    // RENDER-TO-DISK across all scene cameras; returns the written file paths.
    nlohmann::json cmdRenderAll(const nlohmann::json& req);
    // Add a scene camera from the current orbit view; returns the new index/name.
    nlohmann::json cmdAddCamera(const nlohmann::json& req);
    // Edit a scene camera's per-camera settings (resolution / exposure / debug /
    // output path) by index. The single path the panel widgets and puppet share.
    nlohmann::json cmdSetCameraSettings(const nlohmann::json& req);
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
    // Serialize the model with the live orbit camera baked in.
    //   singleCameraForPreview == true  (Preview / pick): emit EXACTLY ONE camera
    //     (the orbit view), stripping any configured scene cameras.
    //   singleCameraForPreview == false (Save): keep all configured cameras and
    //     update only the primary to the orbit view.
    nlohmann::json serializeWithLiveCamera(bool singleCameraForPreview = true);

    // Serialize the model for the to-disk RENDER: all configured scene cameras,
    // each with its own resolution / exposure / debug filters, NO orbit override.
    // Synthesizes a single camera from the orbit view only if the scene has none.
    nlohmann::json serializeForRender();

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

    // Draw a silhouette OUTLINE around the selected object (its selection
    // indicator), using a two-pass stencil technique into the bound viewport
    // FBO: pass 1 marks the object's pixels in the stencil buffer; pass 2 redraws
    // the object enlarged (scaled about its center) in a flat outline color, only
    // where the stencil is unmarked, leaving a clean edge ring. No-op when nothing
    // is selected. Reads clearly on both meshes and sphere proxies.
    void drawSelectionOutline(const glm::mat4& view, const glm::mat4& proj);

    // ===== Viewport ray-picking + transform gizmos ========================
    // Build a world-space ray from a click at (winX, winY) in window pixels,
    // using the orbit camera and the current viewport FBO rect. Fills `origin`
    // and `dir` (normalized). Returns false if the point is outside the viewport.
    bool viewportRay(double winX, double winY, glm::vec3& origin, glm::vec3& dir) const;

    // Project a world point to window pixels via the orbit camera + viewport rect.
    // Returns false if behind the camera. Used to place/pick gizmo handles.
    bool projectToWindow(const glm::vec3& world, glm::vec2& outWin) const;

    // Ray-pick the scene objects at a window-pixel click: nearest object the ray
    // hits. Uses the RENDERER's real intersection (triangle-accurate for meshes,
    // exact for spheres) via a cached renderer-side scene (see ensurePickScene),
    // so a click on a thin mesh selects exactly what the renderer would shade —
    // not its bounding box. Returns the object index or -1 on a miss (empty
    // space). Falls back to the AABB/sphere proxy pick if the renderer scene
    // can't be built. Selecting/deselecting is the caller's job.
    int pickObject(double winX, double winY) const;

    // The AABB/sphere bounding-proxy pick (the previous picking path). Retained as
    // a fallback for when the renderer-side scene is unavailable (e.g. an empty /
    // un-serializable model). Returns the object index or -1.
    int pickObjectProxy(double winX, double winY) const;

    // Build (or reuse) the renderer-side scene used for accurate picking. The
    // scene is the SAME representation the render path builds: the live model is
    // serialized to JSON (serializeWithLiveCamera) and parsed by SceneLoader into
    // real Volume objects with a BVH, kept in m_pickScene. It is rebuilt lazily
    // whenever the model changes (m_pickSceneDirty, set by buildSceneGl), so it
    // stays in sync with inserts/deletes/transforms/material/mesh edits. The
    // renderer object names match the editor object names (both are the JSON
    // $scene keys), which is how a hit maps back to an editor object index.
    // Returns true if a usable scene is available. Non-const: it caches.
    bool ensurePickScene();

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

    // The Material Manager panel: lists the scene's materials (name + a small
    // type/color swatch), with create / duplicate / rename / delete operations and
    // an "Assign to selected object" button. Selecting a material row makes it the
    // active material context (m_selectedMaterial) and switches the properties
    // panel into material-edit mode. Registers panel_materials, material_row_<name>,
    // and the material op-button rects in the LayoutRegistry.
    void drawMaterialManager();

    // The material-edit form (shown in the properties panel when a material is the
    // active context): type dropdown, color picker, and IOR (Glass). Edits flow
    // through the model mutators so every object referencing the material updates.
    // Registers mat_type / mat_color / mat_ior.
    void drawMaterialEditForm(const std::string& materialName);

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

    // ===== Material operations (the single path GUI + puppet go through) =====
    // These mutate m_scene.materials directly, the central place to manage
    // materials independent of any one object. Each rebuilds viewport geometry so
    // every object referencing an edited material updates in the viewport.

    // Create a new default Lambertian material with a unique name. Returns the
    // new material's name and makes it the active material context.
    std::string createMaterial();

    // Duplicate the named material (copying type/color/ior) under a unique name.
    // Returns the new name (empty if the source doesn't exist).
    std::string duplicateMaterial(const std::string& materialName);

    // Rename a material and repoint every object that referenced the old name.
    // Fails (returns false) if oldName is absent or newName is empty / already
    // taken by a different material.
    bool renameMaterial(const std::string& oldName, const std::string& newName);

    // Delete a material. If it is referenced by any object, the references are
    // reassigned to `reassignTo` (which must exist) — pass empty to refuse the
    // delete when the material is in use. Returns true if deleted.
    bool deleteMaterial(const std::string& materialName, const std::string& reassignTo);

    // Set a field on a material BY NAME (not via an object). `field` is one of:
    //   type (string: Lambertian|Mirror|Glass|Microfacet),
    //   color_r|color_g|color_b (numeric), ior (numeric).
    // Rebuilds viewport geometry. Returns true if applied. The material-edit form
    // and the create/edit puppet command both call this.
    bool setMaterialType(const std::string& materialName, const std::string& type);
    bool setMaterialFloatField(const std::string& materialName, const std::string& field,
                               float value);

    // Assign the active material context (m_selectedMaterial) to the object at
    // `index`. Returns true if both exist and the assignment applied.
    bool assignMaterialToObject(const std::string& materialName, int index);

    nlohmann::json cmdCreateMaterial(const nlohmann::json& req);
    nlohmann::json cmdSetMaterial(const nlohmann::json& req);
    nlohmann::json cmdAssignMaterial(const nlohmann::json& req);

    // Bind a MeshVolume object at `index` to a specific OBJ sub-shape (and,
    // optionally, register a specific OBJ file). `mesh_shape` sets the $mesh
    // sub-shape name the volume draws (e.g. "Left"/"Right"/"Ceiling" in
    // CornellBox.obj); `mesh_file` ensures the given OBJ (resolved upwards if
    // relative) is in the model's $meshes list so the sub-shape resolves in both
    // the viewport and the serialized render. Returns true if applied. Same
    // mutator the (future) properties-panel mesh picker would call.
    bool setObjectMeshFile(int index, const std::string& meshFile);
    bool setObjectMeshShape(int index, const std::string& shapeName);

    // The right-side render-config panel (global settings + camera list +
    // per-camera settings + Preview/Render buttons). Replaces the old bottom
    // render display panel.
    void drawRenderPanel();

    // Per-camera settings form for the camera at `index` (resolution, exposure,
    // debug filters, output path). Drawn inside the render panel.
    void drawCameraSettings(int index);

    // "Add camera from current view": drop the live orbit view into a new scene
    // camera and select it. Returns the new camera's index.
    int addCameraFromCurrentView();

    // Phase 3: kick off a PREVIEW path-traced render (orbit view -> in-viewport
    // progressive overlay) on a worker thread and poll its completion in the UI
    // loop.
    void startRender();
    void pollRender();
    void uploadRenderTexture();

    // RENDER-TO-DISK: render ALL scene cameras (each at its own resolution /
    // exposure / debug filters) via the renderer's multi-camera path and write
    // each camera's image to its resolved output path. Populates
    // m_lastRenderOutputs. Runs synchronously on the calling (main) thread.
    void startFullRender();

    // Phase 4: render-as-viewport-overlay + live progressive preview.
    //
    // Overlay: when a render completes (or a progressive snapshot lands), the
    // result is drawn as a screen-filling textured quad INTO the viewport FBO,
    // on top of the live GL scene (drawOverlay below, called at the end of
    // renderViewport). The overlay is DISMISSED the moment the user touches the
    // view or selection (orbit/pan/zoom/gizmo/select) — see dismissRenderOverlay,
    // called from the input handlers and the set_camera automation command — at
    // which point the live grid/gnomon/objects/gizmos show again. m_overlayShown
    // gates both the FBO draw and the get_state "render_overlay_shown" flag.
    //
    // Live progressive tap: while the render thread runs, a preview callback
    // (passed to Renderer::renderFrame) snapshots the in-progress splat buffer a
    // few times/sec, tonemaps it with PROGRESSIVE EXPOSURE (scaling by
    // total/emitted-so-far so brightness is stable as photons accumulate, not
    // dark-then-bright), and stages the RGBA for the main thread to upload. The
    // snapshot reads the buffer's atomics atomically (no UB; minor torn reads
    // across pixels are visually acceptable for a preview).
    void drawOverlay();              // draw m_renderTex over the FBO (if shown)
    void dismissRenderOverlay();     // hide the overlay; also supersede a live render
    void uploadPreviewTexture();     // upload a staged progressive snapshot (main thread)
    void buildOverlayQuad();         // lazily create the fullscreen-quad VAO/VBO

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

    // The currently selected scene camera (index into m_scene.cameras), or -1.
    // The render panel's per-camera settings edit this camera. Defaults to the
    // first camera when a scene loads. NOTE: selection (which camera the panel
    // EDITS) is distinct from ACTIVATION (which viewpoint the viewport renders
    // FROM) — see m_activeCamera below.
    int m_selectedCamera = -1;

    // ----- active scene camera (Cinema-4D "active camera") ----------------
    // The scene camera the viewport currently renders FROM, or -1 for the editor
    // (orbit) camera. Exactly one is active at a time. When -1, the viewport uses
    // the roaming orbit camera (m_camera) at its retained position; when >= 0, the
    // viewport renders from that scene camera's pose/FOV/projection and draws a
    // letterbox masking the viewport to the camera's aspect ratio.
    //
    // Mechanism: activating a scene camera SAVES the editor camera's orbit state
    // into m_savedEditorCamera and drives the orbit rig (m_camera) to the scene
    // camera's authored pose (via OrbitCamera::setFromAuthoredCamera). While
    // active, orbit navigation moves the rig and is written BACK to the active
    // CameraDesc each frame (C4D moves the active camera when you navigate).
    // Deactivating restores m_savedEditorCamera so the editor camera returns to
    // exactly where it was left.
    int m_activeCamera = -1;
    OrbitCamera m_savedEditorCamera;       // editor-camera pose while a shot is active
    bool m_haveSavedEditorCamera = false;  // whether m_savedEditorCamera holds a pose

    // Activate the scene camera at `index` (drives the orbit rig to its pose and
    // saves the editor camera). Pass -1 to deactivate (restore the editor camera).
    // Toggling the already-active index deactivates. No-op on an invalid index.
    void setActiveCamera(int index);

    // Write the live orbit rig pose back into the active CameraDesc (position /
    // euler / fov), using the same convention serializeWithLiveCamera bakes. Call
    // after navigation while a scene camera is active so the shot tracks the view.
    void syncActiveCameraFromOrbit();

    // The viewport's effective aspect ratio: the active scene camera's
    // width/height when one is active, else the FBO aspect. Drives the letterbox
    // and the preview-render region.
    float viewportAspect() const;

    // Compute the letterboxed sub-rect of the FBO (in FBO pixels, bottom-left
    // origin for GL) that matches `aspect`, centered in the FBO. When the FBO is
    // wider than the target it pillarboxes (bars left/right); taller, letterboxes
    // (bars top/bottom). Returns the full FBO when no camera is active.
    void letterboxRegion(float aspect, int& x, int& y, int& w, int& h) const;

    // ----- renderer-side scene for accurate picking -----------------------
    // The same representation the render path builds (real Volume objects + BVH),
    // cached so a viewport left-click can cast the unprojected world ray with the
    // renderer's true intersection and select the object the ray hits NEAREST
    // (triangle-accurate for meshes). Rebuilt lazily by ensurePickScene() when
    // m_pickSceneDirty is set — buildSceneGl() sets it on every model change, so
    // the pick scene tracks inserts/deletes/transforms/material/mesh edits. Held
    // by pointer so EditorApp.h need not include the renderer scene headers.
    std::unique_ptr<LoadedScene> m_pickScene;
    bool m_pickSceneDirty = true;

    // ----- properties-panel arbitration (object vs. material) -------------
    // The editor has two independent "what is selected" notions: an object (via
    // the explorer / viewport pick) and a material (via the Material Manager). The
    // Properties panel can only show one editable form at a time, so a small mode
    // enum decides which it renders this frame. Selecting an object row switches to
    // Object mode (transform + kind fields + its material, read-mostly, with a
    // jump-to-material button). Selecting a material row switches to Material mode
    // (the material's type/color/ior, editable; changes propagate to every object
    // referencing it). m_selectedMaterial is the active material context by name
    // ("" = none); it persists across object selections so "Assign" has a target.
    enum class PropertiesMode
    {
        Object,
        Material
    };
    PropertiesMode m_propertiesMode = PropertiesMode::Object;
    std::string m_selectedMaterial;  // active material context (name), "" = none

    // Scratch buffer backing the rename text field in the Material Manager.
    char m_materialRenameBuf[128] = {0};

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

    // ----- left-button selection press tracking ---------------------------
    // A left-press over the viewport (that did not grab a gizmo handle) is a
    // pending selection: on release we ray-pick the object under the cursor.
    // Left no longer orbits, so there is no click-vs-drag arbitration to do.
    bool m_leftPressInViewport = false;   // a left-press landed over the viewport
    glm::vec2 m_leftPressStart{0.0f};     // its position, window pixels
    bool m_leftDragMoved = false;         // retained for gizmo-drag bookkeeping

    // Raster viewport state.
    RasterMesh m_mesh;
    OrbitCamera m_camera;
    ViewportGrid m_grid;             // XZ ground grid + origin gnomon
    unsigned int m_shaderProgram = 0;  // mesh shader
    unsigned int m_lineProgram = 0;    // flat line shader (grid/gnomon)
    unsigned int m_outlineProgram = 0; // flat-color shader (selection mask pass)
    unsigned int m_outlineEdgeProgram = 0;  // screen-space outline edge pass

    // ----- selection-outline mask FBO -------------------------------------
    // The selected object is rendered as a white silhouette into this single-
    // channel (R8) offscreen target, then a fullscreen edge pass reads it to
    // paint a uniform-width outline ring into the viewport FBO. Sized to match
    // the viewport FBO (rebuilt in resizeFbo). This screen-space approach gives a
    // crisp, constant-thickness outline on any shape — including thin meshes
    // (e.g. the knot) where a scaled-geometry silhouette would over-fill.
    unsigned int m_outlineMaskFbo = 0;
    unsigned int m_outlineMaskTex = 0;
    unsigned int m_fbo = 0;
    unsigned int m_fboColorTex = 0;
    unsigned int m_fboDepthRbo = 0;
    int m_fboWidth = 0;
    int m_fboHeight = 0;
    float m_meshColor[3] = {0.75f, 0.75f, 0.78f};

    // Viewport navigation input tracking. Orbit = right-drag, pan = middle-drag
    // or shift+right-drag, zoom = scroll. Left is reserved for selection +
    // gizmo-handle dragging.
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
    // Final render buffer. The render thread WRITES these (off the main thread)
    // when a render completes; the main thread READS them (GL upload, screenshot,
    // automation status, saveState) and the preview-upload path WRITES the dims.
    // Guarded by m_renderMutex — the same mutex discipline m_previewRgba already
    // has — because the RGBA vector and the dims are plain (non-atomic) members
    // shared across threads. Without it the render thread's write at completion
    // races the UI's unguarded reads (data race, TSan-detectable).
    std::mutex m_renderMutex;
    std::vector<uint8_t> m_renderRgba;  // populated by render thread, uploaded on main thread
    int m_renderWidth = 0;
    int m_renderHeight = 0;
    unsigned int m_renderTex = 0;
    std::string m_scenePath;  // scene JSON to render (defaults to MirrorTest.json if found)
    std::string m_lastRenderScenePath;  // temp scene file emitted by the last render
    int m_renderResolution = 256;
    int m_renderPhotonsMillions = 4;

    // Global renderer settings exposed in the right render panel. bounceThreshold
    // is the hard per-photon bounce-depth cap; terminationThreshold is the Russian-
    // roulette termination probability floor. Both feed the render scene settings.
    int m_bounceThreshold = 4;
    float m_terminationThreshold = 0.0f;

    // Absolute paths of the files the last to-disk Render wrote, shown in the
    // render panel and returned by the render_all automation command.
    std::vector<std::string> m_lastRenderOutputs;

    // Phase 4: render-as-viewport-overlay state.
    // True while the render texture should be drawn over the viewport FBO.
    // Set when a render starts/completes; cleared by dismissRenderOverlay() on
    // any view/selection interaction.
    bool m_overlayShown = false;
    // Fullscreen-quad geometry for drawing the render texture into the FBO.
    unsigned int m_overlayProgram = 0;
    unsigned int m_overlayVao = 0;
    unsigned int m_overlayVbo = 0;

    // Phase 4: live progressive preview state.
    // m_renderCancel is observed by the render thread's preview callback (via the
    // ProgressCallback return) to supersede an in-progress render when the user
    // interacts. m_renderGeneration bumps on every startRender so a stale thread's
    // late snapshot can be ignored.
    std::atomic<bool> m_renderCancel{false};
    std::atomic<uint64_t> m_renderGeneration{0};
    // Staged progressive snapshot: the render thread fills m_previewRgba (+ dims)
    // and sets m_previewDirty; the main thread uploads it to m_renderTex. Guarded
    // by m_previewMutex because the RGBA vector is not atomic (it is produced off
    // the main thread and consumed on it).
    std::mutex m_previewMutex;
    std::vector<uint8_t> m_previewRgba;
    int m_previewWidth = 0;
    int m_previewHeight = 0;
    std::atomic<bool> m_previewDirty{false};

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
