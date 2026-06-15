from __future__ import annotations

import copy
import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_TEMPLATE_PATH = PROJECT_ROOT / "src" / "capabilities" / "algorithm_library" / "algorithm_package_example.json"


@dataclass
class ContainerItem:
    name: str
    kind: str
    count: int = 1
    stride: int = 4
    x: float = 0.0
    y: float = 0.0
    locked: bool = False


@dataclass
class DecomposerRule:
    name: str
    source: str
    target: str


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


@dataclass
class ProjectState:
    algorithm_name: str = "new_algorithm"
    package_name: str = "new_algorithm"
    cpu_available: bool = True
    gpu_available: bool = True
    containers: list[ContainerItem] = field(default_factory=list)
    decomposer_rules: list[DecomposerRule] = field(default_factory=list)
    reflector_items: list[ReflectorItem] = field(default_factory=list)
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

    def next_stage_name(self) -> str:
        index = 1
        while True:
            candidate = f"stage_{index}"
            if all(stage.name != candidate for stage in self.intervention_stages):
                return candidate
            index += 1

    def next_reflector_name(self) -> str:
        index = 1
        while True:
            candidate = f"reflector_{index}"
            if all(item.name != candidate for item in self.reflector_items):
                return candidate
            index += 1

    def to_package_json(self) -> dict[str, Any]:
        variable_section: dict[str, Any] = {}
        variable_array_section: dict[str, Any] = {}
        for container in self.containers:
            entry = {
                "count": container.count,
                "stride": container.stride,
                "x": container.x,
                "y": container.y,
            }
            if container.kind == "variable":
                variable_section[container.name] = entry
            else:
                variable_array_section[container.name] = entry

        decomposer_description: list[dict[str, Any]] = []
        for rule in self.decomposer_rules:
            decomposer_description.append(
                {
                    "name": rule.name,
                    "from": [rule.source],
                    "to": [rule.target],
                }
            )

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
            "decomposer": {
                "res": {},
                "description": decomposer_description,
            },
            "reflector": {
                "name": f"{self.algorithm_name}_reflector",
                "functionName": "direct",
                "items": reflector_items,
            },
            "intervention": {
                "stages": stage_map,
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
                project.containers.append(
                    ContainerItem(
                        name=str(name),
                        kind="variable",
                        count=int(entry.get("count", 1)),
                        stride=int(entry.get("stride", 4)),
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
                project.containers.append(
                    ContainerItem(
                        name=str(name),
                        kind="array",
                        count=int(entry.get("count", 1)),
                        stride=int(entry.get("stride", 4)),
                        x=float(entry.get("x", 0.0)),
                        y=float(entry.get("y", 0.0)),
                    )
                )

        decomposer = payload.get("decomposer", {})
        for index, rule in enumerate(decomposer.get("description", []), start=1):
            sources = rule.get("from", [])
            targets = rule.get("to", [])
            source = str(sources[0]) if sources else ""
            target = str(targets[0]) if targets else ""
            if source and target:
                project.decomposer_rules.append(
                    DecomposerRule(
                        name=str(rule.get("name") or f"rule_{index}"),
                        source=source,
                        target=target,
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
                    )
                )

        if not project.containers:
            project.containers.append(ContainerItem(name="v1", kind="variable"))
            project.containers.append(ContainerItem(name="a1", kind="array"))
        if not project.intervention_stages:
            project.intervention_stages.extend(
                [
                    InterventionStage(name="preTick", kind="preTick"),
                    InterventionStage(name="afterTick", kind="afterTick"),
                    InterventionStage(name="resultRender", kind="resultRender"),
                ]
            )
        return project


class MockAgentClient:
    def __init__(self) -> None:
        self.provider = "mock"
        self.model = "mock"
        self.base_url = ""
        self.api_key = ""

    def generate(self, project: ProjectState, selection: str, prompt: str) -> str:
        summary = [
            f"provider={self.provider}",
            f"model={self.model}",
            f"selection={selection}",
            f"containers={len(project.containers)}",
            f"rules={len(project.decomposer_rules)}",
            f"reflectors={len(project.reflector_items)}",
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
