from __future__ import annotations

from typing import Any

try:
    from .backend import ContainerGroupItem, ContainerItem
except ImportError:
    from backend import ContainerGroupItem, ContainerItem


class AlgorithmStudioContainerGroupMixin:
    def _container_group_render_size(self, group: ContainerGroupItem) -> tuple[float, float]:
        if not self._is_node_expanded(group):
            return self._collapsed_port_node_size(
                [
                    f"v: {len(group.variables)}",
                    f"a: {len(group.arrays)}",
                    f"g: {len(group.groups)}",
                ],
                ["in"],
                ["out"],
                group.width,
            )
        min_width, min_height = self._container_group_min_size(group)
        return max(group.width, min_width), max(group.height, min_height)

    def _container_center(self, container: ContainerItem) -> tuple[float, float]:
        left, top, right, bottom = self._container_rect(container)
        return (left + right) / 2, (top + bottom) / 2

    def _container_domain_display_offset(self, kind: str, name: str) -> tuple[float, float]:
        if self.canvas_view_mode != "decomposer2container_overview":
            return 0.0, 0.0
        if self._sync_zone_for_node(kind, name) == "resource":
            return 0.0, 0.0
        scene_width = 1440.0
        if self.canvas:
            scene_width = max(float(self.canvas.winfo_width()) / self._canvas_zoom_factor(), scene_width)
        return max(scene_width * 0.48, 620.0), 0.0

    def _container_display_origin(self, kind: str, name: str, x: float, y: float) -> tuple[float, float]:
        dx, dy = self._container_domain_display_offset(kind, name)
        return x + dx, y + dy

    def _group_bounds(self, group: ContainerGroupItem) -> tuple[float, float, float, float]:
        width, height = self._container_group_render_size(group)
        x, y = self._container_display_origin("containerelement", group.name, group.x, group.y)
        return x, y, x + width, y + height

    def _container_rect(self, container: ContainerItem) -> tuple[float, float, float, float]:
        width, height = self._container_render_size(container)
        x, y = self._container_display_origin("container", container.name, container.x, container.y)
        return x, y, x + width, y + height

    def _rects_intersect(
        self,
        left_a: float,
        top_a: float,
        right_a: float,
        bottom_a: float,
        left_b: float,
        top_b: float,
        right_b: float,
        bottom_b: float,
    ) -> bool:
        return not (right_a < left_b or right_b < left_a or bottom_a < top_b or bottom_b < top_a)

    def _container_group_members(self, group: ContainerGroupItem) -> tuple[list[str], list[str]]:
        variables: list[str] = []
        arrays: list[str] = []
        for name in group.variables:
            container = self._find_container(name)
            if not container or container.kind != "variable":
                raise AssertionError(f"Missing variable member {name} in {group.name}.")
            variables.append(container.name)
        for name in group.arrays:
            container = self._find_container(name)
            if not container or container.kind != "array":
                raise AssertionError(f"Missing array member {name} in {group.name}.")
            arrays.append(container.name)
        return variables, arrays

    def _group_child_groups(self, group: ContainerGroupItem) -> list[str]:
        child_groups: list[str] = []
        for child_name in group.groups:
            child_group = self._find_container_group(child_name)
            if not child_group:
                raise AssertionError(f"Missing child containerElement {child_name} in {group.name}.")
            child_groups.append(child_group.name)
        return child_groups

    def _container_parent_group_name(self, name: str) -> str | None:
        for group in self.project.container_groups:
            if name in group.variables or name in group.arrays:
                return group.name
        return None

    def _group_parent_group_name(self, name: str) -> str | None:
        for group in self.project.container_groups:
            if name in group.groups:
                return group.name
        return None

    def _remove_container_from_parent_group(self, container_name: str) -> None:
        parent_name = self._container_parent_group_name(container_name)
        if not parent_name:
            return
        parent_group = self._find_container_group(parent_name)
        if not parent_group:
            raise AssertionError(f"Missing parent containerElement {parent_name}")
        parent_group.variables = [name for name in parent_group.variables if name != container_name]
        parent_group.arrays = [name for name in parent_group.arrays if name != container_name]

    def _add_container_to_group(self, container_name: str, group_name: str) -> None:
        container = self._find_container(container_name)
        if not container:
            raise AssertionError(f"Missing container {container_name}")
        group = self._find_container_group(group_name)
        if not group:
            raise AssertionError(f"Missing containerElement {group_name}")
        self._remove_container_from_parent_group(container_name)
        if container.kind == "variable":
            if container_name not in group.variables:
                group.variables.append(container_name)
        elif container.kind == "array":
            if container_name not in group.arrays:
                group.arrays.append(container_name)
        else:
            raise AssertionError(f"Unsupported container kind: {container.kind}")

    def _remove_group_from_parent_group(self, group_name: str) -> None:
        parent_name = self._group_parent_group_name(group_name)
        if not parent_name:
            return
        parent_group = self._find_container_group(parent_name)
        if not parent_group:
            raise AssertionError(f"Missing parent containerElement {parent_name}")
        parent_group.groups = [name for name in parent_group.groups if name != group_name]

    def _add_group_to_parent_group(self, group_name: str, parent_name: str) -> None:
        if group_name == parent_name:
            raise AssertionError("containerElement cannot contain itself.")
        if parent_name in self._group_descendant_names(group_name):
            raise AssertionError(f"Cannot attach {group_name} into its descendant {parent_name}.")
        parent_group = self._find_container_group(parent_name)
        if not parent_group:
            raise AssertionError(f"Missing containerElement {parent_name}")
        self._remove_group_from_parent_group(group_name)
        if group_name not in parent_group.groups:
            parent_group.groups.append(group_name)

    def _group_descendant_names(self, name: str) -> set[str]:
        descendants: set[str] = set()
        group = self._find_container_group(name)
        if not group:
            raise AssertionError(f"Missing containerElement {name}")
        for child_name in group.groups:
            if child_name in descendants:
                continue
            descendants.add(child_name)
            descendants.update(self._group_descendant_names(child_name))
        return descendants

    def _group_ancestor_names(self, name: str) -> set[str]:
        ancestors: set[str] = set()
        parent_name = self._group_parent_group_name(name)
        while parent_name:
            if parent_name in ancestors:
                raise AssertionError(f"Cycle detected in containerElement ancestry for {name}.")
            ancestors.add(parent_name)
            parent_name = self._group_parent_group_name(parent_name)
        return ancestors

    def _container_names_in_group_tree(self, name: str) -> set[str]:
        group = self._find_container_group(name)
        if not group:
            raise AssertionError(f"Missing containerElement {name}")
        names = set(group.variables) | set(group.arrays)
        for child_name in group.groups:
            names.update(self._container_names_in_group_tree(child_name))
        return names

    def _group_body_bounds(self, group: ContainerGroupItem) -> tuple[float, float, float, float]:
        header_height = 30.0
        width, height = self._container_group_render_size(group)
        x, y = self._container_display_origin("containerelement", group.name, group.x, group.y)
        return x, y + header_height, x + width, y + height

    def _nearest_parent_group_name(self, kind: str, name: str) -> str | None:
        if kind == "container":
            return self._container_parent_group_name(name)
        if kind == "containerelement":
            return self._group_parent_group_name(name)
        return None

    def _node_has_collapsed_ancestor(self, kind: str, name: str) -> bool:
        parent_name = self._nearest_parent_group_name(kind, name)
        while parent_name:
            if self._is_hidden_root_group_name(parent_name):
                parent_name = self._group_parent_group_name(parent_name)
                continue
            parent_group = self._find_container_group(parent_name)
            if not parent_group:
                raise AssertionError(f"Missing ancestor containerElement {parent_name}")
            if not self._is_node_expanded(parent_group):
                return True
            parent_name = self._group_parent_group_name(parent_name)
        return False

    def _is_node_visible_in_current_view(self, kind: str, name: str) -> bool:
        if kind == "containerelement" and self._is_hidden_root_group_name(name):
            return False
        if kind in {"container", "containerelement"} and self._node_has_collapsed_ancestor(kind, name):
            return False
        if kind == "decomposer":
            return False
        if self.canvas_view_mode == "graph":
            if kind == "resnode":
                return False
        if kind == "containerelement":
            return self._find_container_group(name) is not None and self._scene_consumes_node_zone(kind, name)
        if kind == "container":
            return self._find_container(name) is not None and self._scene_consumes_node_zone(kind, name)
        if self.canvas_view_mode == "all_in_one":
            if kind == "decomposer":
                return self._find_rule(name) is not None
            if kind == "reflector":
                return self._find_reflector(name) is not None
            if kind == "resnode":
                return self._find_res_node(name) is not None
            if kind == "function":
                return self._find_function_frame(name) is not None
            if kind == "functiontext":
                return self._find_function_text_item(name) is not None
            if kind in {"interventioner", "stage"}:
                return self._find_stage(name) is not None
        if self.canvas_view_mode == "decomposer_overview":
            if kind == "decomposer":
                return self._find_rule(name) is not None
            if kind == "resnode":
                return self._find_res_node(name) is not None
            return False
        if self.canvas_view_mode == "reflector_overview":
            if kind == "reflector":
                return self._find_reflector(name) is not None
            return False
        if self.canvas_view_mode == "interventioner_overview":
            if kind in {"interventioner", "stage"}:
                return self._find_stage(name) is not None
            if kind == "function":
                return self._find_function_frame(name) is not None
            if kind == "functiontext":
                return self._find_function_text_item(name) is not None
            return False
        if self.canvas_view_mode in {"interventioner_pretick", "interventioner_aftertick", "interventioner_render"}:
            if kind in {"interventioner", "stage"}:
                return False
            if kind == "function":
                return self._find_function_frame(name) is not None and self._is_function_visible_in_current_view(name)
            if kind == "functiontext":
                item = self._find_function_text_item(name)
                return item is not None and self._is_function_visible_in_current_view(item.function_name)
            return False
        if self.canvas_view_mode == "decomposer2container_overview":
            if kind == "resnode":
                return self._find_res_node(name) is not None
            return False
        if self.canvas_view_mode == "graph":
            if kind == "reflector":
                return self._find_reflector(name) is not None
            if kind == "function":
                return self._find_function_frame(name) is not None
            if kind == "functiontext":
                return self._find_function_text_item(name) is not None
            if kind in {"interventioner", "stage"}:
                return self._find_stage(name) is not None
            return False
        if self.canvas_view_mode == "container_overview":
            return False
        raise AssertionError(f"Unsupported canvas view mode: {self.canvas_view_mode}")

    def _smallest_group_containing_rect(
        self,
        rect: tuple[float, float, float, float],
        *,
        exclude_group_names: set[str] | None = None,
    ) -> str | None:
        exclude_group_names = exclude_group_names or set()
        chosen_name: str | None = None
        chosen_area = float("inf")
        for group in self.project.container_groups:
            if group.name in exclude_group_names:
                continue
            if not self._is_node_visible_in_current_view("containerelement", group.name):
                continue
            if not self._is_node_expanded(group):
                continue
            body_rect = self._group_body_bounds(group)
            if not self.project._rect_contains_rect(*body_rect, *rect):
                continue
            area = group.width * group.height
            if area < chosen_area:
                chosen_area = area
                chosen_name = group.name
        return chosen_name

    def _sync_container_group_membership(self, group: ContainerGroupItem) -> None:
        self.project.validate_container_group(group)

    def _sync_all_container_groups(self) -> None:
        self._ensure_singleton_container_group(self.project)
        self._ensure_singleton_resource_group(self.project)
        self.project.sync_container_groups_from_geometry()

    def _create_container_group_from_selection(self, name: str, x: float, y: float, width: float, height: float) -> ContainerGroupItem:
        group = ContainerGroupItem(name=name, x=x, y=y, width=width, height=height)
        self._pack_container_group_contents(group)
        self.project.validate_container_group(group)
        self.project.container_groups.append(group)
        return group

    def _container_group_direct_layout_items(self, group: ContainerGroupItem) -> list[tuple[str, Any]]:
        items: list[tuple[str, Any]] = []
        for child_name in group.groups:
            child_group = self._find_container_group(child_name)
            if not child_group:
                raise AssertionError(f"Missing child containerElement {child_name} in {group.name}.")
            items.append(("containerelement", child_group))
        for name in group.variables:
            container = self._find_container(name)
            if container:
                items.append(("container", container))
        for name in group.arrays:
            container = self._find_container(name)
            if container:
                items.append(("container", container))
        return items

    def _pack_container_group_contents(self, group: ContainerGroupItem) -> None:
        layout_items = self._container_group_direct_layout_items(group)
        start_y = group.y + 68.0
        vertical_gap = 16.0
        min_width, min_height = self._container_group_min_size(group)
        group.width = max(group.width, min_width)
        group.height = max(group.height, min_height)
        inner_width = max(group.width - 32.0, 64.0)
        cursor_y = start_y
        max_child_width = 0.0
        for child_kind, item in layout_items:
            if child_kind == "containerelement":
                child_group = item
                child_group.width = max(inner_width, self._container_group_min_size(child_group)[0])
                self._pack_container_group_contents(child_group)
                child_width, child_height = self._container_group_render_size(child_group)
                target_x = group.x + (group.width - child_width) / 2
                dx = target_x - child_group.x
                dy = cursor_y - child_group.y
                if dx or dy:
                    self._move_container_group_and_members(child_group, dx, dy)
                max_child_width = max(max_child_width, child_width)
                cursor_y += child_height + vertical_gap
                continue
            container = item
            min_child_width, min_child_height = self._container_min_render_size(container)
            container.width = max(inner_width, min_child_width)
            container.height = max(float(getattr(container, "height", 0.0) or 0.0), min_child_height)
            child_width, child_height = self._container_render_size(container)
            container.x = group.x + (group.width - child_width) / 2
            container.y = cursor_y
            max_child_width = max(max_child_width, child_width)
            cursor_y += child_height + vertical_gap
        required_width = max(min_width, max_child_width + 32.0)
        content_height = 68.0 if not layout_items else cursor_y - vertical_gap - group.y
        required_height = max(min_height, content_height + 20.0)
        group.width = max(group.width, required_width)
        group.height = max(group.height, required_height)

    def _pack_all_container_groups(self) -> None:
        for group in sorted(self.project.container_groups, key=lambda item: item.width * item.height):
            self._pack_container_group_contents(group)

    def _resize_group_contents_proportionally(
        self,
        group: ContainerGroupItem,
        old_body_bounds: tuple[float, float, float, float],
        new_body_bounds: tuple[float, float, float, float],
    ) -> None:
        old_left, old_top, old_right, old_bottom = old_body_bounds
        new_left, new_top, new_right, new_bottom = new_body_bounds
        old_width = old_right - old_left
        old_height = old_bottom - old_top
        new_width = new_right - new_left
        new_height = new_bottom - new_top
        if old_width <= 0.0 or old_height <= 0.0:
            raise AssertionError(f"Invalid old body bounds for {group.name}: {old_body_bounds}")
        if new_width <= 0.0 or new_height <= 0.0:
            raise AssertionError(f"Invalid new body bounds for {group.name}: {new_body_bounds}")
        scale_x = new_width / old_width
        scale_y = new_height / old_height

        for child_name in group.groups:
            child_group = self._find_container_group(child_name)
            if not child_group:
                raise AssertionError(f"Missing child containerElement {child_name} in {group.name}.")
            old_child_bounds = self._group_bounds(child_group)
            old_child_body_bounds = self._group_body_bounds(child_group)
            child_group.x = new_left + (old_child_bounds[0] - old_left) * scale_x
            child_group.y = new_top + (old_child_bounds[1] - old_top) * scale_y
            min_child_width, min_child_height = self._container_group_min_size(child_group)
            child_group.width = max(min_child_width, (old_child_bounds[2] - old_child_bounds[0]) * scale_x)
            child_group.height = max(min_child_height, (old_child_bounds[3] - old_child_bounds[1]) * scale_y)
            new_child_body_bounds = self._group_body_bounds(child_group)
            self._resize_group_contents_proportionally(child_group, old_child_body_bounds, new_child_body_bounds)

        for container_name in list(group.variables) + list(group.arrays):
            container = self._find_container(container_name)
            if not container:
                raise AssertionError(f"Missing container {container_name} in {group.name}.")
            old_child_bounds = self._container_rect(container)
            min_child_width, min_child_height = self._container_min_render_size(container)
            container.x = new_left + (old_child_bounds[0] - old_left) * scale_x
            container.y = new_top + (old_child_bounds[1] - old_top) * scale_y
            container.width = max(min_child_width, (old_child_bounds[2] - old_child_bounds[0]) * scale_x)
            container.height = max(min_child_height, (old_child_bounds[3] - old_child_bounds[1]) * scale_y)
