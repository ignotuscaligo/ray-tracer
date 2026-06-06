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
| `set_camera` | any of `eye[3]`, `target[3]`, `fov`, `orbit_yaw`, `orbit_pitch`, `dolly` | Move the orbit camera. `eye` is converted to yaw/pitch/distance about `target`. |
| `set_render_settings` | any of `resolution`, `photons` (millions), `scene_path` | Configure the path-traced render. |
| `render` | `wait` (default true), `timeout` (s, default 600) | Path-trace the loaded scene **from the LIVE orbit camera** (eye/target/fov are baked into a temp scene next to the source) on a worker thread. With `wait`, blocks until done. |
| `screenshot` | `path`, `target` (`window`\|`viewport`\|`render`) | Capture pixels to an 8-bit RGBA PNG. |
| `inject_input` | single event obj, or `events` (array) | Push InputEvent(s) through the editor's single input path (drives camera nav + ImGui). |
| `query_layout` | `name` (optional) | Pixel rect(s) of named UI elements recorded last frame. With `name`: one rect; without: all. |
| `quit` | — | Acknowledge, then shut the editor down. |

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
python3 editor/tools/editor_client.py --port 8780 click 800 280
python3 editor/tools/editor_client.py --port 8780 drag 800 280 1000 340 --steps 10
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
    c.inject_drag(cx, cy, cx + 200, cy + 60)  # orbits the camera
    c.inject_click(cx, cy)
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
