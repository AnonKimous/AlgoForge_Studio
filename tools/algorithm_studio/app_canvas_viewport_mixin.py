from __future__ import annotations

import tkinter as tk
from tkinter import font as tkfont


class AlgorithmStudioCanvasViewportMixin:
    def _canvas_zoom_factor(self) -> float:
        zoom = float(self.canvas_zoom)
        if zoom <= 0.0:
            raise AssertionError(f"Canvas zoom must stay positive, got {zoom}.")
        return zoom

    def _normalize_canvas_view_mode_name(self, view_mode: str | None) -> str:
        normalized = str(view_mode or self.canvas_view_mode).strip().lower()
        if normalized not in {
            "graph",
            "container_overview",
            "decomposer_overview",
            "reflector_overview",
            "interventioner_overview",
            "interventioner_pretick",
            "interventioner_aftertick",
            "interventioner_render",
            "decomposer2container_overview",
            "all_in_one",
        }:
            raise AssertionError(f"Unsupported canvas view mode: {view_mode}")
        return normalized

    def _store_canvas_viewport_state(self, view_mode: str | None = None) -> None:
        normalized = self._normalize_canvas_view_mode_name(view_mode)
        self.canvas_viewport_states[normalized] = (
            self._canvas_zoom_factor(),
            float(self.canvas_camera_x),
            float(self.canvas_camera_y),
        )

    def _restore_canvas_viewport_state(self, view_mode: str | None = None) -> None:
        normalized = self._normalize_canvas_view_mode_name(view_mode)
        state = self.canvas_viewport_states.get(normalized)
        if state is None:
            fallback_keys: list[str] = []
            if normalized.startswith("interventioner_"):
                fallback_keys.append("interventioner_overview")
            if normalized in {"interventioner_overview", "interventioner_pretick", "interventioner_aftertick", "interventioner_render"}:
                fallback_keys.append("graph")
            fallback_keys.append("graph")
            for fallback_key in fallback_keys:
                state = self.canvas_viewport_states.get(fallback_key)
                if state is not None:
                    break
        if state is None:
            state = (1.0, 0.0, 0.0)
        zoom, camera_x, camera_y = state
        self.canvas_zoom = float(zoom)
        self.canvas_camera_x = float(camera_x)
        self.canvas_camera_y = float(camera_y)
        self.canvas_viewport_states[normalized] = (
            self.canvas_zoom,
            self.canvas_camera_x,
            self.canvas_camera_y,
        )

    def _scene_delta(self, value: float) -> float:
        return value / self._canvas_zoom_factor()

    def _scene_point(self, x: float, y: float) -> tuple[float, float]:
        zoom = self._canvas_zoom_factor()
        return (float(x) - self.canvas_camera_x) / zoom, (float(y) - self.canvas_camera_y) / zoom

    def _port_canvas_position_screen(self, kind: str, name: str, direction: str, port: str) -> tuple[float, float]:
        x, y = self._port_canvas_position(kind, name, direction, port)
        zoom = self._canvas_zoom_factor()
        return x * zoom + self.canvas_camera_x, y * zoom + self.canvas_camera_y

    def _scale_canvas_rendering(self, canvas: tk.Canvas, zoom: float) -> None:
        if zoom == 1.0:
            return
        for item_id in canvas.find_all():
            item_type = canvas.type(item_id)
            if item_type == "text":
                font_spec = canvas.itemcget(item_id, "font")
                if font_spec:
                    font_cache_key = (str(font_spec), round(float(zoom), 4))
                    font = self.canvas_scaled_font_cache.get(font_cache_key)
                    if font is None:
                        font = tkfont.Font(font=font_spec)
                        size = int(font.cget("size"))
                        if size < 0:
                            size = -size
                        font.configure(size=max(1, int(round(size * zoom))))
                        self.canvas_scaled_font_cache[font_cache_key] = font
                    canvas.itemconfigure(item_id, font=font)
                raw_wrap_width = str(canvas.itemcget(item_id, "width")).strip()
                if raw_wrap_width:
                    wrap_width = float(raw_wrap_width)
                    if wrap_width > 0.0:
                        canvas.itemconfigure(item_id, width=max(1, int(round(wrap_width * zoom))))
                continue
            raw_width_value = str(canvas.itemcget(item_id, "width")).strip()
            if raw_width_value:
                width_value = float(raw_width_value)
                if width_value > 0.0:
                    canvas.itemconfigure(item_id, width=max(1, int(round(width_value * zoom))))

    def _mouse_wheel_steps(self, event: tk.Event) -> int:
        if getattr(event, "num", None) == 4:
            return 1
        if getattr(event, "num", None) == 5:
            return -1
        if getattr(event, "delta", 0):
            return 1 if event.delta > 0 else -1
        return 0

    def _on_scene_tabs_mouse_wheel(self, event: tk.Event) -> str | None:
        if not self.scene_tabs_canvas:
            return None
        steps = self._mouse_wheel_steps(event)
        if steps == 0:
            return "break"
        self.scene_tabs_canvas.xview_scroll(-steps * 3, "units")
        return "break"

    def _on_canvas_mouse_wheel(self, event: tk.Event) -> str | None:
        if not self.canvas:
            return None
        steps = self._mouse_wheel_steps(event)
        if steps == 0:
            return "break"
        self._zoom_canvas_at(float(event.x), float(event.y), steps)
        return "break"

    def _on_palette_mouse_wheel(self, event: tk.Event) -> str | None:
        if not self.palette_canvas:
            return None
        steps = self._mouse_wheel_steps(event)
        if steps == 0:
            return "break"
        self.palette_canvas.yview_scroll(-steps * 3, "units")
        return "break"

    def _drag_canvas_pan(self, x: int, y: int) -> None:
        if not self.canvas_pan_state or not self.canvas:
            raise AssertionError("Canvas pan drag requested without active state.")
        previous_x = float(self.canvas_pan_state["x"])
        previous_y = float(self.canvas_pan_state["y"])
        self.canvas_camera_x += float(x) - previous_x
        self.canvas_camera_y += float(y) - previous_y
        self.canvas_pan_state["x"] = x
        self.canvas_pan_state["y"] = y
        self._store_canvas_viewport_state()
        self._redraw_canvas()

    def _finish_canvas_pan(self) -> None:
        if not self.canvas_pan_state:
            return
        self.canvas_pan_state = None
        self._redraw_canvas()

    def _restore_canvas_anchor(self, scene_x: float, scene_y: float, screen_x: float, screen_y: float) -> None:
        zoom = self._canvas_zoom_factor()
        self.canvas_camera_x = float(screen_x) - float(scene_x) * zoom
        self.canvas_camera_y = float(screen_y) - float(scene_y) * zoom
        self._store_canvas_viewport_state()

    def _zoom_canvas_at(self, screen_x: float, screen_y: float, steps: int) -> None:
        if not self.canvas:
            raise AssertionError("Canvas zoom requested before canvas initialization.")
        if steps == 0:
            return
        scene_x, scene_y = self._scene_point(screen_x, screen_y)
        old_zoom = self._canvas_zoom_factor()
        new_zoom = max(0.4, min(2.5, old_zoom * (1.1 ** steps)))
        if abs(new_zoom - old_zoom) < 1e-6:
            return
        self.canvas_zoom = new_zoom
        self._restore_canvas_anchor(scene_x, scene_y, screen_x, screen_y)
        self._redraw_canvas()
