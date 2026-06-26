from __future__ import annotations

import re

try:
    from .backend import ContainerGroupItem, ContainerItem, ProjectState
    from .shared import NODE_HEIGHT, NODE_WIDTH, _load_algorithm_studio_settings, _save_algorithm_studio_settings
except ImportError:
    from backend import ContainerGroupItem, ContainerItem, ProjectState
    from shared import NODE_HEIGHT, NODE_WIDTH, _load_algorithm_studio_settings, _save_algorithm_studio_settings


LEGACY_ALGORITHM_NAME_PREFIX = "vxax"
RESOURCE_ROOT_GROUP_NAME = "resourceRoot"
ALGORITHM_SYNC_ZONE = "algorithm"
RESOURCE_SYNC_ZONE = "resource"
CONTAINER_SCENE_SCOPE_CONTAINER = "container_overview"
CONTAINER_SCENE_SCOPE_DECOMPOSER = "decomposer_overview"
CONTAINER_SCENE_SCOPE_D2C = "decomposer2container_overview"
CONTAINER_SCENE_SCOPE_GRAPH = "graph"
CONTAINER_SCENE_SCOPE_REFLECTOR = "reflector_overview"
CONTAINER_SCENE_SCOPE_INTERVENTIONER = "interventioner_overview"
CONTAINER_SCENE_SCOPE_ALL_IN_ONE = "all_in_one"


