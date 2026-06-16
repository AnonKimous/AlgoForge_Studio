from __future__ import annotations

import copy
import os
import json
import shutil
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen
from urllib.parse import urlparse


PROJECT_ROOT = Path(__file__).resolve().parents[2]


def _resolve_codex_command(command: str) -> str:
    search_roots: list[Path] = []
    localappdata = os.environ.get("LOCALAPPDATA", "").strip()
    if localappdata:
        search_roots.extend(
            [
                Path(localappdata) / "OpenAI" / "Codex" / "bin",
                Path(localappdata) / "Programs" / "OpenAI" / "Codex" / "bin",
            ]
        )
    install_dir = os.environ.get("CODEX_INSTALL_DIR", "").strip()
    if install_dir:
        search_roots.append(Path(install_dir))
    for root in search_roots:
        if root.is_dir():
            matches = sorted(root.rglob("codex.exe"))
            if matches:
                return str(matches[0])
    env_command = os.environ.get("CODEX_COMMAND", "").strip()
    if env_command:
        which_env = shutil.which(env_command)
        if which_env is not None:
            return which_env
    resolved = command.strip() or "codex"
    which_resolved = shutil.which(resolved)
    if which_resolved is not None:
        return which_resolved
    return env_command or resolved


def _resolve_default_template_path() -> Path:
    candidates = [
        PROJECT_ROOT / "algorithmLib" / "algorithmSrc" / "algorithm_package_example.json",
        PROJECT_ROOT / "src" / "capabilities" / "algorithm_library" / "algorithm_package_example.json",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


DEFAULT_TEMPLATE_PATH = _resolve_default_template_path()


@dataclass
class ContainerItem:
    name: str
    kind: str
    count: int = 1
    stride: int = 4
    value: str = ""
    values: list[str] = field(default_factory=list)
    view_offset: int = 0
    x: float = 0.0
    y: float = 0.0
    locked: bool = False


@dataclass
class ContainerGroupItem:
    name: str
    variables: list[str] = field(default_factory=list)
    arrays: list[str] = field(default_factory=list)
    groups: list[str] = field(default_factory=list)
    x: float = 0.0
    y: float = 0.0
    width: float = 360.0
    height: float = 220.0
    locked: bool = False


@dataclass
class DecomposerRule:
    name: str
    source: str
    target: str
    map_kind: str = "v2v"
    descriptor_script: str = ""
    resource_mode: str = "default"
    resource_script: str = ""
    x: float = 0.0
    y: float = 0.0
    width: float = 360.0
    height: float = 220.0


@dataclass
class ReflectorItem:
    name: str
    reflect_fun: str = "direct"
    inputs_varity: list[str] = field(default_factory=list)
    inputs_array: list[str] = field(default_factory=list)
    output_kind: str = "v"
    output_name: str = ""
    direct_from: list[str] = field(default_factory=list)
    direct_to: list[str] = field(default_factory=list)
    x: float = 0.0
    y: float = 0.0
    width: float = 360.0
    height: float = 220.0


@dataclass
class ResourceNodeItem:
    name: str
    resource_types: list[str] = field(default_factory=lambda: ["mesh"])
    outputs: list[str] = field(default_factory=lambda: ["mesh"])
    resource_kind: str = "mesh"
    x: float = 0.0
    y: float = 0.0
    width: float = 360.0
    height: float = 220.0


@dataclass
class FunctionFrameItem:
    name: str
    script: str = ""
    input_name: str = "in"
    output_name: str = "out"
    expected_input: str = ""
    expected_output: str = ""
    x: float = 0.0
    y: float = 0.0
    width: float = 360.0
    height: float = 220.0


@dataclass
class ConnectionItem:
    source_kind: str
    source_name: str
    source_port: str
    target_kind: str
    target_name: str
    target_port: str = "in"


@dataclass
class InterventionStage:
    name: str
    kind: str
    used_variables: list[str] = field(default_factory=list)
    used_arrays: list[str] = field(default_factory=list)
    shader_vertex: str = ""
    shader_fragment: str = ""
    functions: list[str] = field(default_factory=list)
    pipeline: str = "graphics"
    x: float = 0.0
    y: float = 0.0
    width: float = 360.0
    height: float = 220.0


@dataclass
class ProjectState:
    algorithm_name: str = "new_algorithm"
    package_name: str = "new_algorithm"
    cpu_available: bool = True
    gpu_available: bool = True
    containers: list[ContainerItem] = field(default_factory=list)
    container_groups: list[ContainerGroupItem] = field(default_factory=list)
    decomposer_rules: list[DecomposerRule] = field(default_factory=list)
    decomposer_res: dict[str, Any] = field(default_factory=dict)
    reflector_items: list[ReflectorItem] = field(default_factory=list)
    res_nodes: list[ResourceNodeItem] = field(default_factory=list)
    function_frames: list[FunctionFrameItem] = field(default_factory=list)
    connections: list[ConnectionItem] = field(default_factory=list)
    intervention_stages: list[InterventionStage] = field(default_factory=list)
    notes: str = ""

    def next_container_name(self, kind: str) -> str:
        prefix = "v" if kind == "variable" else "a"
        index = 1
        while True:
            candidate = f"{prefix}{index}"
            if all(container.name != candidate for container in self.containers):
                return candidate
            index += 1

    def next_container_group_name(self) -> str:
        index = 1
        while True:
            candidate = f"containerElement_{index}"
            if all(group.name != candidate for group in self.container_groups):
                return candidate
            index += 1

    def next_decomposer_name(self) -> str:
        index = 1
        while True:
            candidate = f"decomposer_{index}"
            if all(rule.name != candidate for rule in self.decomposer_rules):
                return candidate
            index += 1

    def next_reflector_name(self) -> str:
        index = 1
        while True:
            candidate = f"reflector_{index}"
            if all(item.name != candidate for item in self.reflector_items):
                return candidate
            index += 1

    def next_res_name(self) -> str:
        index = 1
        while True:
            candidate = f"resNode_{index}"
            if all(item.name != candidate for item in self.res_nodes):
                return candidate
            index += 1

    def next_function_name(self) -> str:
        index = 1
        while True:
            candidate = f"function_{index}"
            if all(item.name != candidate for item in self.function_frames):
                return candidate
            index += 1

    def next_intervention_name(self) -> str:
        index = 1
        while True:
            candidate = f"interventioner_{index}"
            if all(stage.name != candidate for stage in self.intervention_stages):
                return candidate
            index += 1

    def next_stage_name(self) -> str:
        return self.next_intervention_name()

    def _normalize_kind(self, kind: str) -> str:
        normalized = str(kind).strip()
        if not normalized:
            raise ValueError("Node kind is required.")
        lowered = normalized.replace("_", "").replace("-", "").lower()
        if lowered == "resnode":
            return "resnode"
        if lowered == "function":
            return "function"
        if lowered == "stage":
            return "interventioner"
        if lowered in {"containerelement", "containergroup"}:
            return "containerelement"
        return lowered

    def _resource_output_ports(self, resource_kind: str, outputs: list[str] | None = None) -> list[str]:
        kind = self._normalize_kind(resource_kind)
        if outputs:
            return list(outputs)
        if kind:
            return [kind]
        return ["out"]

    def _resolve_node(self, kind: str, name: str) -> Any:
        node_kind = self._normalize_kind(kind)
        node_name = str(name).strip()
        if not node_name:
            raise ValueError("Node name is required.")
        if node_kind == "container":
            for item in self.containers:
                if item.name == node_name:
                    return item
        elif node_kind == "containerelement":
            for item in self.container_groups:
                if item.name == node_name:
                    return item
        elif node_kind == "decomposer":
            for item in self.decomposer_rules:
                if item.name == node_name:
                    return item
        elif node_kind == "reflector":
            for item in self.reflector_items:
                if item.name == node_name:
                    return item
        elif node_kind == "interventioner":
            for item in self.intervention_stages:
                if item.name == node_name:
                    return item
        elif node_kind == "resnode":
            for item in self.res_nodes:
                if item.name == node_name:
                    return item
        elif node_kind == "function":
            for item in self.function_frames:
                if item.name == node_name:
                    return item
        raise ValueError(f"Unknown node reference: {node_kind}:{node_name}")

    def iter_nodes(self) -> list[tuple[str, str, Any]]:
        nodes: list[tuple[str, str, Any]] = []
        nodes.extend(("containerelement", item.name, item) for item in self.container_groups)
        nodes.extend(("container", item.name, item) for item in self.containers)
        nodes.extend(("decomposer", item.name, item) for item in self.decomposer_rules)
        nodes.extend(("reflector", item.name, item) for item in self.reflector_items)
        nodes.extend(("interventioner", item.name, item) for item in self.intervention_stages)
        nodes.extend(("resnode", item.name, item) for item in self.res_nodes)
        nodes.extend(("function", item.name, item) for item in self.function_frames)
        return nodes

    def input_ports_for(self, kind: str, name: str) -> list[str]:
        node_kind = self._normalize_kind(kind)
        if node_kind == "container":
            return ["in"]
        if node_kind == "containerelement":
            return ["in"]
        if node_kind in {"decomposer", "reflector", "interventioner", "resnode"}:
            return ["in"]
        if node_kind == "function":
            node = self._resolve_node(kind, name)
            assert isinstance(node, FunctionFrameItem)
            return [node.input_name or "in"]
        raise ValueError(f"Unsupported node kind for input ports: {node_kind}")

    def output_ports_for(self, kind: str, name: str) -> list[str]:
        node = self._resolve_node(kind, name)
        node_kind = self._normalize_kind(kind)
        if node_kind == "container":
            return ["out"]
        if node_kind == "containerelement":
            return ["out"]
        if node_kind == "decomposer":
            assert isinstance(node, DecomposerRule)
            schema_ports = self._schema_output_ports(self.decomposer_res)
            if schema_ports:
                return schema_ports
            return [node.target or "out"]
        if node_kind == "reflector":
            assert isinstance(node, ReflectorItem)
            if node.direct_to:
                return list(node.direct_to)
            if node.output_name:
                return [node.output_name]
            return [node.output_kind or "out"]
        if node_kind == "interventioner":
            assert isinstance(node, InterventionStage)
            if node.functions:
                return list(node.functions)
            return [node.kind or "out"]
        if node_kind == "resnode":
            assert isinstance(node, ResourceNodeItem)
            return self._resource_output_ports(node.resource_kind, node.outputs)
        if node_kind == "function":
            assert isinstance(node, FunctionFrameItem)
            return [node.output_name or "out"]
        raise ValueError(f"Unsupported node kind for output ports: {node_kind}")

    def validate_container_group(self, group: ContainerGroupItem) -> None:
        for variable_name in group.variables:
            match = next((item for item in self.containers if item.name == variable_name and item.kind == "variable"), None)
            if match is None:
                raise ValueError(f"Container group {group.name} references missing variable {variable_name}.")
        for array_name in group.arrays:
            match = next((item for item in self.containers if item.name == array_name and item.kind == "array"), None)
            if match is None:
                raise ValueError(f"Container group {group.name} references missing array {array_name}.")
        for child_group_name in group.groups:
            match = next((item for item in self.container_groups if item.name == child_group_name), None)
            if match is None:
                raise ValueError(f"Container group {group.name} references missing child group {child_group_name}.")
            if child_group_name == group.name:
                raise ValueError(f"Container group {group.name} cannot contain itself.")

    def _container_kind_code(self, container: ContainerItem) -> str:
        if container.kind == "variable":
            return "v"
        if container.kind == "array":
            return "a"
        raise AssertionError(f"Unsupported container kind: {container.kind}")

    def _structure_signature_from_group(self, group: ContainerGroupItem) -> tuple[Any, ...]:
        signature: list[Any] = []
        for name in group.variables:
            item = next((container for container in self.containers if container.name == name and container.kind == "variable"), None)
            if item is None:
                raise AssertionError(f"Missing variable {name} while building signature for {group.name}.")
            signature.append("v")
        for name in group.arrays:
            item = next((container for container in self.containers if container.name == name and container.kind == "array"), None)
            if item is None:
                raise AssertionError(f"Missing array {name} while building signature for {group.name}.")
            signature.append("a")
        for child_name in group.groups:
            child = next((item for item in self.container_groups if item.name == child_name), None)
            if child is None:
                raise AssertionError(f"Missing child group {child_name} while building signature for {group.name}.")
            signature.append(self._structure_signature_from_group(child))
        return tuple(sorted(signature, key=repr))

    def _structure_signature_from_schema(self, schema: Any) -> tuple[Any, ...] | str:
        if isinstance(schema, dict):
            signature: list[Any] = []
            for value in schema.values():
                signature.append(self._structure_signature_from_schema(value))
            return tuple(sorted(signature, key=repr))
        if isinstance(schema, list):
            signature = [self._structure_signature_from_schema(value) for value in schema]
            return tuple(sorted(signature, key=repr))
        text = str(schema).strip().lower()
        if text.startswith("v"):
            return "v"
        if text.startswith("a"):
            return "a"
        return text or "?"

    def _schema_output_ports(self, schema: Any, prefix: str = "") -> list[str]:
        if isinstance(schema, dict):
            ports: list[str] = []
            for key, value in schema.items():
                child_prefix = f"{prefix}.{key}" if prefix else str(key)
                child_ports = self._schema_output_ports(value, child_prefix)
                if child_ports:
                    ports.extend(child_ports)
                else:
                    ports.append(child_prefix)
            return ports
        if isinstance(schema, list):
            ports: list[str] = []
            for index, value in enumerate(schema, start=1):
                child_prefix = f"{prefix}[{index}]" if prefix else str(index)
                child_ports = self._schema_output_ports(value, child_prefix)
                if child_ports:
                    ports.extend(child_ports)
                else:
                    ports.append(child_prefix)
            return ports
        text = str(schema).strip()
        if prefix:
            return [prefix]
        if text:
            return [text]
        return ["out"]

    def _decomposer_expected_container_kind(self, rule: DecomposerRule, connection: ConnectionItem) -> str | None:
        kind = rule.map_kind.strip().lower()
        if kind in {"v2v", "v2a", "a2a"}:
            left_kind, right_kind = kind.split("2", 1)
            if connection.source_kind == "decomposer":
                return right_kind
            if connection.target_kind == "decomposer":
                return left_kind
        return None

    def _validate_decomposer_connection(self, connection: ConnectionItem) -> None:
        if connection.source_kind != "decomposer" and connection.target_kind != "decomposer":
            return
        rule_name = connection.source_name if connection.source_kind == "decomposer" else connection.target_name
        rule = self._resolve_node("decomposer", rule_name)
        if not isinstance(rule, DecomposerRule):
            raise AssertionError(f"Missing decomposer rule {rule_name}.")
        other_kind = connection.target_kind if connection.source_kind == "decomposer" else connection.source_kind
        other_name = connection.target_name if connection.source_kind == "decomposer" else connection.source_name
        if rule.map_kind.strip().lower() == "filter" and not rule.descriptor_script.strip():
            raise ValueError(f"Decomposer {rule.name} uses filter mapping but has no descriptor script.")
        if rule.resource_mode == "filter" and not rule.resource_script.strip():
            raise ValueError(f"Decomposer {rule.name} uses filter resource mode but has no resource script.")
        if other_kind == "container":
            container = self._resolve_node("container", other_name)
            if not isinstance(container, ContainerItem):
                raise AssertionError(f"Missing container {other_name}.")
            expected_kind = self._decomposer_expected_container_kind(rule, connection)
            if expected_kind and self._container_kind_code(container) != expected_kind:
                raise ValueError(
                    f"Decomposer {rule.name} expects {expected_kind} container, got {container.kind}:{container.name}."
                )
            return
        if other_kind == "containerelement":
            group = self._resolve_node("containerelement", other_name)
            if not isinstance(group, ContainerGroupItem):
                raise AssertionError(f"Missing containerElement {other_name}.")
            if not self.decomposer_res:
                raise ValueError(f"Decomposer {rule.name} has no resource schema for structured connection validation.")
            schema_signature = self._structure_signature_from_schema(self.decomposer_res)
            group_signature = self._structure_signature_from_group(group)
            if schema_signature != group_signature:
                raise ValueError(
                    f"Decomposer {rule.name} resource schema does not match containerElement {group.name}."
                )
            return
        raise ValueError(
            f"Decomposer connections only support container or containerElement targets, got {other_kind}:{other_name}."
        )

    def _rect_contains_rect(
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
        return left_a <= left_b and top_a <= top_b and right_a >= right_b and bottom_a >= bottom_b

    def sync_container_groups_from_geometry(self) -> None:
        for group in self.container_groups:
            group.variables = []
            group.arrays = []
            group.groups = []

        parent_group_for_container: dict[str, str] = {}
        parent_group_for_group: dict[str, str] = {}

        for container in self.containers:
            c_left = container.x
            c_top = container.y
            c_right = container.x + 180
            c_bottom = container.y + 76
            parent_name: str | None = None
            parent_area = float("inf")
            for group in self.container_groups:
                left = group.x
                top = group.y
                right = group.x + group.width
                bottom = group.y + group.height
                if not self._rect_contains_rect(left, top, right, bottom, c_left, c_top, c_right, c_bottom):
                    continue
                area = group.width * group.height
                if area < parent_area:
                    parent_area = area
                    parent_name = group.name
            if parent_name:
                parent_group_for_container[container.name] = parent_name

        for group in self.container_groups:
            left = group.x
            top = group.y
            right = group.x + group.width
            bottom = group.y + group.height
            parent_name = None
            parent_area = float("inf")
            for candidate in self.container_groups:
                if candidate.name == group.name:
                    continue
                candidate_left = candidate.x
                candidate_top = candidate.y
                candidate_right = candidate.x + candidate.width
                candidate_bottom = candidate.y + candidate.height
                if not self._rect_contains_rect(left, top, right, bottom, candidate_left, candidate_top, candidate_right, candidate_bottom):
                    continue
                area = candidate.width * candidate.height
                if area < parent_area:
                    parent_area = area
                    parent_name = candidate.name
            if parent_name:
                parent_group_for_group[group.name] = parent_name

        for container in self.containers:
            parent_name = parent_group_for_container.get(container.name)
            if not parent_name:
                continue
            parent_group = next((item for item in self.container_groups if item.name == parent_name), None)
            if parent_group is None:
                raise AssertionError(f"Missing parent group {parent_name} for container {container.name}.")
            if container.kind == "variable":
                parent_group.variables.append(container.name)
            elif container.kind == "array":
                parent_group.arrays.append(container.name)
            else:
                raise AssertionError(f"Unsupported container kind in group {parent_group.name}: {container.kind}")

        for group in self.container_groups:
            parent_name = parent_group_for_group.get(group.name)
            if not parent_name:
                continue
            parent_group = next((item for item in self.container_groups if item.name == parent_name), None)
            if parent_group is None:
                raise AssertionError(f"Missing parent group {parent_name} for nested group {group.name}.")
            parent_group.groups.append(group.name)

        for group in self.container_groups:
            group.variables.sort()
            group.arrays.sort()
            group.groups.sort()

    def validate_connection(self, connection: ConnectionItem) -> None:
        self._validate_decomposer_connection(connection)
        source_outputs = self.output_ports_for(connection.source_kind, connection.source_name)
        target_inputs = self.input_ports_for(connection.target_kind, connection.target_name)
        if connection.source_port not in source_outputs:
            raise ValueError(
                f"Invalid source port {connection.source_kind}:{connection.source_name}:{connection.source_port}"
            )
        if connection.target_port not in target_inputs:
            raise ValueError(
                f"Invalid target port {connection.target_kind}:{connection.target_name}:{connection.target_port}"
            )

    def _ui_nodes(self) -> list[dict[str, Any]]:
        nodes: list[dict[str, Any]] = []
        for kind, name, item in self.iter_nodes():
            entry = {
                "kind": kind,
                "name": name,
                "x": float(getattr(item, "x", 0.0)),
                "y": float(getattr(item, "y", 0.0)),
            }
            if kind == "containerelement":
                entry["width"] = float(getattr(item, "width", 360.0))
                entry["height"] = float(getattr(item, "height", 220.0))
            elif kind in {"decomposer", "reflector", "resnode", "interventioner", "function"}:
                entry["width"] = float(getattr(item, "width", 360.0))
                entry["height"] = float(getattr(item, "height", 220.0))
                if kind == "function":
                    entry["script"] = str(getattr(item, "script", ""))
                    entry["input_name"] = str(getattr(item, "input_name", "in"))
                    entry["output_name"] = str(getattr(item, "output_name", "out"))
            nodes.append(entry)
        return nodes

    def _container_group_section(self) -> dict[str, Any]:
        container_group_section: dict[str, Any] = {}
        for group in self.container_groups:
            container_group_section[group.name] = {
                "variables": list(group.variables),
                "arrays": list(group.arrays),
                "groups": list(group.groups),
                "x": group.x,
                "y": group.y,
                "width": group.width,
                "height": group.height,
            }
        return container_group_section

    def _ui_connections(self) -> list[dict[str, Any]]:
        connections: list[dict[str, Any]] = []
        for connection in self.connections:
            connections.append(
                {
                    "source": {
                        "kind": connection.source_kind,
                        "name": connection.source_name,
                        "port": connection.source_port,
                    },
                    "target": {
                        "kind": connection.target_kind,
                        "name": connection.target_name,
                        "port": connection.target_port,
                    },
                }
            )
        return connections

    def to_package_json(self) -> dict[str, Any]:
        self.sync_container_groups_from_geometry()
        variable_section: dict[str, Any] = {}
        variable_array_section: dict[str, Any] = {}
        for container in self.containers:
            entry = {
                "count": container.count,
                "stride": container.stride,
                "value": container.value,
                "values": list(container.values),
                "view_offset": container.view_offset,
                "x": container.x,
                "y": container.y,
            }
            if container.kind == "variable":
                variable_section[container.name] = entry
            else:
                variable_array_section[container.name] = entry

        decomposer_description: list[dict[str, Any]] = []
        for rule in self.decomposer_rules:
            entry: dict[str, Any] = {
                "name": rule.name,
                "from": [rule.source] if rule.source else [],
                "to": [rule.target] if rule.target else [],
                "mapKind": rule.map_kind,
                "x": rule.x,
                "y": rule.y,
                "width": rule.width,
                "height": rule.height,
            }
            if rule.map_kind == "filter":
                entry["script"] = rule.descriptor_script
            if rule.resource_mode != "default" or rule.resource_script:
                entry["resourceMode"] = rule.resource_mode
                entry["resourceScript"] = rule.resource_script
            decomposer_description.append(entry)

        reflector_items: list[dict[str, Any]] = []
        for item in self.reflector_items:
            if item.reflect_fun == "direct":
                reflector_items.append(
                    {
                        "name": item.name,
                        "from": item.direct_from or item.inputs_varity + item.inputs_array,
                        "to": item.direct_to or ([item.output_name] if item.output_name else []),
                        "reflectFun": "direct",
                        "x": item.x,
                        "y": item.y,
                        "width": item.width,
                        "height": item.height,
                    }
                )
            else:
                reflector_items.append(
                    {
                        "name": item.name,
                        "input": {
                            "varity": item.inputs_varity,
                            "array": item.inputs_array,
                        },
                        "output": {
                            item.output_kind: {
                                "name": item.output_name,
                            }
                        },
                        "reflectFun": item.reflect_fun,
                        "x": item.x,
                        "y": item.y,
                        "width": item.width,
                        "height": item.height,
                    }
                )

        res_items: list[dict[str, Any]] = []
        for item in self.res_nodes:
            resource_kind = str(item.resource_kind or (item.resource_types[0] if item.resource_types else "mesh"))
            resource_types = [resource_kind]
            outputs = [resource_kind]
            res_items.append(
                {
                    "name": item.name,
                    "resource_kind": resource_kind,
                    "resourceTypes": resource_types,
                    "outputs": outputs,
                    "x": item.x,
                    "y": item.y,
                    "width": item.width,
                    "height": item.height,
                }
            )

        function_items: list[dict[str, Any]] = []
        for item in self.function_frames:
            function_items.append(
                {
                    "name": item.name,
                    "script": item.script,
                    "input_name": item.input_name,
                    "output_name": item.output_name,
                    "expected_input": item.expected_input,
                    "expected_output": item.expected_output,
                    "x": item.x,
                    "y": item.y,
                    "width": item.width,
                    "height": item.height,
                }
            )

        stage_map: dict[str, Any] = {}
        for stage in self.intervention_stages:
            stage_map[stage.name] = {
                "stage_name": stage.name,
                "stage_kind": stage.kind,
                "functions": stage.functions,
                "x": stage.x,
                "y": stage.y,
                "width": stage.width,
                "height": stage.height,
                "used_algorithm_containers": {
                    "variables": [{"name": name, "kind": "variable", "tuple_width": 1, "required": True} for name in stage.used_variables],
                    "arrays": [{"name": name, "kind": "array", "tuple_width": 1, "required": True} for name in stage.used_arrays],
                },
                "shader": {
                    "pipeline": stage.pipeline,
                    "vertex": stage.shader_vertex,
                    "fragment": stage.shader_fragment,
                },
            }

        return {
            "algorithm_name": self.algorithm_name,
            "package_name": self.package_name,
            "cpu_available": self.cpu_available,
            "gpu_available": self.gpu_available,
            "container": {
                "variable": variable_section,
                "variableArray": variable_array_section,
            },
            "containerElement": self._container_group_section(),
            "decomposer": {
                "res": self.decomposer_res,
                "description": decomposer_description,
            },
            "reflector": {
                "name": f"{self.algorithm_name}_reflector",
                "functionName": "direct",
                "items": reflector_items,
            },
            "res": {
                "items": res_items,
            },
            "function": {
                "items": function_items,
            },
            "intervention": {
                "stages": stage_map,
            },
            "ui": {
                "nodes": self._ui_nodes(),
                "connections": self._ui_connections(),
            },
            "notes": self.notes,
        }

    @classmethod
    def from_package_json(cls, payload: dict[str, Any]) -> ProjectState:
        project = cls()
        project.algorithm_name = str(payload.get("algorithm_name") or payload.get("package_name") or "new_algorithm")
        project.package_name = str(payload.get("package_name") or project.algorithm_name)
        project.cpu_available = bool(payload.get("cpu_available", True))
        project.gpu_available = bool(payload.get("gpu_available", True))
        project.notes = str(payload.get("notes") or "")

        container_section = payload.get("container", {})
        variables = container_section.get("variable", {})
        if isinstance(variables, int):
            for index in range(max(0, variables)):
                project.containers.append(ContainerItem(name=f"v{index + 1}", kind="variable"))
        elif isinstance(variables, dict):
            for name, entry in variables.items():
                values = entry.get("values", [])
                if not isinstance(values, list):
                    values = [values] if values else []
                project.containers.append(
                    ContainerItem(
                        name=str(name),
                        kind="variable",
                        count=int(entry.get("count", 1)),
                        stride=int(entry.get("stride", 4)),
                        value=str(entry.get("value") or ""),
                        values=[str(value) for value in values],
                        view_offset=int(entry.get("view_offset", 0)),
                        x=float(entry.get("x", 0.0)),
                        y=float(entry.get("y", 0.0)),
                    )
                )

        arrays = container_section.get("variableArray", {})
        if isinstance(arrays, int):
            for index in range(max(0, arrays)):
                project.containers.append(ContainerItem(name=f"a{index + 1}", kind="array"))
        elif isinstance(arrays, dict):
            for name, entry in arrays.items():
                values = entry.get("values", [])
                if not isinstance(values, list):
                    values = [values] if values else []
                project.containers.append(
                    ContainerItem(
                        name=str(name),
                        kind="array",
                        count=int(entry.get("count", 1)),
                        stride=int(entry.get("stride", 4)),
                        value=str(entry.get("value") or ""),
                        values=[str(value) for value in values],
                        view_offset=int(entry.get("view_offset", 0)),
                        x=float(entry.get("x", 0.0)),
                        y=float(entry.get("y", 0.0)),
                    )
                )

        container_groups = payload.get("containerElement", {})
        if container_groups:
            if not isinstance(container_groups, dict):
                raise ValueError("containerElement must be an object.")
            for group_name, entry in container_groups.items():
                if not isinstance(entry, dict):
                    raise ValueError(f"containerElement[{group_name}] must be an object.")
                variables_value = entry.get("variables", [])
                arrays_value = entry.get("arrays", [])
                groups_value = entry.get("groups", [])
                if not isinstance(variables_value, list):
                    raise ValueError(f"containerElement[{group_name}].variables must be a list.")
                if not isinstance(arrays_value, list):
                    raise ValueError(f"containerElement[{group_name}].arrays must be a list.")
                if not isinstance(groups_value, list):
                    raise ValueError(f"containerElement[{group_name}].groups must be a list.")
                project.container_groups.append(
                    ContainerGroupItem(
                        name=str(group_name),
                        variables=[str(value) for value in variables_value],
                        arrays=[str(value) for value in arrays_value],
                        groups=[str(value) for value in groups_value],
                        x=float(entry.get("x", 0.0)),
                        y=float(entry.get("y", 0.0)),
                        width=float(entry.get("width", 360.0)),
                        height=float(entry.get("height", 220.0)),
                    )
                )

        decomposer = payload.get("decomposer", {})
        decomposer_res = decomposer.get("res", {})
        if decomposer_res and not isinstance(decomposer_res, dict):
            raise ValueError("decomposer.res must be an object.")
        project.decomposer_res = copy.deepcopy(decomposer_res) if isinstance(decomposer_res, dict) else {}
        for index, rule in enumerate(decomposer.get("description", []), start=1):
            sources = rule.get("from", [])
            targets = rule.get("to", [])
            source = str(sources[0]) if sources else ""
            target = ""
            if isinstance(targets, list):
                target = str(targets[0]) if targets else ""
            elif isinstance(targets, dict):
                target = str(targets.get("container") or targets.get("name") or "")
            elif targets is not None:
                target = str(targets)
            if source and target:
                project.decomposer_rules.append(
                    DecomposerRule(
                        name=str(rule.get("name") or f"rule_{index}"),
                        source=source,
                        target=target,
                        map_kind=str(rule.get("mapKind") or rule.get("kind") or "v2v"),
                        descriptor_script=str(rule.get("script") or ""),
                        resource_mode=str(rule.get("resourceMode") or "default"),
                        resource_script=str(rule.get("resourceScript") or ""),
                        x=float(rule.get("x", 0.0)),
                        y=float(rule.get("y", 0.0)),
                        width=float(rule.get("width", 360.0)),
                        height=float(rule.get("height", 220.0)),
                    )
                )

        reflector = payload.get("reflector", {})
        for index, item in enumerate(reflector.get("items", []), start=1):
            direct_from = [str(value) for value in item.get("from", [])]
            direct_to = [str(value) for value in item.get("to", [])]
            input_object = item.get("input", {})
            output_object = item.get("output", {})
            output_kind = "v" if "v" in output_object else "a"
            output_name = ""
            if output_kind in output_object:
                output_name = str(output_object[output_kind].get("name") or "")
            project.reflector_items.append(
                ReflectorItem(
                    name=str(item.get("name") or f"reflector_{index}"),
                    reflect_fun=str(item.get("reflectFun") or "direct"),
                    inputs_varity=[str(value) for value in input_object.get("varity", [])],
                    inputs_array=[str(value) for value in input_object.get("array", [])],
                    output_kind=output_kind,
                    output_name=output_name,
                    direct_from=direct_from,
                    direct_to=direct_to,
                    x=float(item.get("x", 0.0)),
                    y=float(item.get("y", 0.0)),
                    width=float(item.get("width", 360.0)),
                    height=float(item.get("height", 220.0)),
                )
            )

        res_section = payload.get("res", {})
        res_items = res_section.get("items", [])
        if isinstance(res_items, list):
            for index, item in enumerate(res_items, start=1):
                resource_types = item.get("resourceTypes", item.get("resources", []))
                if not isinstance(resource_types, list):
                    resource_types = [resource_types] if resource_types else []
                resource_kind = str(item.get("resource_kind") or item.get("kind") or (resource_types[0] if resource_types else "mesh"))
                resource_types = [resource_kind]
                project.res_nodes.append(
                    ResourceNodeItem(
                        name=str(item.get("name") or f"resNode_{index}"),
                        resource_types=resource_types,
                        outputs=[resource_kind],
                        resource_kind=resource_kind,
                        x=float(item.get("x", 0.0)),
                        y=float(item.get("y", 0.0)),
                        width=float(item.get("width", 360.0)),
                        height=float(item.get("height", 220.0)),
                    )
                )

        function_section = payload.get("function", {})
        function_items = function_section.get("items", [])
        if isinstance(function_items, list):
            for index, item in enumerate(function_items, start=1):
                project.function_frames.append(
                    FunctionFrameItem(
                        name=str(item.get("name") or f"function_{index}"),
                        script=str(item.get("script") or ""),
                        input_name=str(item.get("input_name") or "in"),
                        output_name=str(item.get("output_name") or "out"),
                        expected_input=str(item.get("expected_input") or ""),
                        expected_output=str(item.get("expected_output") or ""),
                        x=float(item.get("x", 0.0)),
                        y=float(item.get("y", 0.0)),
                        width=float(item.get("width", 360.0)),
                        height=float(item.get("height", 220.0)),
                    )
                )

        stages = payload.get("intervention", {}).get("stages", {})
        if isinstance(stages, dict):
            for stage_name, stage in stages.items():
                used = stage.get("used_algorithm_containers", {})
                project.intervention_stages.append(
                    InterventionStage(
                        name=str(stage_name),
                        kind=str(stage.get("stage_kind") or stage.get("kind") or stage_name),
                        used_variables=[str(item.get("name")) for item in used.get("variables", []) if item.get("name")],
                        used_arrays=[str(item.get("name")) for item in used.get("arrays", []) if item.get("name")],
                        shader_vertex=str(stage.get("shader", {}).get("vertex") or ""),
                        shader_fragment=str(stage.get("shader", {}).get("fragment") or ""),
                        functions=[str(value) for value in stage.get("functions", [])],
                        pipeline=str(stage.get("shader", {}).get("pipeline") or "graphics"),
                        x=float(stage.get("x", 0.0)),
                        y=float(stage.get("y", 0.0)),
                        width=float(stage.get("width", 360.0)),
                        height=float(stage.get("height", 220.0)),
                    )
                )

        if not project.containers:
            project.containers.append(ContainerItem(name="v1", kind="variable"))
            project.containers.append(ContainerItem(name="a1", kind="array"))
        if not project.res_nodes:
            project.res_nodes.append(ResourceNodeItem(name="resNode_1", resource_types=["mesh"], outputs=["mesh"], resource_kind="mesh"))
        if not project.intervention_stages:
            project.intervention_stages.extend(
                [
                    InterventionStage(name="preTick", kind="preTick"),
                    InterventionStage(name="afterTick", kind="afterTick"),
                    InterventionStage(name="resultRender", kind="resultRender"),
                ]
            )

        for group in project.container_groups:
            project.validate_container_group(group)

        node_registry = {
            (kind, name): item
            for kind, name, item in project.iter_nodes()
        }
        ui_section = payload.get("ui", {})
        if ui_section:
            if not isinstance(ui_section, dict):
                raise ValueError("ui must be an object.")
            ui_nodes = ui_section.get("nodes", [])
            if ui_nodes:
                if not isinstance(ui_nodes, list):
                    raise ValueError("ui.nodes must be a list.")
                for index, entry in enumerate(ui_nodes, start=1):
                    if not isinstance(entry, dict):
                        raise ValueError(f"ui.nodes[{index}] must be an object.")
                    kind = project._normalize_kind(entry.get("kind", ""))
                    name = str(entry.get("name") or "").strip()
                    if not name:
                        raise ValueError(f"ui.nodes[{index}] is missing a name.")
                    item = node_registry.get((kind, name))
                    if item is None:
                        raise ValueError(f"ui.nodes[{index}] references unknown node {kind}:{name}.")
                    item.x = float(entry.get("x", getattr(item, "x", 0.0)))
                    item.y = float(entry.get("y", getattr(item, "y", 0.0)))
                    if hasattr(item, "width"):
                        item.width = float(entry.get("width", getattr(item, "width", 360.0)))
                    if hasattr(item, "height"):
                        item.height = float(entry.get("height", getattr(item, "height", 220.0)))

            ui_connections = ui_section.get("connections", [])
            if ui_connections:
                if not isinstance(ui_connections, list):
                    raise ValueError("ui.connections must be a list.")
                project.connections.clear()
                for index, entry in enumerate(ui_connections, start=1):
                    if not isinstance(entry, dict):
                        raise ValueError(f"ui.connections[{index}] must be an object.")
                    source = entry.get("source", entry)
                    target = entry.get("target", entry)
                    if not isinstance(source, dict) or not isinstance(target, dict):
                        raise ValueError(f"ui.connections[{index}] must contain source and target objects.")
                    source_kind = project._normalize_kind(source.get("kind", ""))
                    target_kind = project._normalize_kind(target.get("kind", ""))
                    source_name = str(source.get("name") or "").strip()
                    target_name = str(target.get("name") or "").strip()
                    source_port = str(source.get("port") or source.get("source_port") or "out").strip()
                    target_port = str(target.get("port") or target.get("target_port") or "in").strip()
                    connection = ConnectionItem(
                        source_kind=source_kind,
                        source_name=source_name,
                        source_port=source_port,
                        target_kind=target_kind,
                        target_name=target_name,
                        target_port=target_port,
                    )
                    project.validate_connection(connection)
                    project.connections.append(connection)
        return project


class MockAgentClient:
    def __init__(self) -> None:
        self.provider = "codex"
        self.model = "gpt-5.4-mini"
        self.approval_mode = "manual"
        self.base_url = ""
        self.api_key = ""
        self.codex_command = "codex"
        self.timeout_seconds = 60

    def _build_prompt(self, project: ProjectState, selection: str, prompt: str) -> str:
        summary = [
            f"selection={selection}",
            f"approval_mode={self.approval_mode}",
            f"containers={len(project.containers)}",
            f"container_groups={len(project.container_groups)}",
            f"rules={len(project.decomposer_rules)}",
            f"reflectors={len(project.reflector_items)}",
            f"res={len(project.res_nodes)}",
            f"functions={len(project.function_frames)}",
            f"stages={len(project.intervention_stages)}",
        ]
        return (
            "You are the Algorithm Studio agent.\n"
            "Use strict failures. Do not silently ignore invalid state.\n\n"
            + "\n".join(summary)
            + "\n\nPrompt:\n"
            + prompt.strip()
        )

    def _build_openai_messages(self, project: ProjectState, selection: str, prompt: str) -> list[dict[str, str]]:
        system_message = (
            "You are the Algorithm Studio agent.\n"
            "Use strict failures. Do not silently ignore invalid state.\n"
            f"Selection: {selection}\n"
            f"approval_mode={self.approval_mode}\n"
            f"containers={len(project.containers)}, "
            f"container_groups={len(project.container_groups)}, "
            f"rules={len(project.decomposer_rules)}, "
            f"reflectors={len(project.reflector_items)}, "
            f"res={len(project.res_nodes)}, "
            f"functions={len(project.function_frames)}, "
            f"stages={len(project.intervention_stages)}"
        )
        return [
            {"role": "system", "content": system_message},
            {"role": "user", "content": prompt.strip()},
        ]

    def _call_api(self, project: ProjectState, selection: str, prompt: str) -> str:
        base_url = self.base_url.strip()
        if not base_url:
            raise ValueError("API base_url is required when provider is api.")
        parsed = urlparse(base_url)
        if base_url.endswith("/chat/completions"):
            url = base_url
        elif base_url.endswith("/v1"):
            url = base_url + "/chat/completions"
        elif parsed.netloc.endswith("deepseek.com") and parsed.path in {"", "/"}:
            url = base_url.rstrip("/") + "/chat/completions"
        else:
            url = base_url.rstrip("/") + "/v1/chat/completions"
        body = {
            "model": self.model.strip() or "gpt-5.4-mini",
            "messages": self._build_openai_messages(project, selection, prompt),
        }
        payload = json.dumps(body).encode("utf-8")
        headers = {"Content-Type": "application/json"}
        if self.api_key.strip():
            headers["Authorization"] = f"Bearer {self.api_key.strip()}"
        request = Request(url, data=payload, headers=headers, method="POST")
        try:
            with urlopen(request, timeout=self.timeout_seconds) as response:
                response_text = response.read().decode("utf-8", errors="replace")
        except HTTPError as exc:
            error_body = exc.read().decode("utf-8", errors="replace") if exc.fp else ""
            raise RuntimeError(f"API request failed: {exc.code} {exc.reason}\n{error_body}") from exc
        except URLError as exc:
            raise RuntimeError(f"API request failed: {exc.reason}") from exc
        data = json.loads(response_text)
        choices = data.get("choices", [])
        if not choices:
            raise RuntimeError("API response did not contain choices.")
        message = choices[0].get("message", {})
        content = str(message.get("content") or "").strip()
        if not content:
            raise RuntimeError("API response did not contain assistant content.")
        return content

    def _call_codex(self, project: ProjectState, selection: str, prompt: str) -> str:
        command = _resolve_codex_command(self.codex_command)
        if not Path(command).exists():
            raise RuntimeError(f"Codex command not found: {command}")
        args = [
            command,
            "exec",
            "--cd",
            str(PROJECT_ROOT),
            "--color",
            "never",
        ]
        model = self.model.strip()
        if model:
            args.extend(["--model", model])
        args.append("-")
        stdin_prompt = self._build_prompt(project, selection, prompt)
        completed = subprocess.run(
            args,
            input=stdin_prompt,
            text=True,
            encoding="utf-8",
            errors="strict",
            capture_output=True,
            timeout=self.timeout_seconds,
            check=False,
        )
        output = (completed.stdout or "").strip()
        error_output = (completed.stderr or "").strip()
        if completed.returncode != 0:
            raise RuntimeError(
                "Codex command failed "
                f"(exit {completed.returncode}).\n{error_output or output or 'No output captured.'}"
            )
        if output:
            return output
        if error_output:
            return error_output
        return "Codex finished without output."

    def generate(self, project: ProjectState, selection: str, prompt: str) -> str:
        mode = self.provider.strip().lower()
        if mode == "api":
            return self._call_api(project, selection, prompt)
        if mode == "codex":
            return self._call_codex(project, selection, prompt)
        summary = [
            f"provider={self.provider}",
            f"model={self.model}",
            f"approval_mode={self.approval_mode}",
            f"selection={selection}",
            f"containers={len(project.containers)}",
            f"container_groups={len(project.container_groups)}",
            f"rules={len(project.decomposer_rules)}",
            f"reflectors={len(project.reflector_items)}",
            f"res={len(project.res_nodes)}",
            f"functions={len(project.function_frames)}",
            f"stages={len(project.intervention_stages)}",
        ]
        return (
            "Mock agent response\n"
            + "\n".join(summary)
            + "\n\nPrompt:\n"
            + prompt.strip()
            + "\n\nNext action:\n"
            + "Generate package JSON patch, C++ skeleton, or shader binding update."
        )


def generate_cpp_skeleton(project_name: str) -> str:
    return "\n".join(
        [
            "#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1",
            "",
            "#include \"capabilities/algorithm_library/algorithm_plugin_api.h\"",
            "",
            "#include <memory>",
            "#include <string>",
            "#include <utility>",
            "",
            "namespace {",
            "",
            f"constexpr const char* kAlgorithmName = \"{project_name}\";",
            "",
            "}  // namespace",
            "",
            "extern \"C\" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(",
            "  const algorithm_support::AlgorithmPluginRequest* request,",
            "  algorithm_support::AlgorithmPluginBundle* out_bundle) {",
            "  if (!request || !out_bundle) {",
            "    return false;",
            "  }",
            "  out_bundle->Clear();",
            "  (void)kAlgorithmName;",
            "  return true;",
            "}",
            "",
            "extern \"C\" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(",
            "  const algorithm_support::AlgorithmPluginRequest* request,",
            "  algorithm::AlgorithmReflector* out_reflector) {",
            "  if (!request || !out_reflector) {",
            "    return false;",
            "  }",
            "  (void)kAlgorithmName;",
            "  return true;",
            "}",
            "",
        ]
    )
