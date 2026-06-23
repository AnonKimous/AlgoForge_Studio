from __future__ import annotations

import tkinter as tk
from tkinter import simpledialog

try:
    from .shared import COLORS
except ImportError:
    from shared import COLORS


RESOURCE_ROOT_GROUP_NAME = "resourceRoot"


class AlgorithmStudioCanvasOverlayMixin:
    def _interface4agents_demo_palette_drag(self, kind: str) -> str:
        if not self.canvas:
            raise AssertionError("Canvas is not ready.")
        normalized = str(kind).strip().lower()
        kind_aliases = {
            "v": "variable",
            "vnode": "variable",
            "variable": "variable",
            "a": "array",
            "anode": "array",
            "array": "array",
            "r": "reflector",
            "reflector": "reflector",
            "fun": "function",
            "function": "function",
            "i": "interventioner",
            "interventioner": "interventioner",
            "stage": "interventioner",
            "meshnode": "resnode",
            "resnode": "resnode",
        }
        palette_key = kind_aliases.get(normalized)
        if not palette_key:
            raise RuntimeError(f"Unsupported createNode demo kind: {kind}")
        self._interface4agents_highlight_target(palette_key)
        self.canvas.delete("highlight_demo")
        label_map = {
            "variable": "v",
            "array": "a",
            "reflector": "R",
            "function": "fun",
            "interventioner": "I",
            "resnode": "M",
        }
        zoom = self._canvas_zoom_factor()
        start_x = self._scene_point(24.0, 96.0)[0]
        start_y = self._scene_point(24.0, 96.0)[1]
        end_x = self._scene_point(max(240.0, float(self.canvas.winfo_width()) * 0.45), max(160.0, float(self.canvas.winfo_height()) * 0.3))[0]
        end_y = self._scene_point(max(180.0, float(self.canvas.winfo_height()) * 0.3), max(180.0, float(self.canvas.winfo_height()) * 0.3))[1]
        width = 132.0 / zoom
        height = 68.0 / zoom
        rect_id = self.canvas.create_rectangle(
            start_x,
            start_y,
            start_x + width,
            start_y + height,
            fill=COLORS["panel_alt"],
            outline=COLORS["accent"],
            width=2,
            dash=(4, 2),
            tags=("highlight_demo",),
        )
        text_id = self.canvas.create_text(
            start_x + width / 2,
            start_y + height / 2,
            text=label_map[palette_key],
            fill=COLORS["accent"],
            font=("Segoe UI", 12, "bold"),
            tags=("highlight_demo",),
        )
        steps = 12

        def animate(step: int = 0) -> None:
            if not self.canvas:
                return
            t = step / steps
            eased = 1.0 - (1.0 - t) * (1.0 - t)
            x = start_x + (end_x - start_x) * eased
            y = start_y + (end_y - start_y) * eased
            self.canvas.coords(rect_id, x, y, x + width, y + height)
            self.canvas.coords(text_id, x + width / 2, y + height / 2)
            if step >= steps:
                self.canvas.after(450, lambda: self.canvas.delete("highlight_demo"))
                return
            self.canvas.after(28, lambda next_step=step + 1: animate(next_step))

        animate()
        return f"Highlight demo: createNode {kind}"

    def _close_canvas_detail_panel(self) -> None:
        if not self.detail_panel_state:
            return
        frame = self.detail_panel_state.get("frame")
        if isinstance(frame, tk.Widget):
            try:
                frame.destroy()
            except tk.TclError:
                pass
        self.detail_panel_state = None

    def _refresh_canvas_detail_panel(self) -> None:
        if not self.detail_panel_state:
            return
        kind = str(self.detail_panel_state.get("kind") or "").strip()
        name = str(self.detail_panel_state.get("name") or "").strip()
        if not kind or not name:
            self._close_canvas_detail_panel()
            return
        node = self._node_by_kind_name(kind, name) if kind not in {"container", "containerelement"} else True
        if kind == "container" and self._find_container(name) is None:
            self._close_canvas_detail_panel()
            return
        if kind == "containerelement" and self._find_container_group(name) is None:
            self._close_canvas_detail_panel()
            return
        if kind not in {"container", "containerelement"} and node is None:
            self._close_canvas_detail_panel()
            return
        if not self._is_node_visible_in_current_view(kind, name):
            self._close_canvas_detail_panel()
            return
        preserved_x = float(self.detail_panel_state.get("x", 0.0))
        preserved_y = float(self.detail_panel_state.get("y", 0.0))
        self._open_canvas_detail_panel(kind, name, panel_x=preserved_x, panel_y=preserved_y)

    def _start_canvas_detail_panel_drag(self, event: tk.Event) -> str:
        if not self.detail_panel_state:
            raise AssertionError("Detail panel drag started without a panel.")
        self.detail_panel_state["drag_root_x"] = event.x_root
        self.detail_panel_state["drag_root_y"] = event.y_root
        return "break"

    def _drag_canvas_detail_panel(self, event: tk.Event) -> str:
        if not self.canvas or not self.detail_panel_state:
            return "break"
        window_id = self.detail_panel_state.get("window_id")
        if not window_id:
            raise AssertionError("Detail panel is missing its canvas window id.")
        x = float(self.detail_panel_state.get("x", 0.0))
        y = float(self.detail_panel_state.get("y", 0.0))
        dx = self._scene_delta(event.x_root - float(self.detail_panel_state.get("drag_root_x", event.x_root)))
        dy = self._scene_delta(event.y_root - float(self.detail_panel_state.get("drag_root_y", event.y_root)))
        x += dx
        y += dy
        self.detail_panel_state["x"] = x
        self.detail_panel_state["y"] = y
        self.detail_panel_state["drag_root_x"] = event.x_root
        self.detail_panel_state["drag_root_y"] = event.y_root
        self.canvas.coords(window_id, x, y)
        return "break"

    def _finish_canvas_detail_panel_drag(self, _event: tk.Event | None = None) -> str:
        if not self.canvas or not self.detail_panel_state:
            return "break"
        visible_top = self._scene_point(0.0, 0.0)[1]
        if float(self.detail_panel_state.get("y", 0.0)) < visible_top - 24.0:
            self._close_canvas_detail_panel()
            self._log("Closed the node detail panel.")
        return "break"

    def _open_canvas_detail_panel(
        self,
        kind: str,
        name: str,
        *,
        panel_x: float | None = None,
        panel_y: float | None = None,
    ) -> None:
        if not self.canvas:
            raise AssertionError("Canvas is not initialized.")
        self._close_canvas_detail_panel()
        title = f"{kind}:{name}"
        detail_text = self._selected_item_summary()
        _left, top, right, _bottom = self._node_bounds(kind, name)
        resolved_panel_x = right + 20.0 if panel_x is None else panel_x
        resolved_panel_y = top if panel_y is None else panel_y

        frame = tk.Frame(
            self.canvas,
            bg=COLORS["panel_alt"],
            highlightbackground=COLORS["accent"],
            highlightcolor=COLORS["accent"],
            highlightthickness=1,
            bd=0,
        )
        header = tk.Frame(frame, bg=COLORS["accent"], cursor="fleur")
        header.pack(fill="x")
        title_label = tk.Label(
            header,
            text=title,
            bg=COLORS["accent"],
            fg=COLORS["window"],
            anchor="w",
            padx=8,
            pady=4,
        )
        title_label.pack(side="left", fill="x", expand=True)
        close_button = tk.Button(
            header,
            text="x",
            command=self._close_canvas_detail_panel,
            bg=COLORS["accent"],
            fg=COLORS["window"],
            activebackground=COLORS["accent"],
            activeforeground=COLORS["window"],
            relief="flat",
            bd=0,
            padx=6,
            pady=2,
        )
        close_button.pack(side="right")
        body = tk.Text(
            frame,
            wrap="word",
            width=44,
            height=14,
            bg=COLORS["canvas"],
            fg=COLORS["text"],
            insertbackground=COLORS["text"],
            relief="flat",
            borderwidth=0,
            highlightthickness=0,
        )
        body.pack(fill="both", expand=True, padx=8, pady=(8, 4))
        body.insert("1.0", detail_text)
        body.configure(state="disabled")
        footer = tk.Label(
            frame,
            text="Drag the blue bar above the canvas top to dismiss",
            bg=COLORS["panel_alt"],
            fg=COLORS["muted"],
            anchor="w",
            padx=8,
            pady=4,
        )
        footer.pack(fill="x")
        window_id = self.canvas.create_window(resolved_panel_x, resolved_panel_y, anchor="nw", window=frame, tags=("detail_panel",))
        self.detail_panel_state = {
            "kind": kind,
            "name": name,
            "frame": frame,
            "window_id": window_id,
            "x": resolved_panel_x,
            "y": resolved_panel_y,
        }
        for widget in (header, title_label):
            widget.bind("<ButtonPress-1>", self._start_canvas_detail_panel_drag)
            widget.bind("<B1-Motion>", self._drag_canvas_detail_panel)
            widget.bind("<ButtonRelease-1>", self._finish_canvas_detail_panel_drag)
        self.canvas.tag_raise("detail_panel")

    def _handle_canvas_node_body_double_click(self, kind: str, node_name: str) -> None:
        if kind == "function":
            item = self._find_function_frame(node_name)
            if item is None:
                raise AssertionError(f"Missing function {node_name}")
            self._open_function_editor(item)
            return
        if kind == "functiontext":
            item = self._find_function_text_item(node_name)
            if item is None:
                raise AssertionError(f"Missing function text {node_name}")
            self._open_function_text_editor(item)
            return
        self._open_canvas_detail_panel(kind, node_name)

    def _prompt_insert_container_before(self, target_name: str) -> None:
        source_name = simpledialog.askstring("Insert container", f"Insert which standard container before {target_name}?")
        if source_name is None:
            return
        normalized_name = source_name.strip()
        if not normalized_name:
            raise AssertionError("Insert source container name cannot be empty.")
        self._insert_standard_container_before(normalized_name, target_name)

    def _node_shared_name_set(self, kind: str, name: str) -> set[str]:
        if kind == "container":
            return {self._canonical_shared_name(name)}
        if kind != "containerelement":
            return set()
        group = self._find_container_group(name)
        if not group:
            raise AssertionError(f"Missing containerElement {name}")
        shared_names: set[str] = set()
        for variable_name in group.variables:
            shared_names.add(self._canonical_shared_name(variable_name))
        for array_name in group.arrays:
            shared_names.add(self._canonical_shared_name(array_name))
        for child_name in group.groups:
            shared_names.update(self._node_shared_name_set("containerelement", child_name))
        return shared_names

    def _draw_partial_shared_links(self, canvas: tk.Canvas) -> None:
        visible_groups = [
            group
            for group in self.project.container_groups
            if group.name not in {"container", RESOURCE_ROOT_GROUP_NAME}
            and self._is_node_visible_in_current_view("containerelement", group.name)
        ]
        visible_targets: list[tuple[str, str]] = []
        for container in self.project.containers:
            if self._is_node_visible_in_current_view("container", container.name):
                visible_targets.append(("container", container.name))
        for group in visible_groups:
            visible_targets.append(("containerelement", group.name))
        drawn_pairs: set[tuple[str, str]] = set()
        for group in visible_groups:
            source_set = self._node_shared_name_set("containerelement", group.name)
            if not source_set:
                continue
            related_groups = {group.name} | self._group_descendant_names(group.name) | self._group_ancestor_names(group.name)
            related_containers = self._container_names_in_group_tree(group.name)
            for target_kind, target_name in visible_targets:
                if target_kind == "containerelement" and target_name in related_groups:
                    continue
                if target_kind == "container" and target_name in related_containers:
                    continue
                pair_key = tuple(sorted((f"containerelement:{group.name}", f"{target_kind}:{target_name}")))
                if pair_key in drawn_pairs:
                    continue
                target_set = self._node_shared_name_set(target_kind, target_name)
                shared_names = source_set & target_set
                if not shared_names:
                    continue
                identical = source_set == target_set
                sx, sy = self._node_center("containerelement", group.name)
                tx, ty = self._node_center(target_kind, target_name)
                canvas.create_line(
                    sx,
                    sy,
                    tx,
                    ty,
                    fill=COLORS["edge"],
                    width=2 if identical else 1,
                    dash=(8, 5) if identical else (2, 12),
                    tags=("shared_connection",),
                )
                drawn_pairs.add(pair_key)