class AlgorithmStudioIdentityMixin:
    def _normalize_container_scene_scope(self, scope: str) -> str:
        normalized = str(scope or "").strip().lower()
        if not normalized:
            return ""
        if normalized in {"interventioner_pretick", "interventioner_aftertick", "interventioner_render"}:
            return CONTAINER_SCENE_SCOPE_INTERVENTIONER
        if normalized not in {
            CONTAINER_SCENE_SCOPE_CONTAINER,
            CONTAINER_SCENE_SCOPE_DECOMPOSER,
            CONTAINER_SCENE_SCOPE_D2C,
            CONTAINER_SCENE_SCOPE_GRAPH,
            CONTAINER_SCENE_SCOPE_REFLECTOR,
            CONTAINER_SCENE_SCOPE_INTERVENTIONER,
            CONTAINER_SCENE_SCOPE_ALL_IN_ONE,
        }:
            raise AssertionError(f"Unsupported container scene scope: {scope}")
        return normalized

    def _inventory_scene_scope_for_zone(self, zone: str) -> str:
        normalized_zone = self._normalize_sync_zone(zone)
        if normalized_zone == RESOURCE_SYNC_ZONE:
            return CONTAINER_SCENE_SCOPE_DECOMPOSER
        return CONTAINER_SCENE_SCOPE_CONTAINER

    def _current_container_scene_scope(self, view_mode: str | None = None) -> str:
        normalized = str(view_mode or self.canvas_view_mode).strip().lower()
        return self._normalize_container_scene_scope(normalized)

    def _scene_scope_matches_view(self, scope: str, view_mode: str | None = None) -> bool:
        normalized_scope = self._normalize_container_scene_scope(scope)
        view_scope = self._current_container_scene_scope(view_mode)
        return normalized_scope == view_scope

    def _container_group_scene_scope(self, group: ContainerGroupItem) -> str:
        raw_scope = self._normalize_container_scene_scope(str(getattr(group, "scene_scope", "") or ""))
        if raw_scope:
            return raw_scope
        if self._is_hidden_root_group_name(group.name):
            return self._inventory_scene_scope_for_zone(self._sync_zone_for_node("containerelement", group.name))
        parent_name = self._group_parent_group_name(group.name)
        if parent_name:
            parent_group = self._find_container_group(parent_name)
            if parent_group is None:
                raise AssertionError(f"Missing parent containerElement {parent_name}")
            return self._container_group_scene_scope(parent_group)
        return self._inventory_scene_scope_for_zone(self._sync_zone_for_node("containerelement", group.name))

    def _container_scene_scope(self, container: ContainerItem) -> str:
        raw_scope = self._normalize_container_scene_scope(str(getattr(container, "scene_scope", "") or ""))
        if raw_scope:
            return raw_scope
        parent_name = self._container_parent_group_name(container.name)
        if parent_name:
            parent_group = self._find_container_group(parent_name)
            if parent_group is None:
                raise AssertionError(f"Missing parent containerElement {parent_name}")
            return self._container_group_scene_scope(parent_group)
        return self._inventory_scene_scope_for_zone(self._sync_zone_for_node("container", container.name))

    def _node_scene_scope(self, kind: str, name: str) -> str:
        normalized_kind = str(kind).strip().lower()
        normalized_name = str(name).strip()
        if normalized_kind == "container":
            container = self._find_container(normalized_name)
            if container is None:
                raise AssertionError(f"Missing container {normalized_name}")
            return self._container_scene_scope(container)
        if normalized_kind == "containerelement":
            group = self._find_container_group(normalized_name)
            if group is None:
                raise AssertionError(f"Missing containerElement {normalized_name}")
            return self._container_group_scene_scope(group)
        raise AssertionError(f"Scene scope is only defined for container-like nodes, got {kind}:{name}")

    def _set_node_scene_scope(self, item: object, scope: str) -> None:
        normalized_scope = self._normalize_container_scene_scope(scope)
        if isinstance(item, (ContainerItem, ContainerGroupItem)):
            item.scene_scope = normalized_scope
            return
        raise AssertionError(f"Scene scope is only supported for container-like nodes, got {type(item).__name__}")

    def _normalize_container_scene_scopes(self) -> None:
        for group in self.project.container_groups:
            group.scene_scope = self._container_group_scene_scope(group)
        for container in self.project.containers:
            container.scene_scope = self._container_scene_scope(container)

    def _normalize_sync_zone(self, zone: str) -> str:
        normalized = str(zone).strip().lower()
        if normalized not in {ALGORITHM_SYNC_ZONE, RESOURCE_SYNC_ZONE}:
            raise AssertionError(f"Unsupported sync zone: {zone}")
        return normalized

    def _is_hidden_root_group_name(self, name: str) -> bool:
        normalized = str(name).strip()
        return normalized in {"container", RESOURCE_ROOT_GROUP_NAME}

    def _sync_zone_root_group_name(self, zone: str) -> str:
        normalized = self._normalize_sync_zone(zone)
        return RESOURCE_ROOT_GROUP_NAME if normalized == RESOURCE_SYNC_ZONE else "container"

    def _scene_sync_zones(self, view_mode: str | None = None) -> tuple[str, ...]:
        normalized = str(view_mode or self.canvas_view_mode).strip().lower()
        if normalized == "decomposer_overview":
            return (RESOURCE_SYNC_ZONE,)
        if normalized == "decomposer2container_overview":
            return (RESOURCE_SYNC_ZONE, ALGORITHM_SYNC_ZONE)
        if normalized == "all_in_one":
            return (RESOURCE_SYNC_ZONE, ALGORITHM_SYNC_ZONE)
        if normalized in {
            "graph",
            "container_overview",
            "reflector_overview",
            "interventioner_overview",
            "interventioner_pretick",
            "interventioner_aftertick",
            "interventioner_render",
        }:
            return (ALGORITHM_SYNC_ZONE,)
        raise AssertionError(f"Unsupported canvas view mode: {normalized}")

    def _primary_sync_zone_for_view(self, view_mode: str | None = None) -> str:
        normalized = str(view_mode or self.canvas_view_mode).strip().lower()
        return RESOURCE_SYNC_ZONE if normalized == "decomposer_overview" else ALGORITHM_SYNC_ZONE

    def _selection_sync_zone(self, selection: dict[str, object] | None = None) -> str:
        payload = selection or self.selection_state
        if not payload:
            return self._primary_sync_zone_for_view()
        zones: set[str] = set()
        scope_group_name = str(payload.get("scope_group") or "").strip()
        if scope_group_name:
            zones.add(self._sync_zone_for_node("containerelement", scope_group_name))
        for group_name in payload.get("groups", []) or []:
            zones.add(self._sync_zone_for_node("containerelement", str(group_name)))
        for container_name in payload.get("variables", []) or []:
            zones.add(self._sync_zone_for_node("container", str(container_name)))
        for container_name in payload.get("arrays", []) or []:
            zones.add(self._sync_zone_for_node("container", str(container_name)))
        for resnode_name in payload.get("resnodes", []) or []:
            zones.add(self._sync_zone_for_node("resnode", str(resnode_name)))
        if not zones:
            return self._primary_sync_zone_for_view()
        if len(zones) != 1:
            raise AssertionError(f"Selection spans multiple sync zones: {sorted(zones)}")
        return next(iter(zones))

    def _sync_zone_for_node(self, kind: str, name: str) -> str:
        normalized_kind = str(kind).strip().lower()
        normalized_name = str(name).strip()
        if normalized_kind == "resnode":
            return RESOURCE_SYNC_ZONE
        if normalized_kind == "container":
            return RESOURCE_SYNC_ZONE if self._is_resource_container_name(normalized_name) else ALGORITHM_SYNC_ZONE
        if normalized_kind == "containerelement":
            return RESOURCE_SYNC_ZONE if self._is_resource_group_name(normalized_name) else ALGORITHM_SYNC_ZONE
        raise AssertionError(f"Sync zone is only defined for container-like nodes, got {kind}:{name}")

    def _scene_consumes_node_zone(self, kind: str, name: str, view_mode: str | None = None) -> bool:
        zone = self._sync_zone_for_node(kind, name)
        if zone not in self._scene_sync_zones(view_mode):
            return False
        return self._scene_scope_matches_view(self._node_scene_scope(kind, name), view_mode)

    def _next_container_name_for_zone(self, kind: str, zone: str) -> str:
        normalized_zone = self._normalize_sync_zone(zone)
        return self.project.next_resource_container_name(kind) if normalized_zone == RESOURCE_SYNC_ZONE else self.project.next_container_name(kind)

    def _next_group_name_for_zone(self, zone: str) -> str:
        normalized_zone = self._normalize_sync_zone(zone)
        return self.project.next_resource_group_name() if normalized_zone == RESOURCE_SYNC_ZONE else self.project.next_container_group_name()

    def _settings_payload(self) -> dict[str, str]:
        return {
            "provider": self.provider_var.get().strip(),
            "model": self.model_var.get().strip(),
            "approval_mode": self.approval_mode_var.get().strip(),
            "base_url": self.base_url_var.get().strip(),
            "api_key": self.api_key_var.get().strip(),
            "agent_command": self.agent_command_var.get().strip(),
        }

    def _apply_saved_settings(self) -> None:
        settings = _load_algorithm_studio_settings()
        self.provider_var.set(settings.get("provider", self.provider_var.get()))
        self.model_var.set(settings.get("model", self.model_var.get()))
        self.approval_mode_var.set(settings.get("approval_mode", self.approval_mode_var.get()))
        self.base_url_var.set(settings.get("base_url", self.base_url_var.get()))
        self.api_key_var.set(settings.get("api_key", self.api_key_var.get()))
        self.agent_command_var.set(settings.get("agent_command", self.agent_command_var.get()))

    def _install_settings_persistence(self) -> None:
        self.provider_var.trace_add("write", self._on_settings_changed)
        self.model_var.trace_add("write", self._on_settings_changed)
        self.approval_mode_var.trace_add("write", self._on_settings_changed)
        self.base_url_var.trace_add("write", self._on_settings_changed)
        self.api_key_var.trace_add("write", self._on_settings_changed)
        self.agent_command_var.trace_add("write", self._on_settings_changed)
        self.algorithm_suffix_var.trace_add("write", self._on_algorithm_suffix_changed)

    def _algorithm_suffix_from_full_name(self, full_name: str) -> str:
        text = str(full_name or "").strip()
        match = re.match(r"^v\d+a\d+", text)
        if match:
            return text[match.end() :]
        if text.startswith(LEGACY_ALGORITHM_NAME_PREFIX):
            return text[len(LEGACY_ALGORITHM_NAME_PREFIX) :]
        return text

    def _algorithm_count_prefix(self) -> str:
        variable_count, array_count = self._algorithm_shareptr_counts(self.project)
        return f"v{variable_count}a{array_count}"

    def _container_shareptr_key(self, container: ContainerItem) -> str:
        chain_id = str(container.reuse_chain_id or "").strip()
        if chain_id:
            return chain_id
        return f"standalone:{container.name}"

    def _algorithm_shareptr_counts(self, project: ProjectState) -> tuple[int, int]:
        variable_keys: set[str] = set()
        array_keys: set[str] = set()
        for item in project.containers:
            if self._is_resource_container_name(item.name):
                continue
            key = self._container_shareptr_key(item)
            if item.kind == "variable":
                variable_keys.add(key)
            elif item.kind == "array":
                array_keys.add(key)
            else:
                raise AssertionError(f"Unsupported container kind: {item.kind}")
        return len(variable_keys), len(array_keys)

    def _compose_algorithm_name(self, suffix: str) -> str:
        normalized_suffix = self._algorithm_suffix_from_full_name(suffix).strip()
        prefix = self._algorithm_count_prefix()
        self.algorithm_name_prefix = prefix
        return f"{prefix}{normalized_suffix}"

    def _normalize_project_algorithm_identity(self, project: ProjectState) -> None:
        suffix = self._algorithm_suffix_from_full_name(project.algorithm_name or project.package_name)
        if not suffix:
            suffix = "new_algorithm"
        variable_count, array_count = self._algorithm_shareptr_counts(project)
        prefix = f"v{variable_count}a{array_count}"
        self.algorithm_name_prefix = prefix
        full_name = f"{prefix}{suffix}"
        project.algorithm_name = full_name
        project.package_name = full_name

    def _is_resource_container_name(self, name: str) -> bool:
        normalized = str(name).strip()
        return normalized.startswith("resV") or normalized.startswith("resA")

    def _is_resource_group_name(self, name: str) -> bool:
        normalized = str(name).strip()
        return normalized == RESOURCE_ROOT_GROUP_NAME or normalized.startswith("resContainerElement_")

    def _ensure_singleton_container_group(self, project: ProjectState) -> None:
        root_group = next((group for group in project.container_groups if group.name == "container"), None)
        if root_group is None:
            root_group = ContainerGroupItem(
                name="container",
                scene_scope=CONTAINER_SCENE_SCOPE_CONTAINER,
                x=120.0,
                y=80.0,
                width=420.0,
                height=260.0,
                expand=True,
            )
            project.container_groups.insert(0, root_group)
        root_group.scene_scope = CONTAINER_SCENE_SCOPE_CONTAINER
        inventory_scope = CONTAINER_SCENE_SCOPE_CONTAINER
        owned_container_names = {
            name
            for group in project.container_groups
            for name in list(group.variables) + list(group.arrays)
            if group.name != "container"
            and not self._is_resource_group_name(group.name)
            and self._container_group_scene_scope(group) == inventory_scope
        }
        owned_group_names = {
            name
            for group in project.container_groups
            for name in group.groups
            if group.name != "container"
            and not self._is_resource_group_name(group.name)
            and self._container_group_scene_scope(group) == inventory_scope
        }
        direct_variables = [
            name
            for name in root_group.variables
            if (container := self._find_container(name)) is not None and self._container_scene_scope(container) == inventory_scope
        ]
        direct_arrays = [
            name
            for name in root_group.arrays
            if (container := self._find_container(name)) is not None and self._container_scene_scope(container) == inventory_scope
        ]
        direct_groups = [
            name
            for name in root_group.groups
            if name != "container"
            and (group := self._find_container_group(name)) is not None
            and self._container_group_scene_scope(group) == inventory_scope
        ]
        for container in project.containers:
            if self._is_resource_container_name(container.name):
                continue
            if self._container_scene_scope(container) != inventory_scope:
                continue
            if container.name in owned_container_names:
                continue
            if container.kind == "variable":
                if container.name not in direct_variables:
                    direct_variables.append(container.name)
            elif container.kind == "array":
                if container.name not in direct_arrays:
                    direct_arrays.append(container.name)
            else:
                raise AssertionError(f"Unsupported container kind: {container.kind}")
        for group in project.container_groups:
            if group.name == "container":
                continue
            if self._is_resource_group_name(group.name):
                continue
            if self._container_group_scene_scope(group) != inventory_scope:
                continue
            if group.name in owned_group_names:
                continue
            if group.name not in direct_groups:
                direct_groups.append(group.name)
        root_group.variables = direct_variables
        root_group.arrays = direct_arrays
        root_group.groups = direct_groups
        left = root_group.x
        top = root_group.y
        right = root_group.x + root_group.width
        bottom = root_group.y + root_group.height
        has_member = False
        for container in project.containers:
            if self._is_resource_container_name(container.name):
                continue
            if self._container_scene_scope(container) != inventory_scope:
                continue
            if container.name not in root_group.variables and container.name not in root_group.arrays:
                continue
            has_member = True
            left = min(left, container.x - 24.0)
            top = min(top, container.y - 48.0)
            right = max(right, container.x + NODE_WIDTH + 24.0)
            bottom = max(bottom, container.y + NODE_HEIGHT + 24.0)
        for group in project.container_groups:
            if (
                group.name == "container"
                or self._is_resource_group_name(group.name)
                or self._container_group_scene_scope(group) != inventory_scope
                or group.name not in root_group.groups
            ):
                continue
            has_member = True
            left = min(left, group.x - 24.0)
            top = min(top, group.y - 48.0)
            right = max(right, group.x + group.width + 24.0)
            bottom = max(bottom, group.y + group.height + 24.0)
        if has_member:
            root_group.x = left
            root_group.y = top
            root_group.width = max(420.0, right - left)
            root_group.height = max(260.0, bottom - top)

    def _ensure_singleton_resource_group(self, project: ProjectState) -> None:
        root_group = next((group for group in project.container_groups if group.name == RESOURCE_ROOT_GROUP_NAME), None)
        if root_group is None:
            root_group = ContainerGroupItem(
                name=RESOURCE_ROOT_GROUP_NAME,
                scene_scope=CONTAINER_SCENE_SCOPE_DECOMPOSER,
                x=120.0,
                y=80.0,
                width=420.0,
                height=260.0,
                expand=True,
            )
            project.container_groups.append(root_group)
        root_group.scene_scope = CONTAINER_SCENE_SCOPE_DECOMPOSER
        inventory_scope = CONTAINER_SCENE_SCOPE_DECOMPOSER
        owned_container_names = {
            name
            for group in project.container_groups
            for name in list(group.variables) + list(group.arrays)
            if group.name != RESOURCE_ROOT_GROUP_NAME
            and self._is_resource_group_name(group.name)
            and self._container_group_scene_scope(group) == inventory_scope
        }
        owned_group_names = {
            name
            for group in project.container_groups
            for name in group.groups
            if group.name != RESOURCE_ROOT_GROUP_NAME
            and self._is_resource_group_name(group.name)
            and self._container_group_scene_scope(group) == inventory_scope
        }
        direct_variables = [
            name
            for name in root_group.variables
            if self._is_resource_container_name(name)
            and (container := self._find_container(name)) is not None
            and self._container_scene_scope(container) == inventory_scope
        ]
        direct_arrays = [
            name
            for name in root_group.arrays
            if self._is_resource_container_name(name)
            and (container := self._find_container(name)) is not None
            and self._container_scene_scope(container) == inventory_scope
        ]
        direct_groups = [
            name
            for name in root_group.groups
            if self._is_resource_group_name(name)
            and name != RESOURCE_ROOT_GROUP_NAME
            and (group := self._find_container_group(name)) is not None
            and self._container_group_scene_scope(group) == inventory_scope
        ]
        for container in project.containers:
            if not self._is_resource_container_name(container.name):
                continue
            if self._container_scene_scope(container) != inventory_scope:
                continue
            if container.name in owned_container_names:
                continue
            if container.kind == "variable":
                if container.name not in direct_variables:
                    direct_variables.append(container.name)
            elif container.kind == "array":
                if container.name not in direct_arrays:
                    direct_arrays.append(container.name)
            else:
                raise AssertionError(f"Unsupported container kind: {container.kind}")
        for group in project.container_groups:
            if group.name == RESOURCE_ROOT_GROUP_NAME or not self._is_resource_group_name(group.name):
                continue
            if self._container_group_scene_scope(group) != inventory_scope:
                continue
            if group.name in owned_group_names:
                continue
            if group.name not in direct_groups:
                direct_groups.append(group.name)
        root_group.variables = direct_variables
        root_group.arrays = direct_arrays
        root_group.groups = direct_groups

    def _on_algorithm_suffix_changed(self, *_args: str) -> None:
        if self.algorithm_identity_syncing:
            return
        suffix = self._algorithm_suffix_from_full_name(self.algorithm_suffix_var.get()).strip()
        full_name = self._compose_algorithm_name(suffix)
        self.algorithm_identity_syncing = True
        try:
            self.project_name_var.set(full_name)
            self.package_name_var.set(full_name)
        finally:
            self.algorithm_identity_syncing = False
        self.project.algorithm_name = full_name
        self.project.package_name = full_name
        self._sync_project_manifest_cache()
        self._refresh_preview()
        self._refresh_inspector()

    def _on_settings_changed(self, *_args: object) -> None:
        self._save_settings()

    def _save_settings(self) -> None:
        _save_algorithm_studio_settings(self._settings_payload())

    def _on_close(self) -> None:
        self._save_settings()
        self._cancel_document_apply()
        self._uninstall_chat_input_drop_target()
        self.root.destroy()
