from __future__ import annotations

import copy
import json
import os
import re
import shutil
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen

try:
    from .shared import DEFAULT_TEMPLATE_PATH, PROJECT_ROOT, _resolve_codex_command
except ImportError:
    from shared import DEFAULT_TEMPLATE_PATH, PROJECT_ROOT, _resolve_codex_command


@dataclass
class ContainerFieldItem:
    name: str
    kind: str = "variable"
    bit_width: int = 32
    rule_text: str = ""


@dataclass
class ContainerItem:
    name: str
    kind: str
    origin_name: str = ""
    node_name: str = ""
    scene_scope: str = ""
    count: int = 1
    stride: int = 4
    value: str = ""
    values: list[str] = field(default_factory=list)
    structure: list[str] = field(default_factory=list)
    layout_fields: list[ContainerFieldItem] = field(default_factory=list)
    view_offset: int = 0
    x: float = 0.0
    y: float = 0.0
    width: float = 0.0
    height: float = 0.0
    expand: bool = False
    locked: bool = False
    reuse_chain_id: str = ""
    reuse_chain_index: int = 0


@dataclass
class ContainerGroupItem:
    name: str
    variables: list[str] = field(default_factory=list)
    arrays: list[str] = field(default_factory=list)
    groups: list[str] = field(default_factory=list)
    scene_scope: str = ""
    x: float = 0.0
    y: float = 0.0
    width: float = 360.0
    height: float = 220.0
    expand: bool = True
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
    expand: bool = True


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
    expand: bool = True


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
    expand: bool = True


@dataclass
class FunctionFrameItem:
    name: str
    script: str = ""
    script_language: str = "pseudocode"
    input_name: str = "in"
    output_name: str = "out"
    expected_input: str = ""
    expected_output: str = ""
    x: float = 0.0
    y: float = 0.0
    width: float = 360.0
    height: float = 220.0
    expand: bool = True


