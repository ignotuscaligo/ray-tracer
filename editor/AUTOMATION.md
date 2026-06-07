# Editor Automation

A localhost command port that lets an external process (e.g. an AI agent) drive
the Ray Tracer Editor GUI and capture screenshots of what is on screen. This is
how an agent verifies the GUI actually renders: load a mesh, take a screenshot,
inspect the PNG.

## Launch with the automation port

```bash
cmake --preset conan-release && cmake --build --preset conan-release
./build/Release/editor --automation-port 8780 &
```

The editor opens its GLFW window, creates an OpenGL 3.2 core context, and starts
a command server bound to **127.0.0.1 only** (never `0.0.0.0` — no external
exposure). On success stderr prints:

```
AutomationServer listening on 127.0.0.1:8780
```

A GL context (a window/display session) is required. With no display, window
creation fails and the editor exits before the port starts. If the chosen port
is already in use, the editor logs `bind(127.0.0.1:PORT) failed: Address already
in use` and continues *without* the port — pick a free port or kill the stale
process (`lsof -nP -iTCP:8780`).

You can also pass an initial mesh: `./build/Release/editor --automation-port 8780 path/to/mesh.obj`.

### Headless mode

Add `--headless` to create the GLFW window **hidden** (`GLFW_VISIBLE=false`) so
automation runs without popping a visible window:

```bash
./build/Release/editor --headless --automation-port 8780 &
```

A real GL context, the viewport FBO, and the full ImGui UI are still created and
rendered every frame, so `render`, `screenshot` (all targets), `inject_input`,
and `query_layout` all work exactly as with a visible window — the window just
never appears. This is the recommended mode for agent-driven validation.

## Protocol

Line-delimited JSON over TCP: the client writes one JSON request object per line
(`\n`-terminated) and reads one JSON response object per line. Every response has
a boolean `ok`; on failure it carries an `error` string.

Commands run on the editor's **main/GL thread**: the socket thread parses a
request, enqueues it, and blocks until the main loop's `drain()` executes the
handler and returns the response. This keeps all GL/ImGui access single-threaded.

