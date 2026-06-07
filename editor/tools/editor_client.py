#!/usr/bin/env python3
"""Dependency-light client for the Ray Tracer Editor automation port.

The editor, when launched with ``--automation-port N``, listens on
``127.0.0.1:N`` and speaks a line-delimited JSON protocol: the client writes one
JSON object per line (the request) and reads one JSON object per line (the
response). This module wraps that protocol so an agent can drive and inspect the
editor programmatically.

Usage as a library::

    from editor_client import EditorClient
    with EditorClient(port=8780) as c:
        print(c.ping())
        c.load_mesh("/abs/path/to/mesh.obj")
        c.set_camera(orbit_yaw=0.5, dolly=0.8)
        c.screenshot("/tmp/shot.png", target="viewport")
        c.render(wait=True)
        c.screenshot("/tmp/render.png", target="render")

Usage as a CLI::

    python3 editor_client.py --port 8780 ping
    python3 editor_client.py --port 8780 get-state
    python3 editor_client.py --port 8780 load-mesh /abs/mesh.obj
    python3 editor_client.py --port 8780 set-camera --orbit-yaw 0.5 --dolly 0.8
    python3 editor_client.py --port 8780 render --wait
    python3 editor_client.py --port 8780 screenshot /tmp/shot.png --target viewport
    python3 editor_client.py --port 8780 quit

Only the Python standard library is used (socket, json, argparse).
"""

from __future__ import annotations

import argparse
import json
import socket
import sys
from typing import Any, Dict, Optional


class EditorClientError(RuntimeError):
    """Raised when the editor returns a non-ok response or the link fails."""