@dataclass
class FunctionTextItem:
    name: str
    function_name: str = ""
    text: str = ""
    x: float = 0.0
    y: float = 0.0
    width: float = 360.0
    height: float = 200.0
    expand: bool = True


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
    expand: bool = True


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
    function_text_items: list[FunctionTextItem] = field(default_factory=list)
    connections: list[ConnectionItem] = field(default_factory=list)
    intervention_stages: list[InterventionStage] = field(default_factory=list)
    notes: str = ""
    manifest_text: str = ""

    def next_container_name(self, kind: str) -> str:
        prefix = "v" if kind == "variable" else "a"
        index = 1
        while True:
            candidate = f"{prefix}{index}"
            if all(container.name != candidate for container in self.containers):
                return candidate
            index += 1

    def next_resource_container_name(self, kind: str) -> str:
        prefix = "resV" if kind == "variable" else "resA"
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

    def next_container_reuse_chain_id(self) -> str:
        index = 1
        while True:
            candidate = f"containerReuse_{index}"
            if all(container.reuse_chain_id != candidate for container in self.containers):
                return candidate
            index += 1

    def next_resource_group_name(self) -> str:
        index = 1
        while True:
            candidate = f"resContainerElement_{index}"
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

    def next_function_text_name(self, function_name: str = "fun") -> str:
        base = f"{function_name}_text".strip() or "fun_text"
        if all(item.name != base for item in self.function_text_items):
            return base
        index = 1
        while True:
            candidate = f"{base}_{index}"
            if all(item.name != candidate for item in self.function_text_items):
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

    @staticmethod
    def _split_port_names(raw_value: str, default_name: str) -> list[str]:
        values = [part.strip() for part in str(raw_value or "").split(",") if part.strip()]
        return values or [default_name]

    @staticmethod
    def _expand_flag(value: Any, default: bool) -> bool:
        if value is None:
            return default
        if isinstance(value, bool):
            return value
        if isinstance(value, str):
            lowered = value.strip().lower()
            if lowered in {"true", "1"}:
                return True
            if lowered in {"false", "0"}:
                return False
        raise ValueError(f"expand must be a boolean, got {value!r}.")

    def _normalize_kind(self, kind: str) -> str:
        normalized = str(kind).strip()
        if not normalized:
            raise ValueError("Node kind is required.")
        lowered = normalized.replace("_", "").replace("-", "").lower()
        if lowered == "resnode":
            return "resnode"
        if lowered == "function":
            return "function"
        if lowered in {"functiontext", "funtext", "textnode", "text"}:
            return "functiontext"
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
        elif node_kind == "functiontext":
            for item in self.function_text_items:
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
        nodes.extend(("functiontext", item.name, item) for item in self.function_text_items)
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
            return self._split_port_names(node.input_name, "in")
        if node_kind == "functiontext":
            return []
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
            return self._split_port_names(node.output_name, "out")
        if node_kind == "functiontext":
            return []
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

    @staticmethod
    def _parse_layout_fields(raw_fields: Any, owner_name: str) -> list[ContainerFieldItem]:
        if raw_fields is None:
            return []
        if not isinstance(raw_fields, list):
            raise ValueError(f"{owner_name}.layoutFields must be a list.")
        parsed: list[ContainerFieldItem] = []
        for index, field_entry in enumerate(raw_fields, start=1):
            if not isinstance(field_entry, dict):
                raise ValueError(f"{owner_name}.layoutFields[{index}] must be an object.")
            field_name = str(field_entry.get("name") or "").strip() or f"field_{index}"
            field_kind = str(field_entry.get("kind") or "variable").strip().lower() or "variable"
            if field_kind not in {"variable", "array"}:
                raise ValueError(f"{owner_name}.layoutFields[{index}].kind must be variable or array.")
            bit_width = int(field_entry.get("bitWidth", 32))
            if bit_width <= 0:
                raise ValueError(f"{owner_name}.layoutFields[{index}].bitWidth must be positive.")
            rule_text = str(field_entry.get("ruleText") or field_entry.get("readAs") or "").strip()
            if not rule_text:
                raise ValueError(f"{owner_name}.layoutFields[{index}].ruleText is required.")
            parsed.append(
                ContainerFieldItem(
                    name=field_name,
                    kind=field_kind,
                    bit_width=bit_width,
                    rule_text=rule_text,
                )
            )
        return parsed

    def _layout_field_signature_from_container(self, container: ContainerItem) -> tuple[str, ...]:
        signature: list[str] = []
        for field_item in getattr(container, "layout_fields", []):
            if not isinstance(field_item, ContainerFieldItem):
                raise AssertionError(f"Container {container.name} has an invalid layout field entry.")
            normalized_kind = str(field_item.kind or "").strip().lower()
            if normalized_kind not in {"variable", "array"}:
                raise AssertionError(f"Container {container.name} has unsupported layout field kind: {field_item.kind}")
            bit_width = int(field_item.bit_width)
            if bit_width <= 0:
                raise AssertionError(f"Container {container.name} has a non-positive layout field width.")
            rule_text = str(field_item.rule_text or "").strip()
            if not rule_text:
                raise AssertionError(f"Container {container.name} has an empty layout field rule text.")
            signature.append(f"{normalized_kind}:{bit_width}:{rule_text}")
        return tuple(signature)

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

    def _structure_signature_from_container(self, container: ContainerItem) -> tuple[Any, ...]:
        layout_signature = self._layout_field_signature_from_container(container)
        if layout_signature:
            return (self._container_kind_code(container), layout_signature)
        normalized_structure = tuple(str(value).strip() for value in container.structure if str(value).strip())
        return (self._container_kind_code(container), normalized_structure)

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

    def _validate_container_structure_connection(self, connection: ConnectionItem) -> None:
        container_like_kinds = {"container", "containerelement"}
        if connection.source_kind not in container_like_kinds or connection.target_kind not in container_like_kinds:
            return
        if connection.source_kind != connection.target_kind:
            raise ValueError(
                f"Cannot connect {connection.source_kind}:{connection.source_name} to "
                f"{connection.target_kind}:{connection.target_name} directly."
            )
        if connection.source_kind == "container":
            source = self._resolve_node("container", connection.source_name)
            target = self._resolve_node("container", connection.target_name)
            if not isinstance(source, ContainerItem) or not isinstance(target, ContainerItem):
                raise AssertionError("Container connection validation resolved a non-container node.")
            if self._structure_signature_from_container(source) != self._structure_signature_from_container(target):
                raise ValueError(
                    f"Container structure mismatch: {source.name} cannot connect to {target.name}."
                )
            return
        source_group = self._resolve_node("containerelement", connection.source_name)
        target_group = self._resolve_node("containerelement", connection.target_name)
        if not isinstance(source_group, ContainerGroupItem) or not isinstance(target_group, ContainerGroupItem):
            raise AssertionError("containerElement connection validation resolved an invalid node.")
        if self._structure_signature_from_group(source_group) != self._structure_signature_from_group(target_group):
            raise ValueError(
                f"containerElement structure mismatch: {source_group.name} cannot connect to {target_group.name}."
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
        container_owner: dict[str, str] = {}
        group_owner: dict[str, str] = {}
        for group in self.container_groups:
            self.validate_container_group(group)
        for group in self.container_groups:
            dedup_variables: list[str] = []
            dedup_arrays: list[str] = []
            dedup_groups: list[str] = []
            for name in group.variables:
                if name in dedup_variables:
                    continue
                if name in container_owner:
                    raise ValueError(f"Container {name} belongs to both {container_owner[name]} and {group.name}.")
                container_owner[name] = group.name
                dedup_variables.append(name)
            for name in group.arrays:
                if name in dedup_arrays:
                    continue
                if name in container_owner:
                    raise ValueError(f"Container {name} belongs to both {container_owner[name]} and {group.name}.")
                container_owner[name] = group.name
                dedup_arrays.append(name)
            for name in group.groups:
                if name in dedup_groups:
                    continue
                if name in group_owner:
                    raise ValueError(f"containerElement {name} belongs to both {group_owner[name]} and {group.name}.")
                group_owner[name] = group.name
                dedup_groups.append(name)
            group.variables = dedup_variables
            group.arrays = dedup_arrays
            group.groups = dedup_groups

    def validate_connection(self, connection: ConnectionItem) -> None:
        self._validate_decomposer_connection(connection)
        self._validate_container_structure_connection(connection)
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
                "expand": bool(getattr(item, "expand", True)),
            }
            if kind == "container":
                entry["width"] = float(getattr(item, "width", 0.0))
                entry["height"] = float(getattr(item, "height", 0.0))
                entry["nodeName"] = str(getattr(item, "node_name", ""))
                entry["originName"] = str(getattr(item, "origin_name", "") or name)
                entry["sceneScope"] = str(getattr(item, "scene_scope", ""))
            elif kind == "containerelement":
                entry["width"] = float(getattr(item, "width", 360.0))
                entry["height"] = float(getattr(item, "height", 220.0))
                entry["sceneScope"] = str(getattr(item, "scene_scope", ""))
            elif kind in {"decomposer", "reflector", "resnode", "interventioner", "function", "functiontext"}:
                entry["width"] = float(getattr(item, "width", 360.0))
                entry["height"] = float(getattr(item, "height", 220.0))
                if kind == "function":
                    entry["script"] = str(getattr(item, "script", ""))
                    entry["script_language"] = str(getattr(item, "script_language", "pseudocode"))
                    entry["input_name"] = str(getattr(item, "input_name", "in"))
                    entry["output_name"] = str(getattr(item, "output_name", "out"))
                if kind == "functiontext":
                    entry["text"] = str(getattr(item, "text", ""))
                    entry["function_name"] = str(getattr(item, "function_name", ""))
            nodes.append(entry)
        return nodes

    def _container_group_section(self) -> dict[str, Any]:
        container_group_section: dict[str, Any] = {}
        for group in self.container_groups:
            container_group_section[group.name] = {
                "variables": list(group.variables),
                "arrays": list(group.arrays),
                "groups": list(group.groups),
                "sceneScope": group.scene_scope,
                "x": group.x,
                "y": group.y,
                "width": group.width,
                "height": group.height,
                "expand": group.expand,
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

    def has_explicit_layout(self) -> bool:
        for _kind, _name, item in self.iter_nodes():
            x = float(getattr(item, "x", 0.0))
            y = float(getattr(item, "y", 0.0))
            if abs(x) > 0.001 or abs(y) > 0.001:
                return True
        return False

    def apply_default_layout(self) -> None:
        x = 32.0
        y = 32.0
        row_height = 0.0
        max_width = 1400.0
        gap_x = 28.0
        gap_y = 28.0
        for _kind, _name, item in self.iter_nodes():
            width = float(getattr(item, "width", 180.0))
            height = float(getattr(item, "height", 96.0))
            if x + width > max_width and row_height > 0.0:
                x = 32.0
                y += row_height + gap_y
                row_height = 0.0
            item.x = x
            item.y = y
            x += width + gap_x
            row_height = max(row_height, height)

    @staticmethod
    def _parse_decomposer_direct_script(script: str) -> tuple[str, list[str], list[int]] | None:
        text = str(script or "").strip()
        if not text:
            return None
        match = re.fullmatch(
            r"from\s+([A-Za-z_][A-Za-z0-9_]*)\s+to\s+([A-Za-z0-9_,\s]+?)(?:\s+([0-9,\s]+))?",
            text,
            flags=re.IGNORECASE,
        )
        if not match:
            return None
        source = match.group(1).strip()
        targets = [item.strip() for item in match.group(2).split(",") if item.strip()]
        if not source or not targets:
            return None
        bit_widths: list[int] = []
        bit_text = (match.group(3) or "").strip()
        if bit_text:
            for token in bit_text.split(","):
                token_text = token.strip()
                if not token_text:
                    continue
                if not token_text.isdigit():
                    return None
                bit_widths.append(int(token_text))
            if bit_widths and len(bit_widths) != len(targets):
                return None
        return source, targets, bit_widths

    def to_package_json(self) -> dict[str, Any]:
        self.sync_container_groups_from_geometry()
        variable_section: dict[str, Any] = {}
        variable_array_section: dict[str, Any] = {}
        for container in self.containers:
            entry = {
                "nodeName": container.node_name,
                "originName": container.origin_name or container.name,
                "sceneScope": container.scene_scope,
                "count": container.count,
                "stride": container.stride,
                "value": container.value,
                "values": list(container.values),
                "structure": list(container.structure),
                "layoutFields": [
                    {
                        "name": field_item.name,
                        "kind": field_item.kind,
                        "bitWidth": field_item.bit_width,
                        "ruleText": field_item.rule_text,
                    }
                    for field_item in container.layout_fields
                ],
                "view_offset": container.view_offset,
                "x": container.x,
                "y": container.y,
                "width": container.width,
                "height": container.height,
                "expand": container.expand,
                "reuseChainId": container.reuse_chain_id,
                "reuseChainIndex": container.reuse_chain_index,
            }
            if container.kind == "variable":
                variable_section[container.name] = entry
            else:
                variable_array_section[container.name] = entry

        decomposer_description: list[dict[str, Any]] = []
        for rule in self.decomposer_rules:
            parsed_direct_rule = None
            if rule.map_kind != "filter" and rule.descriptor_script.strip():
                parsed_direct_rule = self._parse_decomposer_direct_script(rule.descriptor_script)
            from_names = [rule.source] if rule.source else []
            to_value: Any = [rule.target] if rule.target else []
            if parsed_direct_rule:
                parsed_source, parsed_targets, _parsed_bit_widths = parsed_direct_rule
                from_names = [parsed_source]
                to_value = parsed_targets
            entry: dict[str, Any] = {
                "name": rule.name,
                "from": from_names,
                "to": to_value,
                "mapKind": rule.map_kind,
                "x": rule.x,
                "y": rule.y,
                "width": rule.width,
                "height": rule.height,
                "expand": rule.expand,
            }
            if rule.descriptor_script:
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
                        "expand": item.expand,
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
                        "expand": item.expand,
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
                    "expand": item.expand,
                }
            )

        function_items: list[dict[str, Any]] = []
        for item in self.function_frames:
            function_items.append(
                {
                    "name": item.name,
                    "script": item.script,
                    "script_language": item.script_language,
                    "input_name": item.input_name,
                    "output_name": item.output_name,
                    "expected_input": item.expected_input,
                    "expected_output": item.expected_output,
                    "x": item.x,
                    "y": item.y,
                    "width": item.width,
                    "height": item.height,
                    "expand": item.expand,
                }
            )

        function_text_items: list[dict[str, Any]] = []
        for item in self.function_text_items:
            function_text_items.append(
                {
                    "name": item.name,
                    "function_name": item.function_name,
                    "text": item.text,
                    "x": item.x,
                    "y": item.y,
                    "width": item.width,
                    "height": item.height,
                    "expand": item.expand,
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
                "expand": stage.expand,
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
            "functionText": {
                "items": function_text_items,
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

    def rebuild_manifest_text(self) -> str:
        self.manifest_text = json.dumps(self.to_package_json(), indent=2, ensure_ascii=False)
        return self.manifest_text

    def current_manifest_text(self) -> str:
        if not self.manifest_text:
            return self.rebuild_manifest_text()
        return self.manifest_text

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
                structure = entry.get("structure", [])
                if not isinstance(structure, list):
                    structure = [structure] if structure else []
                project.containers.append(
                    ContainerItem(
                        name=str(name),
                        kind="variable",
                        node_name=str(entry.get("nodeName") or entry.get("aliasName") or ""),
                        origin_name=str(entry.get("originName") or name),
                        scene_scope=str(entry.get("sceneScope") or ""),
                        count=int(entry.get("count", 1)),
                        stride=int(entry.get("stride", 4)),
                        value=str(entry.get("value") or ""),
                        values=[str(value) for value in values],
                        structure=[str(value) for value in structure],
                        layout_fields=project._parse_layout_fields(entry.get("layoutFields", []), f"container.variable[{name}]"),
                        view_offset=int(entry.get("view_offset", 0)),
                        x=float(entry.get("x", 0.0)),
                        y=float(entry.get("y", 0.0)),
                        width=float(entry.get("width", 0.0)),
                        height=float(entry.get("height", 0.0)),
                        expand=cls._expand_flag(entry.get("expand"), False),
                        reuse_chain_id=str(entry.get("reuseChainId") or ""),
                        reuse_chain_index=int(entry.get("reuseChainIndex", 0)),
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
                structure = entry.get("structure", [])
                if not isinstance(structure, list):
                    structure = [structure] if structure else []
                project.containers.append(
                    ContainerItem(
                        name=str(name),
                        kind="array",
                        node_name=str(entry.get("nodeName") or entry.get("aliasName") or ""),
                        origin_name=str(entry.get("originName") or name),
                        scene_scope=str(entry.get("sceneScope") or ""),
                        count=int(entry.get("count", 1)),
                        stride=int(entry.get("stride", 4)),
                        value=str(entry.get("value") or ""),
                        values=[str(value) for value in values],
                        structure=[str(value) for value in structure],
                        layout_fields=project._parse_layout_fields(entry.get("layoutFields", []), f"container.variableArray[{name}]"),
                        view_offset=int(entry.get("view_offset", 0)),
                        x=float(entry.get("x", 0.0)),
                        y=float(entry.get("y", 0.0)),
                        width=float(entry.get("width", 0.0)),
                        height=float(entry.get("height", 0.0)),
                        expand=cls._expand_flag(entry.get("expand"), False),
                        reuse_chain_id=str(entry.get("reuseChainId") or ""),
                        reuse_chain_index=int(entry.get("reuseChainIndex", 0)),
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
                        scene_scope=str(entry.get("sceneScope") or ""),
                        x=float(entry.get("x", 0.0)),
                        y=float(entry.get("y", 0.0)),
                        width=float(entry.get("width", 360.0)),
                        height=float(entry.get("height", 220.0)),
                        expand=cls._expand_flag(entry.get("expand"), True),
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
                        expand=cls._expand_flag(rule.get("expand"), True),
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
                    expand=cls._expand_flag(item.get("expand"), True),
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
                        expand=cls._expand_flag(item.get("expand"), True),
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
                        script_language=str(item.get("script_language") or "pseudocode"),
                        input_name=str(item.get("input_name") or "in"),
                        output_name=str(item.get("output_name") or "out"),
                        expected_input=str(item.get("expected_input") or ""),
                        expected_output=str(item.get("expected_output") or ""),
                        x=float(item.get("x", 0.0)),
                        y=float(item.get("y", 0.0)),
                        width=float(item.get("width", 360.0)),
                        height=float(item.get("height", 220.0)),
                        expand=cls._expand_flag(item.get("expand"), True),
                    )
                )

        function_text_section = payload.get("functionText", {})
        function_text_items = function_text_section.get("items", [])
        if isinstance(function_text_items, list):
            for index, item in enumerate(function_text_items, start=1):
                project.function_text_items.append(
                    FunctionTextItem(
                        name=str(item.get("name") or f"fun_text_{index}"),
                        function_name=str(item.get("function_name") or ""),
                        text=str(item.get("text") or ""),
                        x=float(item.get("x", 0.0)),
                        y=float(item.get("y", 0.0)),
                        width=float(item.get("width", 360.0)),
                        height=float(item.get("height", 200.0)),
                        expand=cls._expand_flag(item.get("expand"), True),
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
                        expand=cls._expand_flag(stage.get("expand"), True),
                    )
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
                    if isinstance(item, ContainerItem):
                        item.width = float(entry.get("width", getattr(item, "width", 0.0)))
                        item.height = float(entry.get("height", getattr(item, "height", 0.0)))
                        item.node_name = str(entry.get("nodeName") or entry.get("aliasName") or getattr(item, "node_name", ""))
                        item.origin_name = str(entry.get("originName") or getattr(item, "origin_name", "") or item.name)
                        item.scene_scope = str(entry.get("sceneScope") or getattr(item, "scene_scope", ""))
                    if isinstance(item, ContainerGroupItem):
                        item.scene_scope = str(entry.get("sceneScope") or getattr(item, "scene_scope", ""))
                    if hasattr(item, "expand") and "expand" in entry:
                        item.expand = cls._expand_flag(entry.get("expand"), bool(getattr(item, "expand", True)))

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
        if not project.has_explicit_layout():
            project.apply_default_layout()
        project.rebuild_manifest_text()
        return project


class MockAgentClient:
    def __init__(self) -> None:
        self.provider = "codex"
        self.model = "gpt-5.4-mini"
        self.approval_mode = "manual"
        self.base_url = ""
        self.api_key = ""
        self.codex_command = "codex"
        self.timeout_seconds = 180

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
            error_text = " ".join(
                part.strip()
                for part in [f"{exc.code} {exc.reason}".strip(), error_body.splitlines()[0].strip() if error_body else ""]
                if part
            )
            raise RuntimeError(f"API request failed: {error_text or exc.reason}") from exc
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
        try:
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
        except subprocess.TimeoutExpired as exc:
            raise RuntimeError(f"Codex command timed out after {self.timeout_seconds} seconds.") from exc
        output = (completed.stdout or "").strip()
        if completed.returncode != 0:
            raise RuntimeError(f"Codex command failed (exit {completed.returncode}).") from None
        if output:
            return output
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
            "#include \"../algorithm_plugin_api.h\"",
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
