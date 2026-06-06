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
        elif args.command == "click":
            result = client.inject_click(args.x, args.y, button=args.button)
        elif args.command == "drag":
            result = client.inject_drag(
                args.x0, args.y0, args.x1, args.y1, steps=args.steps, button=args.button
            )
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