class EditorClient:
    """A connection to the editor automation port.

    A fresh TCP connection is opened per command. The editor serves one client
    at a time and replies with exactly one JSON line per request, so per-command
    connections keep the protocol simple and robust to partial reads.
    """

    def __init__(self, host: str = "127.0.0.1", port: int = 8780, timeout: float = 605.0):
        self.host = host
        self.port = port
        self.timeout = timeout

    # ----- transport -------------------------------------------------------

    def _send(self, request: Dict[str, Any]) -> Dict[str, Any]:
        payload = (json.dumps(request) + "\n").encode("utf-8")
        with socket.create_connection((self.host, self.port), timeout=self.timeout) as sock:
            sock.settimeout(self.timeout)
            sock.sendall(payload)
            buf = bytearray()
            while b"\n" not in buf:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                buf.extend(chunk)
        line, _, _ = bytes(buf).partition(b"\n")
        if not line:
            raise EditorClientError(f"no response to {request!r}")
        return json.loads(line.decode("utf-8"))

    def _command(self, cmd: str, **kwargs: Any) -> Dict[str, Any]:
        request = {"cmd": cmd}
        request.update({k: v for k, v in kwargs.items() if v is not None})
        response = self._send(request)
        if not response.get("ok", False):
            raise EditorClientError(response.get("error", f"{cmd} failed: {response}"))
        return response

    # ----- context manager (no persistent socket, but convenient) ----------

    def __enter__(self) -> "EditorClient":
        return self

    def __exit__(self, *exc: Any) -> None:
        return None

    # ----- commands --------------------------------------------------------

    def ping(self) -> Dict[str, Any]:
        return self._command("ping")

    def get_state(self) -> Dict[str, Any]:
        return self._command("get_state")

    def load_mesh(self, path: str) -> Dict[str, Any]:
        return self._command("load_mesh", path=path)

    def load_scene(self, path: str) -> Dict[str, Any]:
        """Load a renderer scene JSON into the in-memory model + viewport.

        Populates the scene explorer with the scene's named objects, uploads each
        object's geometry for the orbit viewport, frames the camera on the scene,
        and sets it as the render target (render-from-current-view). Returns the
        loaded object-name list.
        """
        return self._command("load_scene", path=path)

    def save_scene(self, path: str) -> Dict[str, Any]:
        """Serialize the in-memory model to renderer scene JSON at `path`.

        Backs File > Save: the live orbit camera is baked into the saved Camera
        block so the file renders framed as the viewport shows it. The saved JSON
        is directly renderable with `ray-tracer <path>`.
        """
        return self._command("save_scene", path=path)

    def set_camera(
        self,
        eye: Optional[list] = None,
        target: Optional[list] = None,
        fov: Optional[float] = None,
        orbit_yaw: Optional[float] = None,
        orbit_pitch: Optional[float] = None,
        dolly: Optional[float] = None,
    ) -> Dict[str, Any]:
        return self._command(
            "set_camera",
            eye=eye,
            target=target,
            fov=fov,
            orbit_yaw=orbit_yaw,
            orbit_pitch=orbit_pitch,
            dolly=dolly,
        )

    def set_render_settings(
        self,
        resolution: Optional[int] = None,
        photons: Optional[int] = None,
        scene_path: Optional[str] = None,
    ) -> Dict[str, Any]:
        return self._command(
            "set_render_settings",
            resolution=resolution,
            photons=photons,
            scene_path=scene_path,
        )

    def render(self, wait: bool = True, timeout: Optional[float] = None) -> Dict[str, Any]:
        return self._command("render", wait=wait, timeout=timeout)

    def screenshot(self, path: str, target: str = "window") -> Dict[str, Any]:
        return self._command("screenshot", path=path, target=target)

    # ----- puppet: injected input + layout queries -------------------------

    def inject_input(self, events: list) -> Dict[str, Any]:
        """Push a list of InputEvent dicts through the editor's single input path.

        Each event is a dict with a ``type`` and type-specific fields:
          {"type": "mouse_move", "x": .., "y": ..}
          {"type": "mouse_button", "button": 0|1|2, "down": bool, "mods": int?}
          {"type": "scroll", "dx": float?, "dy": float}
          {"type": "key", "key": <glfw key code>, "down": bool, "mods": int?}
          {"type": "char", "codepoint": int}

        Coordinates are window pixels (top-left origin) — the same space
        ``query_layout`` returns rects in.
        """
        return self._command("inject_input", events=events)

    def inject_mouse_move(self, x: float, y: float) -> Dict[str, Any]:
        return self.inject_input([{"type": "mouse_move", "x": x, "y": y}])

    def inject_mouse_button(self, button: int, down: bool, mods: int = 0) -> Dict[str, Any]:
        return self.inject_input(
            [{"type": "mouse_button", "button": button, "down": down, "mods": mods}]
        )

    def inject_scroll(self, dy: float, dx: float = 0.0) -> Dict[str, Any]:
        return self.inject_input([{"type": "scroll", "dx": dx, "dy": dy}])

    def inject_key(self, key: int, down: bool, mods: int = 0) -> Dict[str, Any]:
        return self.inject_input([{"type": "key", "key": key, "down": down, "mods": mods}])

    def inject_char(self, codepoint: int) -> Dict[str, Any]:
        return self.inject_input([{"type": "char", "codepoint": codepoint}])

    def inject_click(self, x: float, y: float, button: int = 0) -> Dict[str, Any]:
        """Move to (x, y) then press + release `button` there — a full click."""
        return self.inject_input(
            [
                {"type": "mouse_move", "x": x, "y": y},
                {"type": "mouse_button", "button": button, "down": True},
                {"type": "mouse_button", "button": button, "down": False},
            ]
        )

    def inject_drag(
        self, x0: float, y0: float, x1: float, y1: float, steps: int = 8, button: int = 0
    ) -> Dict[str, Any]:
        """Press at (x0,y0), move in `steps` increments to (x1,y1), release.

        Sends one batched inject_input so the whole gesture lands in a single
        frame's drain — matching how a real drag's events arrive between frames.
        """
        events = [
            {"type": "mouse_move", "x": x0, "y": y0},
            {"type": "mouse_button", "button": button, "down": True},
        ]
        for i in range(1, steps + 1):
            t = i / steps
            events.append({"type": "mouse_move", "x": x0 + (x1 - x0) * t, "y": y0 + (y1 - y0) * t})
        events.append({"type": "mouse_button", "button": button, "down": False})
        return self.inject_input(events)

    # ----- puppet: TIMED multi-frame sequences + gestures ------------------

    def play_input(
        self, actions: list, fps: float = 60.0, tail_ms: float = 50.0
    ) -> Dict[str, Any]:
        """Replay timestamped input across REAL frames (multi-frame interactions).

        Each action is ``{"t_ms": <relative ms>, "event": {<inject event dict>}}``.
        The editor walks a virtual clock in 1000/fps steps, fires each action when
        the clock reaches its ``t_ms``, and advances ONE real render frame per step
        (NewFrame/drawUi/Render) so ImGui integrates the elapsed time. This is what
        a single batched ``inject_input`` cannot do: a menu popup only appears the
        frame AFTER its header is clicked, and a DragFloat only accumulates motion
        across successive frame deltas. The call blocks until the whole sequence
        (plus ``tail_ms``) has played, so results can be queried immediately after.

        Returns ``{fired, frames, cursor, camera, eye}``.
        """
        return self._command("play_input", actions=actions, fps=fps, tail_ms=tail_ms)

    def click(
        self,
        x: float,
        y: float,
        hold_ms: float = 100.0,
        pre_move_ms: float = 16.0,
        button: int = 0,
    ) -> Dict[str, Any]:
        """A realistic multi-frame click: move to (x,y), small hold, down, hold, up.

        Unlike ``inject_click`` (one frame), this spreads press and release across
        frames so a widget that needs to see the button held (and the hover
        settle) registers the interaction like a human click. ``button`` is
        0=left (select / gizmo / UI), 1=right (orbit), 2=middle (pan).
        """
        t = 0.0
        actions = [{"t_ms": t, "event": {"type": "mouse_move", "x": x, "y": y}}]
        t += pre_move_ms
        actions.append(
            {"t_ms": t, "event": {"type": "mouse_button", "button": button, "down": True}}
        )
        t += hold_ms
        actions.append(
            {"t_ms": t, "event": {"type": "mouse_button", "button": button, "down": False}}
        )
        return self.play_input(actions, tail_ms=hold_ms)

    def click_drag(
        self,
        from_xy,
        to_xy,
        hold_ms: float = 100.0,
        drag_ms: float = 1000.0,
        release_hold_ms: float = 100.0,
        fps: float = 60.0,
        button: int = 0,
    ) -> Dict[str, Any]:
        """Move to ``from_xy``, press, hold, interpolate to ``to_xy`` over
        ``drag_ms``, hold, release — a true multi-frame drag.

        This is the exact gesture a DragFloat needs: the button is held while the
        cursor moves incrementally across many frames, so ImGui accumulates the
        per-frame deltas into the value change. ``button`` selects which mouse
        button is held: 0=left (gizmo handle drag), 1=right (orbit), 2=middle (pan).
        """
        x0, y0 = from_xy
        x1, y1 = to_xy
        t = 0.0
        actions = [{"t_ms": t, "event": {"type": "mouse_move", "x": x0, "y": y0}}]
        t += 16.0
        actions.append(
            {"t_ms": t, "event": {"type": "mouse_button", "button": button, "down": True}}
        )
        t += hold_ms
        # Interpolate the move over drag_ms, one sub-move per simulated frame.
        steps = max(1, int(round(drag_ms / (1000.0 / fps))))
        for i in range(1, steps + 1):
            f = i / steps
            actions.append(
                {
                    "t_ms": t + drag_ms * f,
                    "event": {
                        "type": "mouse_move",
                        "x": x0 + (x1 - x0) * f,
                        "y": y0 + (y1 - y0) * f,
                    },
                }
            )
        t += drag_ms
        t += release_hold_ms
        actions.append(
            {"t_ms": t, "event": {"type": "mouse_button", "button": button, "down": False}}
        )
        return self.play_input(actions, fps=fps, tail_ms=release_hold_ms)

    def menu_pick(self, menu_name: str, item_label: str) -> Dict[str, Any]:
        """Open a menu-bar menu and click one of its items via simulated input.

        Looks up the menu header rect (``menu_<menu_name>``), clicks it to open the
        popup, advances frames so the popup lays out + registers its item rects,
        re-queries the item rect (``menu_<menu_name>_<slug>`` or an explicit
        ``layout_name`` form), then clicks the item. Returns the play_input result
        of the item click.

        ``item_label`` may be the registered layout suffix (e.g. "sphere") or a
        human label ("Sphere"); it is lowercased + spaces→underscores to match the
        registered names (menu_insert_sphere, menu_insert_area_light, ...).
        """
        header = self.query_layout(f"menu_{menu_name}")["rect"]
        hx, hy = header["center"]
        # Click the header; popup opens on the following frame(s).
        self.click(hx, hy)
        # The item rects are recorded under menu_<menu_name_lower>_<slug>.
        slug = item_label.strip().lower().replace(" ", "_")
        item_key = f"menu_{menu_name.lower()}_{slug}"
        rect = self.query_layout(item_key)["rect"]
        ix, iy = rect["center"]
        return self.click(ix, iy)

    # ----- model mutation (insert / edit objects) --------------------------

    def new_scene(self) -> Dict[str, Any]:
        """File > New: reset to a pristine empty scene (the from-scratch start)."""
        return self._command("new_scene")

    def insert_object(self, kind: str) -> Dict[str, Any]:
        """Insert a new object into the scene model and select it.

        `kind` is one of "SphereVolume", "MeshVolume", "AreaLight", "OmniLight"
        (short forms "sphere"/"mesh"/"area_light"/"omni_light" also accepted).
        Goes through the same model-mutation path as the Insert menu, so the new
        object appears in the explorer + viewport immediately. Returns the new
        object's index, name, and full detail.
        """
        return self._command("insert_object", kind=kind)

    def set_property(
        self, field: str, value: Any, index: Optional[int] = None
    ) -> Dict[str, Any]:
        """Set a field on an object (the programmatic twin of the props panel).

        Targets the selected object unless `index` is given. Recognized fields:
          numeric: pos_x/y/z, rot_x/y/z, scale_x/y/z, radius,
                   light_width, light_height, light_radius,
                   mat_color_r/g/b, mat_ior
          string:  material_type (Lambertian|Mirror|Glass|Microfacet),
                   material_name (assign an existing material),
                   mesh_file (register an OBJ in $meshes; MeshVolume only),
                   mesh_shape (bind to an OBJ sub-shape; MeshVolume only)
        Returns the object's updated detail.
        """
        return self._command("set_property", field=field, value=value, index=index)

    # ----- materials (twin of the Material Manager GUI) --------------------

    def create_material(
        self,
        name: Optional[str] = None,
        type: Optional[str] = None,
        color: Optional[list] = None,
        ior: Optional[float] = None,
    ) -> Dict[str, Any]:
        """Create a material via the same path as the Material Manager's New
        button, optionally applying name/type/color/ior. Selects it as the active
        material context. Returns the new name + material detail."""
        kwargs: Dict[str, Any] = {}
        if name is not None:
            kwargs["name"] = name
        if type is not None:
            kwargs["type"] = type
        if color is not None:
            kwargs["color"] = color
        if ior is not None:
            kwargs["ior"] = ior
        return self._command("create_material", **kwargs)

    def set_material(self, name: str, field: str, value: Any) -> Dict[str, Any]:
        """Edit a material by name (twin of the material-edit form). `field` is
        `type` (string) or color_r/color_g/color_b/ior (numeric). Every object
        referencing the material updates."""
        return self._command("set_material", name=name, field=field, value=value)

    def assign_material(
        self, name: Optional[str] = None, index: Optional[int] = None
    ) -> Dict[str, Any]:
        """Assign a material (default the active context) to an object (default the
        selected object) — twin of the Material Manager's assign button."""
        return self._command("assign_material", name=name, index=index)

    def query_layout(self, name: Optional[str] = None) -> Dict[str, Any]:
        """Return the pixel rect(s) of named UI elements from the last frame.

        With `name`, returns {"rect": {x, y, width, height, center:[cx,cy]}}.
        Without, returns {"elements": {name: rect, ...}}.
        """
        return self._command("query_layout", name=name)

    def quit(self) -> Dict[str, Any]:
        return self._command("quit")


