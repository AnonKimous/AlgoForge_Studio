from __future__ import annotations

import time
import tkinter as tk
from typing import Any

try:
    from .shared import BLUEPRINT_NODE_MIN_HEIGHT, BLUEPRINT_NODE_WIDTH, CANVAS_PADDING, COLORS
except ImportError:
    from shared import BLUEPRINT_NODE_MIN_HEIGHT, BLUEPRINT_NODE_WIDTH, CANVAS_PADDING, COLORS


class AlgorithmStudioCanvasInteractionMixin:
    def _canvas_local_pointer(self, event: tk.Event) -> tuple[float, float]:
        if not self.canvas:
            raise AssertionError("Canvas is not initialized.")
        return float(event.x_root - self.canvas.winfo_rootx()), float(event.y_root - self.canvas.winfo_rooty())

    def _fallback_node_hit_at_scene_point(self, scene_x: float, scene_y: float) -> tuple[str | None, str | None]:
        for container in reversed(self.project.containers):
            if not self._is_node_visible_in_current_view("container", container.name):
                continue
            raw_x = container.x or CANVAS_PADDING + 40
            raw_y = container.y or CANVAS_PADDING + 40
            x, y = self._container_display_origin("container", container.name, raw_x, raw_y)
            width, height = self._container_render_size(container)
            if x <= scene_x <= x + width and y <= scene_y <= y + height:
                return "container", container.name
        for group in reversed(self.project.container_groups):
            if not self._is_node_visible_in_current_view("containerelement", group.name):
                continue
            x = float(group.x or CANVAS_PADDING + 20)
            y = float(group.y or CANVAS_PADDING + 20)
            width, height = self._container_group_render_size(group)
            if x <= scene_x <= x + width and y <= scene_y <= y + height:
                return "containerelement", group.name
        return None, None

    def _restore_dragged_node_position(self, drag_state: dict[str, Any]) -> None:
        kind = str(drag_state.get("kind") or "")
        name = str(drag_state.get("name") or "")
        original_x = float(drag_state.get("original_x", 0.0))
        original_y = float(drag_state.get("original_y", 0.0))
        if kind == "containerelement":
            group = self._find_container_group(name)
            if not group:
                raise AssertionError(f"Missing containerElement {name}")
            self._move_container_group_and_members(group, original_x - float(group.x), original_y - float(group.y))
            return
        item = self._node_by_kind_name(kind, name)
        if item is None:
            raise AssertionError(f"Missing node for drag restore: {kind}:{name}")
        item.x = original_x
        item.y = original_y

    def _on_canvas_click(self, event: tk.Event) -> None:
        if time.monotonic() < self.canvas_double_click_suppress_until:
            return
        if not self.canvas:
            return
        local_x, local_y = self._canvas_local_pointer(event)
        canvas_x = self.canvas.canvasx(local_x)
        canvas_y = self.canvas.canvasy(local_y)
        scene_x, scene_y = self._scene_point(local_x, local_y)
        item_id = self._canvas_item_hit(local_x, local_y)
        tags: tuple[str, ...] = self.canvas.gettags(item_id) if item_id is not None else ()
        port_info = self._port_info_from_tags(tags)
        if port_info:
            self._start_connection_drag(port_info, event)
            return
        resize_info = self._resize_handle_info_from_tags(tags)
        if resize_info:
            resize_kind, resize_name = resize_info
            if resize_kind == "containerelement":
                group = self._find_container_group(resize_name)
                if not group:
                    raise AssertionError(f"Missing containerElement {resize_name}")
                self._select_item_on_canvas("containerelement", resize_name)
                self.container_group_resize_state = {
                    "name": resize_name,
                    "x_root": event.x_root,
                    "y_root": event.y_root,
                    "scene_x": self._scene_point(local_x, local_y)[0],
                    "scene_y": self._scene_point(local_x, local_y)[1],
                    "width": group.width,
                    "height": group.height,
                }
                return
            if resize_kind in {"decomposer", "reflector", "resnode", "function", "functiontext", "interventioner", "stage"}:
                node = self._node_by_kind_name(resize_kind, resize_name)
                if node is None:
                    raise AssertionError(f"Missing {resize_kind} node {resize_name}")
                self._select_item_on_canvas(resize_kind, resize_name)
                self.toolnode_resize_state = {
                    "kind": resize_kind,
                    "name": resize_name,
                    "x_root": event.x_root,
                    "y_root": event.y_root,
                    "width": float(getattr(node, "width", BLUEPRINT_NODE_WIDTH)),
                    "height": float(getattr(node, "height", BLUEPRINT_NODE_MIN_HEIGHT)),
                }
                return
        kind, node_name = self._node_info_from_tags(tags)
        if kind and node_name:
            self._select_item_on_canvas(kind, node_name)
            should_drag = False
            if kind == "container":
                should_drag = True
            elif kind == "containerelement":
                group = self._find_container_group(node_name)
                if not group:
                    raise AssertionError(f"Missing containerElement {node_name}")
                if not self._is_node_expanded(group):
                    should_drag = True
                elif "group_header" in tags:
                    should_drag = True
                elif "group_body" in tags:
                    if self.marquee_state is not None:
                        raise AssertionError("Marquee state should not already be active.")
                    self.marquee_state = {
                        "canvas_x0": canvas_x,
                        "canvas_y0": canvas_y,
                        "canvas_x1": canvas_x,
                        "canvas_y1": canvas_y,
                        "scene_x0": scene_x,
                        "scene_y0": scene_y,
                        "scene_x1": scene_x,
                        "scene_y1": scene_y,
                        "item_id": self.canvas.create_rectangle(
                            canvas_x,
                            canvas_y,
                            canvas_x,
                            canvas_y,
                            outline=COLORS["accent"],
                            dash=(3, 2),
                            tags=("marquee",),
                        ),
                        "scope_group": node_name,
                    }
                    self._refresh_inspector()
                    return
            elif kind in {"decomposer", "reflector", "resnode", "function", "functiontext", "interventioner", "stage"}:
                node = self._node_by_kind_name(kind, node_name)
                if node is None:
                    raise AssertionError(f"Missing {kind} node {node_name}")
                should_drag = (not self._is_node_expanded(node)) or ("node_header" in tags)
            if should_drag:
                original_x = 0.0
                original_y = 0.0
                if kind == "container":
                    container = self._find_container(node_name)
                    if container is None:
                        raise AssertionError(f"Missing container {node_name}")
                    original_x = float(container.x)
                    original_y = float(container.y)
                elif kind == "containerelement":
                    group = self._find_container_group(node_name)
                    if group is None:
                        raise AssertionError(f"Missing containerElement {node_name}")
                    original_x = float(group.x)
                    original_y = float(group.y)
                else:
                    node = self._node_by_kind_name(kind, node_name)
                    if node is None:
                        raise AssertionError(f"Missing {kind} node {node_name}")
                    original_x = float(getattr(node, "x", 0.0))
                    original_y = float(getattr(node, "y", 0.0))
                self.node_drag_state = {
                    "kind": kind,
                    "name": node_name,
                    "x_root": event.x_root,
                    "y_root": event.y_root,
                    "original_x": original_x,
                    "original_y": original_y,
                }
                if kind == "container":
                    self.node_drag_state["original_parent_group"] = self._container_parent_group_name(node_name)
                elif kind == "containerelement":
                    self.node_drag_state["original_parent_group"] = self._group_parent_group_name(node_name)
                if kind == "containerelement":
                    self.container_group_drag_state = {
                        "name": node_name,
                        "x_root": event.x_root,
                        "y_root": event.y_root,
                    }
            return
        if item_id is None:
            self.selection_state = None
            self._clear_highlighted_container_chain()
            self.selected_container_name = None
            self.selected_rule_name = None
            self.selected_reflector_name = None
            self.selected_res_node_name = None
            self.selected_function_name = None
            self.selected_stage_name = None
            self.selected_function_text_name = None
            self.selected_container_group_name = None
            self._sync_selected_nodes()
            self._redraw_canvas()
            self._refresh_selection_panel()
            self.marquee_state = {
                "canvas_x0": canvas_x,
                "canvas_y0": canvas_y,
                "canvas_x1": canvas_x,
                "canvas_y1": canvas_y,
                "scene_x0": scene_x,
                "scene_y0": scene_y,
                "scene_x1": scene_x,
                "scene_y1": scene_y,
                "item_id": self.canvas.create_rectangle(
                    canvas_x,
                    canvas_y,
                    canvas_x,
                    canvas_y,
                    outline=COLORS["accent"],
                    dash=(3, 2),
                    tags=("marquee",),
                ),
            }
            self._refresh_inspector()
            return
        self.node_drag_state = None

    def _on_canvas_drag(self, event: tk.Event) -> None:
        if not self.canvas:
            return
        local_x, local_y = self._canvas_local_pointer(event)
        if self.canvas_pan_state:
            self._drag_canvas_pan(local_x, local_y)
            return
        if self.connection_drag_state:
            self._update_connection_drag_preview(local_x, local_y)
            return
        if self.marquee_state:
            x0 = float(self.marquee_state["canvas_x0"])
            y0 = float(self.marquee_state["canvas_y0"])
            x1 = self.canvas.canvasx(local_x)
            y1 = self.canvas.canvasy(local_y)
            self.marquee_state["canvas_x1"] = x1
            self.marquee_state["canvas_y1"] = y1
            scene_x1, scene_y1 = self._scene_point(local_x, local_y)
            self.marquee_state["scene_x1"] = scene_x1
            self.marquee_state["scene_y1"] = scene_y1
            item_id = self.marquee_state.get("item_id")
            if item_id:
                self.canvas.coords(item_id, x0, y0, x1, y1)
            return
        if self.container_group_resize_state:
            name = str(self.container_group_resize_state["name"])
            group = self._find_container_group(name)
            if not group:
                raise AssertionError(f"Missing containerElement {name}")
            min_width, min_height = self._container_group_min_size(group)
            old_body_bounds = self._group_body_bounds(group)
            new_width = max(min_width, float(self.container_group_resize_state["width"]) + self._scene_delta(event.x_root - float(self.container_group_resize_state["x_root"])))
            new_height = max(min_height, float(self.container_group_resize_state["height"]) + self._scene_delta(event.y_root - float(self.container_group_resize_state["y_root"])))
            group.width = new_width
            group.height = new_height
            new_body_bounds = self._group_body_bounds(group)
            self._resize_group_contents_proportionally(group, old_body_bounds, new_body_bounds)
            self._refresh_canvas_drag_preview()
            return
        if self.toolnode_resize_state:
            self._drag_toolnode_resize(event.x_root, event.y_root)
            return
        if not self.node_drag_state:
            return
        dx = event.x_root - float(self.node_drag_state["x_root"])
        dy = event.y_root - float(self.node_drag_state["y_root"])
        kind = str(self.node_drag_state["kind"])
        name = str(self.node_drag_state["name"])
        self.node_drag_state["x_root"] = event.x_root
        self.node_drag_state["y_root"] = event.y_root
        dx = self._scene_delta(dx)
        dy = self._scene_delta(dy)
        if kind == "container":
            container = self._find_container(name)
            if container:
                container.x += dx
                container.y += dy
        elif kind == "containerelement":
            group = self._find_container_group(name)
            if group:
                self._update_container_group_geometry(group, dx, dy)
        elif kind == "decomposer":
            rule = self._find_rule(name)
            if rule:
                rule.x += dx
                rule.y += dy
        elif kind == "reflector":
            item = self._find_reflector(name)
            if item:
                item.x += dx
                item.y += dy
        elif kind == "resnode":
            res_node = self._find_res_node(name)
            if res_node:
                res_node.x += dx
                res_node.y += dy
        elif kind == "function":
            item = self._find_function_frame(name)
            if item:
                item.x += dx
                item.y += dy
        elif kind in {"interventioner", "stage"}:
            stage = self._find_stage(name)
            if stage:
                stage.x += dx
                stage.y += dy
        elif kind == "functiontext":
            item = self._find_function_text_item(name)
            if item:
                item.x += dx
                item.y += dy
        self.node_drag_state["waste_area"] = event.y_root < self._canvas_top_screen_y()
        self.node_drag_state["doc_area"] = self._document_panel_contains_screen_point(event.x_root, event.y_root)
        self._redraw_canvas()

    def _on_canvas_release(self, event: tk.Event) -> None:
        if self.canvas_pan_state:
            self._finish_canvas_pan()
            return
        if self.connection_drag_state:
            self._finish_connection_drag(event)
            return
        if self.marquee_state:
            scene_x0 = float(self.marquee_state["scene_x0"])
            scene_y0 = float(self.marquee_state["scene_y0"])
            scene_x1 = float(self.marquee_state["scene_x1"])
            scene_y1 = float(self.marquee_state["scene_y1"])
            if abs(scene_x1 - scene_x0) >= 8 and abs(scene_y1 - scene_y0) >= 8:
                rect = self._normalize_rect(scene_x0, scene_y0, scene_x1, scene_y1)
                scope_group_name = self.marquee_state.get("scope_group")
                if scope_group_name:
                    group = self._find_container_group(str(scope_group_name))
                    if not group:
                        raise AssertionError(f"Missing containerElement {scope_group_name}")
                    variables, arrays = self._members_inside_group_rect(group, rect)
                    groups = self._groups_inside_group_rect(group, rect)
                else:
                    variables, arrays = self._members_inside_rect(rect)
                    groups = self._groups_inside_rect(rect)
                resnodes = self._resnodes_inside_rect(rect)
                if not variables and not arrays and not groups and not resnodes:
                    self._log("Marquee selection did not include any selectable items.")
                    self.selection_state = None
                    self._refresh_canvas_interaction_state()
                else:
                    selection_payload = {
                        "groups": groups,
                        "variables": variables,
                        "arrays": arrays,
                        "resnodes": resnodes,
                        "scope_group": scope_group_name,
                    }
                    self.selection_state = {
                        "groups": groups,
                        "variables": variables,
                        "arrays": arrays,
                        "resnodes": resnodes,
                        "rect": rect,
                        "scope_group": scope_group_name,
                        "suggested_name": self._next_group_name_for_zone(self._selection_sync_zone(selection_payload)),
                    }
                    self.selected_container_name = None
                    self.selected_rule_name = None
                    self.selected_reflector_name = None
                    self.selected_res_node_name = None
                    self.selected_stage_name = None
                    self.selected_container_group_name = None
                    self._refresh_canvas_interaction_state()
            self._clear_marquee_state()
            self.container_group_drag_state = None
            self.container_group_resize_state = None
            self.node_drag_state = None
            self._refresh_canvas_interaction_state()
            return
        if self.container_group_resize_state:
            self.container_group_resize_state = None
            self.container_group_drag_state = None
            self.node_drag_state = None
            self._refresh_canvas_interaction_state()
            return
        if self.toolnode_resize_state:
            self.toolnode_resize_state = None
            self.node_drag_state = None
            self._refresh_canvas_interaction_state()
            return
        if self.node_drag_state:
            if bool(self.node_drag_state.get("doc_area")):
                kind = str(self.node_drag_state.get("kind") or "")
                name = str(self.node_drag_state.get("name") or "")
                self._restore_dragged_node_position(self.node_drag_state)
                self.node_drag_state = None
                self.container_group_drag_state = None
                self.container_group_resize_state = None
                self.toolnode_resize_state = None
                self._open_node_in_document_panel(kind, name)
                self._refresh_canvas_interaction_state(schedule_manifest_refresh=False)
                return
            if bool(self.node_drag_state.get("waste_area")):
                kind = str(self.node_drag_state.get("kind") or "")
                name = str(self.node_drag_state.get("name") or "")
                if kind == "container":
                    self.selected_container_name = name
                    self._delete_selected_container()
                elif kind == "containerelement" and name != "container":
                    self.selected_container_group_name = name
                    self._delete_selected_container_group()
                elif kind == "decomposer":
                    self.selected_rule_name = name
                    self._delete_selected_rule()
                elif kind == "reflector":
                    self.selected_reflector_name = name
                    self._delete_selected_reflector()
                elif kind == "resnode":
                    self.selected_res_node_name = name
                    self._delete_selected_res_node()
                elif kind == "function":
                    self.selected_function_name = name
                    self._delete_selected_function()
                elif kind == "functiontext":
                    self.selected_function_text_name = name
                    self._delete_selected_function_text()
                elif kind in {"interventioner", "stage"}:
                    self.selected_stage_name = name
                    self._delete_selected_stage()
                self.node_drag_state = None
                self.container_group_drag_state = None
                self.container_group_resize_state = None
                self.toolnode_resize_state = None
                self._refresh_canvas_interaction_state(schedule_manifest_refresh=False)
                return
            if self.canvas_view_mode != "container_overview":
                attached_to_new_parent = self._attach_dragged_node_to_group_if_inside(self.node_drag_state)
                if not attached_to_new_parent:
                    self._detach_dragged_node_if_outside_parent(self.node_drag_state)
                self._sync_all_container_groups()
            self.node_drag_state = None
            self.container_group_drag_state = None
            self.container_group_resize_state = None
            self.toolnode_resize_state = None
            self._refresh_canvas_interaction_state(sync_groups=self.canvas_view_mode != "container_overview")
            return
        self.node_drag_state = None

    def _on_canvas_double_click(self, event: tk.Event) -> str:
        if not self.canvas:
            return "break"
        self._reset_canvas_interaction_states()
        local_x, local_y = self._canvas_local_pointer(event)
        scene_x, scene_y = self._scene_point(local_x, local_y)
        item_id = self._canvas_item_hit(local_x, local_y)
        if item_id is None:
            item_id = self._canvas_item_hit_nearby(local_x, local_y, radius=14)
        tags: tuple[str, ...] = ()
        kind: str | None = None
        node_name: str | None = None
        if item_id is not None:
            tags = self.canvas.gettags(item_id)
            kind, node_name = self._node_info_from_tags(tags)
        if not kind or not node_name:
            kind, node_name = self._fallback_node_hit_at_scene_point(scene_x, scene_y)
            if not kind or not node_name:
                return "break"
        self._select_item_on_canvas(kind, node_name)
        if kind == "container":
            container = self._find_container(node_name)
            if container is None:
                raise AssertionError(f"Missing container {node_name}")
            field_index = self._container_layout_field_index_from_tags(tags)
            if field_index is not None:
                self._open_container_layout_field_editor(container, field_index)
                self.canvas_double_click_suppress_until = time.monotonic() + 0.25
                return "break"
            if not self._is_node_expanded(container, default=False):
                self._toggle_canvas_node_expand(kind, node_name)
                self.canvas_double_click_suppress_until = time.monotonic() + 0.25
                return "break"
            self._toggle_canvas_node_expand(kind, node_name)
            self.canvas_double_click_suppress_until = time.monotonic() + 0.25
            return "break"
        if "node_header" in tags or "group_header" in tags:
            self._toggle_canvas_node_expand(kind, node_name)
        else:
            self._handle_canvas_node_body_double_click(kind, node_name, tags)
        self.canvas_double_click_suppress_until = time.monotonic() + 0.25
        return "break"

    def _toggle_canvas_node_expand(self, kind: str, node_name: str) -> None:
        item: Any | None = None
        if kind == "container":
            item = self._find_container(node_name)
        elif kind == "containerelement":
            item = self._find_container_group(node_name)
        elif kind == "decomposer":
            item = self._find_rule(node_name)
        elif kind == "reflector":
            item = self._find_reflector(node_name)
        elif kind == "resnode":
            item = self._find_res_node(node_name)
        elif kind == "function":
            item = self._find_function_frame(node_name)
        elif kind == "functiontext":
            item = self._find_function_text_item(node_name)
        elif kind in {"interventioner", "stage"}:
            item = self._find_stage(node_name)
        if item is None:
            raise AssertionError(f"Missing node for expand toggle: {kind}:{node_name}")
        if not hasattr(item, "expand"):
            raise AssertionError(f"Node does not support expand: {kind}:{node_name}")
        item.expand = not bool(getattr(item, "expand"))
        self._refresh_all()
        state = "expanded" if item.expand else "collapsed"
        self._log(f"{kind}:{node_name} {state}.")

    def _on_canvas_right_press(self, event: tk.Event) -> None:
        if not self.canvas:
            return
        item_id = self._canvas_item_hit(event.x, event.y)
        if item_id is not None:
            tags = self.canvas.gettags(item_id)
            kind, node_name = self._node_info_from_tags(tags)
            if kind in {"container", "decomposer", "reflector", "resnode", "function", "functiontext", "interventioner", "stage"}:
                self._select_item_on_canvas(kind, node_name)
                self.container_copy_drag_state = {
                    "kind": self._normalize_selected_kind(kind),
                    "source_name": node_name,
                    "start_x": event.x,
                    "start_y": event.y,
                    "started": False,
                    "duplicate_name": "",
                }
            return
        self.canvas_pan_state = {
            "x": event.x,
            "y": event.y,
        }

    def _on_canvas_right_drag(self, event: tk.Event) -> None:
        if self.container_copy_drag_state:
            self._drag_container_copy(event)
            return
        if not self.canvas_pan_state:
            return
        self._drag_canvas_pan(event.x, event.y)

    def _on_canvas_right_release(self, event: tk.Event) -> None:
        if self.container_copy_drag_state:
            if self._finish_container_copy_drag(event):
                return
            self.container_copy_drag_state = None
            self._show_canvas_context_menu(event)
            return
        if self.canvas_pan_state:
            self._finish_canvas_pan()
            return
        self._show_canvas_context_menu(event)

    def _drag_container_copy(self, event: tk.Event) -> None:
        if not self.container_copy_drag_state or not self.canvas:
            return
        source_kind = str(self.container_copy_drag_state.get("kind") or "")
        source_name = str(self.container_copy_drag_state["source_name"])
        source = self._node_by_kind_name(source_kind, source_name)
        if source is None:
            raise AssertionError(f"Missing source node {source_kind}:{source_name}")
        if not hasattr(source, "x") or not hasattr(source, "y"):
            raise AssertionError(f"Node does not expose drag coordinates: {source_kind}:{source_name}")
        dx = event.x - int(self.container_copy_drag_state["start_x"])
        dy = event.y - int(self.container_copy_drag_state["start_y"])
        if not bool(self.container_copy_drag_state.get("started")):
            if abs(dx) + abs(dy) < 6:
                return
            drag_x = float(source.x) + self._scene_delta(dx)
            drag_y = float(source.y) + self._scene_delta(dy)
            reused_existing_node = False
            if source_kind == "container":
                duplicate_name, reused_existing_node = self._extract_or_duplicate_container_for_drag(source_name, x=drag_x, y=drag_y)
                duplicate_kind = "container"
            else:
                duplicate_kind, duplicate_name = self._duplicate_canvas_node(
                    source_kind,
                    source_name,
                    x=drag_x,
                    y=drag_y,
                )
            self.container_copy_drag_state["started"] = True
            self.container_copy_drag_state["duplicate_kind"] = duplicate_kind
            self.container_copy_drag_state["duplicate_name"] = duplicate_name
            self.container_copy_drag_state["reused_existing_node"] = reused_existing_node
            self._set_selected_canvas_item_state(duplicate_kind, duplicate_name)
            self._refresh_canvas_drag_preview()
            return
        duplicate_kind = str(self.container_copy_drag_state.get("duplicate_kind") or source_kind)
        duplicate_name = str(self.container_copy_drag_state["duplicate_name"])
        self._set_canvas_node_position(
            duplicate_kind,
            duplicate_name,
            float(source.x) + self._scene_delta(dx),
            float(source.y) + self._scene_delta(dy),
        )
        self._set_selected_canvas_item_state(duplicate_kind, duplicate_name)
        self._refresh_canvas_drag_preview()

    def _finish_container_copy_drag(self, event: tk.Event) -> bool:
        if not self.container_copy_drag_state:
            return False
        if not bool(self.container_copy_drag_state.get("started")):
            return False
        duplicate_name = str(self.container_copy_drag_state["duplicate_name"])
        if not duplicate_name:
            return False
        duplicate_kind = str(self.container_copy_drag_state.get("duplicate_kind") or self.container_copy_drag_state.get("kind") or "")
        duplicate = self._node_by_kind_name(duplicate_kind, duplicate_name)
        if duplicate is None:
            raise AssertionError(f"Missing duplicated node {duplicate_kind}:{duplicate_name}")
        self._select_item_on_canvas(duplicate_kind, duplicate_name)
        if duplicate_kind == "container" and self.canvas_view_mode != "container_overview":
            self._attach_dragged_node_to_group_if_inside({"kind": "container", "name": duplicate_name})
            self._sync_all_container_groups()
        self.container_copy_drag_state = None
        self._refresh_canvas_interaction_state(sync_groups=self.canvas_view_mode != "container_overview")
        return True

    def _show_canvas_context_menu(self, event: tk.Event) -> None:
        if not self.canvas:
            return
        item_id = self._canvas_item_hit(event.x, event.y)
        if item_id is None:
            if self.selection_clipboard:
                menu = tk.Menu(self.root, tearoff=0)
                scene_x, scene_y = self._scene_point(event.x, event.y)
                menu.add_command(label="Paste selection", command=lambda: self._paste_selection_from_clipboard(scene_x, scene_y))
                menu.tk_popup(event.x_root, event.y_root)
            return
        tags = self.canvas.gettags(item_id)
        if "connection" in tags:
            connection = self._connection_from_canvas_item(item_id)
            if not connection:
                raise AssertionError("Connection canvas item is missing its model entry.")
            menu = tk.Menu(self.root, tearoff=0)
            menu.add_command(label="Delete connection", command=lambda: self._delete_connection(connection))
            menu.tk_popup(event.x_root, event.y_root)
            return
        kind, node_name = self._node_info_from_tags(tags)
        if not kind or not node_name:
            return
        self._select_item_on_canvas(kind, node_name)
        menu = tk.Menu(self.root, tearoff=0)
        if kind == "container":
            menu.add_command(label="Show details", command=lambda: self._open_canvas_detail_panel(kind, node_name))
            menu.add_command(label="Highlight first->last path", command=lambda: self._highlight_container_chain_path(node_name))
            menu.add_command(label="Duplicate", command=self._duplicate_selected_container)
            if self._parse_standard_container_name(node_name) is not None:
                menu.add_command(label="Insert existing standard node before...", command=lambda: self._prompt_insert_container_before(node_name))
            menu.add_separator()
            menu.add_command(label="Delete", command=self._delete_selected_container)
        elif kind == "containerelement":
            menu.add_command(label="Show details", command=lambda: self._show_container_group_details(node_name))
            if node_name != "container":
                menu.add_separator()
                menu.add_command(label="Delete", command=self._delete_selected_container_group)
        elif kind == "decomposer":
            menu.add_command(label="Duplicate", command=lambda: self._duplicate_canvas_node_and_refresh("decomposer", node_name))
            menu.add_separator()
            menu.add_command(label="Delete", command=self._delete_selected_rule)
        elif kind == "reflector":
            menu.add_command(label="Duplicate", command=lambda: self._duplicate_canvas_node_and_refresh("reflector", node_name))
            menu.add_separator()
            menu.add_command(label="Delete", command=self._delete_selected_reflector)
        elif kind == "resnode":
            menu.add_command(label="Duplicate", command=lambda: self._duplicate_canvas_node_and_refresh("resnode", node_name))
            menu.add_separator()
            menu.add_command(label="Delete", command=self._delete_selected_res_node)
        elif kind == "function":
            menu.add_command(label="Duplicate", command=lambda: self._duplicate_canvas_node_and_refresh("function", node_name))
            menu.add_separator()
            menu.add_command(label="Delete", command=self._delete_selected_function)
        elif kind == "functiontext":
            menu.add_command(label="Duplicate", command=lambda: self._duplicate_canvas_node_and_refresh("functiontext", node_name))
            menu.add_separator()
            menu.add_command(label="Delete", command=self._delete_selected_function_text)
        elif kind in {"interventioner", "stage"}:
            menu.add_command(label="Duplicate", command=lambda: self._duplicate_canvas_node_and_refresh("interventioner", node_name))
            menu.add_separator()
            menu.add_command(label="Delete", command=self._delete_selected_stage)
        menu.tk_popup(event.x_root, event.y_root)

    def _show_container_group_details(self, name: str) -> None:
        group = self._find_container_group(name)
        if not group:
            raise AssertionError(f"Missing containerElement {name}")
        self.selected_container_group_name = group.name
        self.selected_container_name = None
        self.selected_rule_name = None
        self.selected_reflector_name = None
        self.selected_res_node_name = None
        self.selected_stage_name = None
        self._sync_selected_nodes()
        self._refresh_all()