| `cmd` | Fields | Effect |
|---|---|---|
| `ping` | — | Liveness check. Returns `pong`, `version`. |
| `get_state` | — | Mesh path/label, camera (eye/target/fov/yaw/pitch/distance), render settings + status, viewport + window sizes, **`scene` model summary (name/objects/material_count/camera_present), and `selected_object` / `selected_index`**. |
| `load_mesh` | `path` | Load an OBJ into the viewport (GL upload + reframe camera). |
| `load_scene` | `path` | Load a renderer scene JSON INTO the in-memory model (camera, materials, named objects), upload each object's geometry to the viewport, frame the orbit camera on the scene, and set it as the render target. Returns the object-name list. |
| `save_scene` | `path` | Serialize the in-memory model to renderer scene JSON at `path` (via `SceneModelSerializer`), baking the live orbit camera into the Camera block so the saved file renders framed as the viewport shows it. Backs File > Save. The saved JSON is directly renderable with `ray-tracer <path>`. |
| `set_camera` | any of `eye[3]`, `target[3]`, `fov`, `orbit_yaw`, `orbit_pitch`, `dolly` | Move the orbit camera. `eye` is converted to yaw/pitch/distance about `target`. |
| `set_render_settings` | any of `resolution`, `photons` (millions), `scene_path` | Configure the path-traced render. |
| `render` | `wait` (default true), `timeout` (s, default 600) | Path-trace the loaded scene **from the LIVE orbit camera** (eye/target/fov are baked into a temp scene next to the source) on a worker thread. With `wait`, blocks until done. |
| `screenshot` | `path`, `target` (`window`\|`viewport`\|`render`) | Capture pixels to an 8-bit RGBA PNG. |
| `inject_input` | single event obj, or `events` (array) | Push InputEvent(s) through the editor's single input path **within ONE frame** (drives camera nav + single-frame ImGui). For multi-frame interactions (menus, drags) use `play_input`. |
| `play_input` | `actions` (array of `{t_ms, event}`), `fps` (default 60), `tail_ms` (default 50) | Replay a TIMED input sequence across REAL frames: events fire at their scheduled `t_ms` and the editor advances one render frame per simulated tick, so multi-frame ImGui interactions (open a menu popup, hover to an item, drag a DragFloat) progress like a human's hand. Blocks until the sequence + tail completes; returns `fired`, `frames`, `cursor`, `camera`, `eye`. Same scheduler in windowed + headless. |
| `query_layout` | `name` (optional) | Pixel rect(s) of named UI elements recorded last frame. With `name`: one rect; without: all. |
| `new_scene` | — | File > New: reset to a pristine empty scene (the from-scratch start). |
| `insert_object` | `kind` (`SphereVolume`\|`MeshVolume`\|`AreaLight`\|`OmniLight`) | Insert an object via the SAME model-mutation path as the Insert menu / explorer right-click, build its viewport geometry, and select it. Returns the new `index`, `name`, and full `object` detail. |
| `create_material` | optional `name`, `type`, `color[3]`, `ior` | Create a new material via the SAME path as the Material Manager's New button, then apply any of name/type/color/ior. Selects it as the active material context. Returns the new `name`, `material` detail, and `material_count`. |
| `set_material` | `name`, `field`, `value` | Edit a material BY NAME (the twin of the material-edit form). `field` is `type` (string Lambertian\|Mirror\|Glass\|Microfacet) or `color_r`/`color_g`/`color_b`/`ior` (numeric). Every object referencing the material updates. Returns the `material` detail. |
| `assign_material` | optional `name` (default active material), `index` (default selected object) | Assign a material to an object (the twin of the Material Manager's "Assign to selected object" button). Returns the `material_name` and updated `object`. |
| `set_property` | `field`, `value`, `index` (optional, default selected) | Edit one field of an object — the programmatic twin of the properties-panel widgets (both call the same model mutators). Numeric fields: `pos_x/y/z`, `rot_x/y/z`, `scale_x/y/z`, `radius`, `light_width`, `light_height`, `light_radius`, `mat_color_r/g/b`, `mat_ior`. String fields: `material_type` (`Lambertian`\|`Mirror`\|`Glass`\|`Microfacet`), `material_name` (assign an existing material), `mesh_file` (register an OBJ in `$meshes`; MeshVolume only — relative paths resolve upwards), `mesh_shape` (bind a MeshVolume to a named OBJ sub-shape, e.g. `Left`/`Right`/`Ceiling`/`Floor`/`Back` of CornellBox.obj). Returns the updated `object` detail. |
| `quit` | — | Acknowledge, then shut the editor down. |

`get_state` additionally reports `selected_detail` — the full transform + material
of the selected object — so an edit can be verified without a screenshot. `render`
reports `render_scene_path`, the temp scene file it emitted from the CURRENT model
(so inserts/edits are in the render, not just the originally-loaded JSON).

### Object insertion + the properties panel (2b-2)

The Insert menu (`menu_Insert`), the scene-explorer right-click context menu, and
the `insert_object` command all funnel through one model-mutation path, so a puppet
builds a scene exactly as a human would. New objects get a default transform, a
fresh per-object material, and a unique name; they appear in the explorer + the
viewport and become selected immediately.

The properties panel (`panel_properties`) edits the selected object. Each editable
widget registers its rect: `prop_pos_x/y/z`, `prop_rot_x/y/z`, `prop_scale_x/y/z`,
`prop_radius` (sphere), `prop_light_width`/`prop_light_height`/`prop_light_radius`
(area light), `prop_material_type`, `prop_material_color`, `prop_material_ior`
(glass). The Insert-menu item rects (`menu_insert_sphere`/`_mesh`/`_area_light`/
`_omni_light`) are recorded while the menu popup is open.

GUI widgets are the real feature. Two ways to drive them:
- **`play_input` + gestures (drive the actual widget):** a menu popup or a
  DragFloat is a *multi-frame* interaction — the popup only appears the frame
  AFTER its header is clicked, and a DragFloat only integrates motion across
  successive frame deltas — so a single batched `inject_input` (one frame) cannot
  reproduce them. `play_input` schedules events across real frames and is the
  proven path for menu-driven insert and DragFloat edits (see "Timed multi-frame
  input" below).
- **`insert_object` / `set_property` (robust fallback):** the programmatic twins
  of the same model mutators the widgets call, for when you want determinism
  without simulating the gesture. Both paths mutate the model identically.

### Material Manager (2b-3)

The **Material Manager** (`panel_materials`) is the central place to manage the
scene's materials, distinct from per-object inline editing. It lists every
material by name with a color swatch, a `[type]` tag, and a `xN` use-count, plus
op buttons: New (`button_material_new`), Duplicate (`button_material_duplicate`),
Delete (`button_material_delete`), a rename field + button
(`material_rename_field` / `button_material_rename`), and
`button_assign_material_to_object`. Each material row records
`material_row_<name>` and a positional `material_row_index_<i>`.

**Properties-panel arbitration (object vs. material).** The Properties panel
shows ONE editable form at a time, chosen by a mode that follows the last
selection:

- Selecting an **object** (explorer row, viewport pick, or insert) → *object
  mode*: the object's transform + kind fields + its material inline
  (`prop_material_type`/`_color`/`_ior`), with `button_edit_material` (jump into
  the material's edit context) and, when a different material is the active
  context, `button_assign_material` (assign it to this object).
- Selecting a **material** (a Material Manager row, or clicking New/Duplicate) →
  *material mode*: the material's editable type / color / IOR
  (`mat_type` combo with per-item `mat_type_<lambertian|mirror|glass|microfacet>`
  rects recorded while the popup is open, `mat_color`, `mat_ior`), plus
  `button_assign_material` to assign it to the selected object. Edits flow
  through the by-name material mutators, so EVERY object referencing the material
  updates and the viewport/render reflect it.

`m_selectedMaterial` (the active material context) persists across object
selections so "Assign" always has a target. `get_state` reports `selected_material`,
`properties_mode` (`object`\|`material`), and a full `scene.materials` list
(name/type/color/ior/use_count).

**Material operations + safety.** Rename repoints every referencing object and
refuses a name already taken by a different material. Delete refuses an in-use
material unless its references can be reassigned (the GUI auto-reassigns to the
first other material; an only-and-used material is protected). The
`create_material` / `set_material` / `assign_material` commands are the
programmatic twins of these GUI buttons and go through the same model mutators.

Screenshot targets:
- `window` — the full editor window incl. ImGui panels (`glReadPixels` of the default framebuffer).
- `viewport` — just the raster viewport FBO (the orbiting mesh).
- `render` — the last path-traced image (requires a prior `render`).

## The input abstraction (how injected input drives the app)

The editor has exactly **one input path**. Two sources feed it, and everything
downstream is identical regardless of source:

```
  GLFW callbacks (real OS events) ─┐
                                   ├─► EditorApp::dispatchInputEvent(InputEvent)
  inject_input (port) ────────────┘        │
                                           ├─► feeds ImGui IO (AddMousePosEvent,
                                           │   AddMouseButtonEvent, AddMouseWheelEvent,
                                           │   AddKeyEvent, AddInputCharacter)
                                           └─► app handlers (camera orbit/pan/zoom,
                                               future UI handlers)
```

`InputEvent` (see `editor/InputEvent.h`) is a tagged struct: `MouseMove {x,y}`,
`MouseButton {button,down,mods}`, `Scroll {dx,dy}`, `Key {key,down,mods}`,
`Char {codepoint}`. The GLFW callbacks only *translate* raw events into
`InputEvent`s; they hold no app logic. The port *injects* `InputEvent`s on the
main/GL thread, exactly as a real OS event would arrive. App code never reads raw
GLFW input.

Key subtlety: `imgui_impl_glfw`'s automatic callback installation is **disabled**
(`ImGui_ImplGlfw_InitForOpenGL(window, false)`). The app owns the GLFW callbacks
and feeds ImGui's IO itself, so injected and real events both reach ImGui. Mouse
pos/button/scroll are fed via the public `io.AddXxx` functions; key/char events
are forwarded through the backend's own `ImGui_ImplGlfw_KeyCallback` because that
function owns the GLFW→`ImGuiKey` translation table (which is not otherwise
exported). `ImGui_ImplGlfw_NewFrame()` still runs each frame for display-size /
delta-time / cursor-shape bookkeeping — it just no longer receives input.

### `inject_input`

Coordinates are **window/framebuffer pixels, top-left origin** — the same space
`query_layout` returns. Send one event object, or a batch via `events`:

```json
{"cmd": "inject_input", "events": [
  {"type": "mouse_move", "x": 800, "y": 280},
  {"type": "mouse_button", "button": 0, "down": true},
  {"type": "mouse_move", "x": 1000, "y": 340},
  {"type": "mouse_button", "button": 0, "down": false}
]}
```

Event types: `mouse_move {x,y}`, `mouse_button {button (0=left,1=right,2=middle),
down, mods?}`, `scroll {dx?, dy}`, `key {key (GLFW code), down, mods?}`,
`char {codepoint}`. The response echoes `applied`, the resulting `cursor`, and
the `camera` yaw/pitch + `eye` so an orbit can be confirmed in one round-trip.

A batched `inject_input` lands entirely within one frame's `drain()`, matching
how a real drag's events arrive between frames — so a full press→moves→release
gesture orbits the camera just like a hand-driven drag.

### Viewport mouse conventions (which button does what)

The viewport reserves the LEFT button for selection + gizmos so it never fights
the orbit gesture:

| Gesture | Effect |
|---|---|
| RIGHT-drag (`button: 1`) | Orbit the camera (yaw/pitch). |
| shift + RIGHT-drag | Pan (track/truck the target in the view plane). |
| MIDDLE-drag (`button: 2`) | Pan. |
| LEFT-click (`button: 0`) | Select the object under the cursor via the renderer ray-pick; empty space deselects. |
| LEFT-drag on a gizmo handle | Drag that transform handle (Move/Rotate/Scale tools). |
| scroll | Zoom (dolly). |

So injected input drives both halves: send a `button: 1` press→moves→release to
orbit, and a `button: 0` click to select / a `button: 0` drag on a handle to move
the gizmo. The `editor_client.py` `click` / `click_drag` gesture helpers and the
`timed-click` / `click-drag` CLI subcommands take a `button` argument (default 0)
so a puppet can orbit (`button=1`) or select/gizmo (`button=0`) through the same
timed-input path.

### `query_layout`

Each frame the UI build records a registry of `{element_name -> pixel rect}`
(see `editor/LayoutRegistry.h`). `query_layout` reads it. With `name`, returns
that element; without, returns all. Rects carry `x,y,width,height` and a
`center: [cx,cy]` an agent can click directly.

Currently registered names: `menu_bar`, `menu_File`, `menu_Insert`,
`panel_controls`, `panel_render`, `panel_explorer`, `button_render`, `viewport`,
plus the scene-explorer rows: `explorer_row_camera`, `explorer_row_<objectName>`
(e.g. `explorer_row_MirrorKnot`, `explorer_row_Light`), and a positional alias
`explorer_row_index_<i>`. Clicking a row's `center` selects that object (verify
via `get_state.selected_object`). The viewport rect doubles as the gate the nav
handlers use to decide whether a press/scroll is a viewport gesture, so injected
and real input are gated against the same rect. Future panels/widgets register
the same way (one `m_layout.record(...)` call as the widget is submitted).

### Timed multi-frame input (`play_input`) and gestures

`inject_input` applies all its events inside a single frame's `drain()`. That is
right for a viewport drag (the camera handler reads the abstraction's own cursor
each event) but WRONG for ImGui interactions that span frames:

- a **menu popup** is submitted by `BeginMenu` only on the frame *after* its
  header is clicked, so its item rects don't even exist yet within the click frame;
- a **DragFloat** integrates `io.MousePos - io.MousePosPrev` per `NewFrame`, so its
  value only changes if the button is held while the cursor moves across many frames.

`play_input` solves this. Request:

```json
{"cmd": "play_input", "fps": 60, "tail_ms": 50, "actions": [
  {"t_ms": 0,    "event": {"type": "mouse_move",   "x": 77, "y": 9}},
  {"t_ms": 16,   "event": {"type": "mouse_button", "button": 0, "down": true}},
  {"t_ms": 116,  "event": {"type": "mouse_button", "button": 0, "down": false}}
]}
```

**Scheduling model (identical windowed + headless).** The editor walks a virtual
clock in fixed `1000/fps` ms steps. Each step it fires every action whose `t_ms`
is due (in `t_ms` order), then advances exactly ONE real render frame — a full
`NewFrame()` / `drawUi()` / `renderViewport()` / `Render()` / present — so ImGui
sees the elapsed frame and popups/drags make progress. It does NOT re-drain the
automation queue while stepping (the handler is already running inside `drain()`),
so the in-flight sequence owns the frames it needs and other commands wait until
it returns. The handler blocks for the whole gesture plus `tail_ms`, then returns
`{fired, frames, cursor, camera, eye}` — so the caller can `get_state` /
`query_layout` immediately afterward and see the result.

**Frame stepping in headless.** Headless steps the same per-frame work as a
windowed loop (the window is just hidden), so ImGui state advances normally.

**Headless mouse fix.** With a hidden window and the backend's auto-callbacks
disabled, `ImGui_ImplGlfw_NewFrame()` would overwrite `io.MousePos` with the real
OS cursor every frame, clobbering the injected position — which is why injected
clicks orbited the camera (app state reads our own cursor) but never opened menus
or activated widgets (ImGui hover is computed from the clobbered `io.MousePos`).
In headless mode the editor re-asserts the injected cursor position after the
backend frame and before `ImGui::NewFrame()`. Headless also disables `imgui.ini`
so a stale persisted layout can't pin a window over the menu bar (which would make
the menu un-hoverable); windows are positioned below the menu bar each launch.

**Menu-item rect registration.** Each Insert-menu item records its rect
(`menu_insert_sphere`/`_mesh`/`_area_light`/`_omni_light`) *while the popup is
open*. `play_input` keeps the popup open across frames, so a `query_layout` right
after opening the menu finds the item rect to click.

The `editor_client.py` gesture helpers (below) are built on `play_input`:
`click`, `click_drag`, and `menu_pick`.

## Python client

`editor/tools/editor_client.py` (stdlib only — `socket`, `json`, `argparse`).

CLI:

```bash
python3 editor/tools/editor_client.py --port 8780 ping
python3 editor/tools/editor_client.py --port 8780 get-state
python3 editor/tools/editor_client.py --port 8780 load-mesh /abs/path/to/mesh.obj
python3 editor/tools/editor_client.py --port 8780 set-camera --orbit-yaw 0.5 --dolly 0.8
python3 editor/tools/editor_client.py --port 8780 set-render-settings --resolution 256 --photons 4
python3 editor/tools/editor_client.py --port 8780 render --wait
python3 editor/tools/editor_client.py --port 8780 screenshot /tmp/shot.png --target viewport
python3 editor/tools/editor_client.py --port 8780 query-layout viewport
python3 editor/tools/editor_client.py --port 8780 new-scene
python3 editor/tools/editor_client.py --port 8780 insert-object AreaLight
python3 editor/tools/editor_client.py --port 8780 set-property pos_y 200 --index 0
python3 editor/tools/editor_client.py --port 8780 set-property material_type Mirror
python3 editor/tools/editor_client.py --port 8780 click 800 280
python3 editor/tools/editor_client.py --port 8780 drag 800 280 1000 340 --steps 10
# Timed multi-frame gestures (drive the REAL widgets):
python3 editor/tools/editor_client.py --port 8780 timed-click 77 9.5
python3 editor/tools/editor_client.py --port 8780 click-drag 66 482 266 482 --drag-ms 1000
python3 editor/tools/editor_client.py --port 8780 menu-pick Insert Sphere
python3 editor/tools/editor_client.py --port 8780 play-input '[{"t_ms":0,"event":{"type":"mouse_move","x":77,"y":9}}]'
python3 editor/tools/editor_client.py --port 8780 quit
```

Library:

```python
from editor_client import EditorClient
with EditorClient(port=8780) as c:
    c.ping()
    c.load_mesh("/abs/path/to/mesh.obj")
    print(c.get_state())
    c.screenshot("/tmp/shot.png", target="viewport")
    # Puppet: find a UI element, then drive it with injected input.
    vp = c.query_layout("viewport")["rect"]
    cx, cy = vp["center"]
    c.inject_drag(cx, cy, cx + 200, cy + 60)  # one-frame orbit (camera nav)
    # Multi-frame gestures that drive the ACTUAL widgets:
    c.menu_pick("Insert", "Sphere")           # open Insert menu, click Sphere
    rect = c.query_layout("prop_pos_x")["rect"]
    px, py = rect["center"]
    c.click_drag((px, py), (px + 200, py), drag_ms=1000)  # drag the DragFloat
    print(c.get_state()["selected_detail"]["position"])    # pos_x changed by drag
    c.render(wait=True)
    c.screenshot("/tmp/render.png", target="render")
    c.quit()
```

A non-`ok` response raises `EditorClientError`. A fresh TCP connection is opened
per command (the editor serves one client at a time).

## Script mode (no socket)

`./build/Release/editor --script commands.json` runs a list of command objects
(a bare array, or `{"commands": [...]}`) through the same handlers, prints each
response to stdout, then exits. Useful for deterministic one-shot runs.

## How an AI agent self-verifies the GUI

The point of this facility is to prove the GUI is genuinely rendering, not just
that the process launched. Procedure:

1. Launch: `./build/Release/editor --automation-port 8780 &` and wait for the
   `listening on 127.0.0.1:8780` line.
2. `ping` — confirm the port answers.
3. `load_mesh` a known OBJ (e.g. `meshes/eschers_knot.obj`), then `get_state`
   and confirm `mesh_label` reports the expected triangle count and the viewport
   has non-zero size.
4. `screenshot` with `target=viewport` to a PNG.
5. **Inspect the PNG**: parse its IHDR for dimensions, decode the pixels, and
   check the image is non-blank — more than one distinct color, and a non-trivial
   min/max spread. A uniform image means the mesh did not draw. A rasterized mesh
   on the dark viewport background yields hundreds of distinct shaded colors.
6. `quit`.

Example non-blank check (stdlib only, no PIL): decode IDAT with `zlib`, undo the
per-scanline PNG filters, then assert `len(distinct_rgb) > 1` and `max > min`.
The bundled test `tests/test_PngExport.cpp` exercises the same encoder the
screenshot path uses.