# ----- CLI -----------------------------------------------------------------


def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Drive the Ray Tracer Editor automation port.")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=8780)
    p.add_argument("--timeout", type=float, default=605.0)

    sub = p.add_subparsers(dest="command", required=True)

    sub.add_parser("ping")
    sub.add_parser("get-state")
    sub.add_parser("quit")

    lm = sub.add_parser("load-mesh")
    lm.add_argument("path")

    ls = sub.add_parser("load-scene")
    ls.add_argument("path")

    sv = sub.add_parser("save-scene")
    sv.add_argument("path")

    sc = sub.add_parser("set-camera")
    sc.add_argument("--eye", type=float, nargs=3)
    sc.add_argument("--target", type=float, nargs=3)
    sc.add_argument("--fov", type=float)
    sc.add_argument("--orbit-yaw", type=float)
    sc.add_argument("--orbit-pitch", type=float)
    sc.add_argument("--dolly", type=float)

    rs = sub.add_parser("set-render-settings")
    rs.add_argument("--resolution", type=int)
    rs.add_argument("--photons", type=int, help="photons in millions")
    rs.add_argument("--scene-path")

    rn = sub.add_parser("render")
    rn.add_argument("--wait", action="store_true")
    rn.add_argument("--timeout", type=float)

    ss = sub.add_parser("screenshot")
    ss.add_argument("path")
    ss.add_argument("--target", default="window", choices=["window", "viewport", "render"])

    ql = sub.add_parser("query-layout")
    ql.add_argument("name", nargs="?", help="element name; omit to list all")

    sub.add_parser("new-scene")

    io = sub.add_parser("insert-object")
    io.add_argument("kind", help="SphereVolume|MeshVolume|AreaLight|OmniLight")

    sp = sub.add_parser("set-property")
    sp.add_argument("field")
    sp.add_argument("value", help="numeric for transform/material fields; string for material_type/name")
    sp.add_argument("--index", type=int, help="target object index (default: selected)")

    cl = sub.add_parser("click")
    cl.add_argument("x", type=float)
    cl.add_argument("y", type=float)
    cl.add_argument("--button", type=int, default=0, choices=[0, 1, 2])

    dr = sub.add_parser("drag")
    dr.add_argument("x0", type=float)
    dr.add_argument("y0", type=float)
    dr.add_argument("x1", type=float)
    dr.add_argument("y1", type=float)
    dr.add_argument("--steps", type=int, default=8)
    dr.add_argument("--button", type=int, default=0, choices=[0, 1, 2])

    tc = sub.add_parser("timed-click", help="multi-frame click via play_input")
    tc.add_argument("x", type=float)
    tc.add_argument("y", type=float)
    tc.add_argument("--hold-ms", type=float, default=100.0)
    tc.add_argument("--button", type=int, default=0, choices=[0, 1, 2],
                    help="0=left (select/gizmo/UI), 1=right (orbit), 2=middle (pan)")

    cd = sub.add_parser("click-drag", help="multi-frame held drag via play_input")
    cd.add_argument("x0", type=float)
    cd.add_argument("y0", type=float)
    cd.add_argument("x1", type=float)
    cd.add_argument("y1", type=float)
    cd.add_argument("--hold-ms", type=float, default=100.0)
    cd.add_argument("--drag-ms", type=float, default=1000.0)
    cd.add_argument("--release-hold-ms", type=float, default=100.0)
    cd.add_argument("--button", type=int, default=0, choices=[0, 1, 2],
                    help="0=left (gizmo handle drag), 1=right (orbit), 2=middle (pan)")

    mp = sub.add_parser("menu-pick", help="open a menu and click an item via injection")
    mp.add_argument("menu", help="menu header name, e.g. Insert (matches menu_<name>)")
    mp.add_argument("item", help="item label/slug, e.g. Sphere or sphere")

    pi = sub.add_parser("play-input", help="replay a JSON actions array across frames")
    pi.add_argument("actions_json", help="JSON array of {t_ms, event} actions, or @file")
    pi.add_argument("--fps", type=float, default=60.0)
    pi.add_argument("--tail-ms", type=float, default=50.0)

    return p


