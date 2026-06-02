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
| `get_state` | — | Mesh path/label, camera (eye/target/fov/yaw/pitch/distance), render settings + status, viewport + window sizes. |
| `load_mesh` | `path` | Load an OBJ into the viewport (GL upload + reframe camera). |
| `set_camera` | any of `eye[3]`, `target[3]`, `fov`, `orbit_yaw`, `orbit_pitch`, `dolly` | Move the orbit camera. `eye` is converted to yaw/pitch/distance about `target`. |
| `set_render_settings` | any of `resolution`, `photons` (millions), `scene_path` | Configure the path-traced render. |
| `render` | `wait` (default true), `timeout` (s, default 600) | Path-trace `scene_path` on a worker thread. With `wait`, blocks until done. |
| `screenshot` | `path`, `target` (`window`\|`viewport`\|`render`) | Capture pixels to an 8-bit RGBA PNG. |
| `quit` | — | Acknowledge, then shut the editor down. |

Screenshot targets:
- `window` — the full editor window incl. ImGui panels (`glReadPixels` of the default framebuffer).
- `viewport` — just the raster viewport FBO (the orbiting mesh).
- `render` — the last path-traced image (requires a prior `render`).

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