def main(argv: Optional[list] = None) -> int:
    args = _build_parser().parse_args(argv)
    client = EditorClient(host=args.host, port=args.port, timeout=args.timeout)

    try:
        if args.command == "ping":
            result = client.ping()
        elif args.command == "get-state":
            result = client.get_state()
        elif args.command == "load-mesh":
            result = client.load_mesh(args.path)
        elif args.command == "load-scene":
            result = client.load_scene(args.path)
        elif args.command == "save-scene":
            result = client.save_scene(args.path)
        elif args.command == "set-camera":
            result = client.set_camera(
                eye=args.eye,
                target=args.target,
                fov=args.fov,
                orbit_yaw=args.orbit_yaw,
                orbit_pitch=args.orbit_pitch,
                dolly=args.dolly,
            )
        elif args.command == "set-render-settings":
            result = client.set_render_settings(
                resolution=args.resolution,
                photons=args.photons,
                scene_path=args.scene_path,
            )
        elif args.command == "render":
            result = client.render(wait=args.wait, timeout=args.timeout)
        elif args.command == "screenshot":
            result = client.screenshot(args.path, target=args.target)
        elif args.command == "query-layout":
            result = client.query_layout(name=args.name)
        elif args.command == "new-scene":
            result = client.new_scene()
        elif args.command == "insert-object":
            result = client.insert_object(args.kind)
        elif args.command == "set-property":
            # Numeric value when it parses as a float, else pass as a string
            # (material_type / material_name take strings).
            try:
                val: Any = float(args.value)
            except ValueError:
                val = args.value
            result = client.set_property(args.field, val, index=args.index)
        elif args.command == "click":
            result = client.inject_click(args.x, args.y, button=args.button)
        elif args.command == "drag":
            result = client.inject_drag(
                args.x0, args.y0, args.x1, args.y1, steps=args.steps, button=args.button
            )
        elif args.command == "timed-click":
            result = client.click(args.x, args.y, hold_ms=args.hold_ms, button=args.button)
        elif args.command == "click-drag":
            result = client.click_drag(
                (args.x0, args.y0),
                (args.x1, args.y1),
                hold_ms=args.hold_ms,
                drag_ms=args.drag_ms,
                release_hold_ms=args.release_hold_ms,
                button=args.button,
            )
        elif args.command == "menu-pick":
            result = client.menu_pick(args.menu, args.item)
        elif args.command == "play-input":
            spec = args.actions_json
            if spec.startswith("@"):
                with open(spec[1:], "r", encoding="utf-8") as fh:
                    actions = json.load(fh)
            else:
                actions = json.loads(spec)
            result = client.play_input(actions, fps=args.fps, tail_ms=args.tail_ms)
        elif args.command == "quit":
            result = client.quit()
        else:  # pragma: no cover - argparse enforces choices
            raise EditorClientError(f"unknown command {args.command}")
    except EditorClientError as e:
        print(json.dumps({"ok": False, "error": str(e)}), file=sys.stderr)
        return 1
    except OSError as e:
        print(json.dumps({"ok": False, "error": f"connection failed: {e}"}), file=sys.stderr)
        return 1

    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
