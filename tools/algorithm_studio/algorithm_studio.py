#!/usr/bin/env python3

from __future__ import annotations

import copy
import json
import shutil
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog, ttk
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

try:
    from .approval_rules import ApprovalDecision, ApprovalRuleSet, evaluate_access_rules, load_access_rules, resolve_access_rules_path
except ImportError:
    from approval_rules import ApprovalDecision, ApprovalRuleSet, evaluate_access_rules, load_access_rules, resolve_access_rules_path


PROJECT_ROOT = Path(__file__).resolve().parents[2]


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

NODE_WIDTH = 154
NODE_HEIGHT = 64
SPECIAL_CARD_WIDTH = 210
SPECIAL_CARD_HEIGHT = 108
BLUEPRINT_NODE_WIDTH = 360
BLUEPRINT_NODE_MIN_HEIGHT = 160
CANVAS_PADDING = 24
SIDEBAR_EXPANDED_WIDTH = 420
SIDEBAR_COLLAPSED_WIDTH = 36

COLORS = {
    "window": "#111418",
    "panel": "#161b22",
    "panel_alt": "#1b212b",
    "canvas": "#0f1319",
    "grid": "#202632",
    "text": "#edf2f7",
    "muted": "#8b98a7",
    "accent": "#6ee7ff",
    "accent_2": "#f59e0b",
    "good": "#34d399",
    "bad": "#f87171",
    "container": "#1d4ed8",
    "container_array": "#7c3aed",
    "stage": "#0f766e",
    "agent": "#b45309",
    "edge": "#9aa4b2",
    "preview": "#0b1020",
}


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

    def to_package_json(self) -> dict[str, Any]:
        variable_section: dict[str, Any] = {}
        variable_array_section: dict[str, Any] = {}
        for container in self.containers:
            entry = {
                "count": container.count,
                "stride": container.stride,
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
                    }
                )

        stage_map: dict[str, Any] = {}
        for stage in self.intervention_stages:
            stage_map[stage.name] = {
                "stage_name": stage.name,
                "stage_kind": stage.kind,
                "functions": stage.functions,
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
        self.provider = "codex"
        self.model = "gpt-5.4-mini"
        self.approval_mode = "manual"
        self.base_url = ""
        self.api_key = ""
        self.codex_command = "codex"
        self.timeout_seconds = 120

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
        if base_url.endswith("/chat/completions"):
            url = base_url
        elif base_url.endswith("/v1"):
            url = base_url + "/chat/completions"
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
        command = self.codex_command.strip() or "codex"
        if shutil.which(command) is None:
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


if __package__:
    from .backend import (
        ContainerGroupItem,
        ConnectionItem,
        ContainerItem,
        DecomposerRule,
        DEFAULT_TEMPLATE_PATH,
        FunctionFrameItem,
        InterventionStage,
        MockAgentClient,
        ProjectState,
        ResourceNodeItem,
        ReflectorItem,
        generate_cpp_skeleton,
    )
else:
    from backend import (
        ContainerGroupItem,
        ConnectionItem,
        ContainerItem,
        DecomposerRule,
        DEFAULT_TEMPLATE_PATH,
        InterventionStage,
        MockAgentClient,
        ProjectState,
        ResourceNodeItem,
        ReflectorItem,
        generate_cpp_skeleton,
    )


class AlgorithmStudioApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("Algorithm Studio")
        self.root.geometry("1640x980")
        self.root.configure(bg=COLORS["window"])

        self.project = ProjectState()
        self.agent_client = MockAgentClient()
        self.selected_container_name: str | None = None
        self.selected_rule_name: str | None = None
        self.selected_reflector_name: str | None = None
        self.selected_res_node_name: str | None = None
        self.selected_function_name: str | None = None
        self.selected_stage_name: str | None = None
        self.selected_container_group_name: str | None = None
        self.connection_drag_state: dict[str, Any] | None = None
        self.marquee_state: dict[str, Any] | None = None
        self.canvas_pan_state: dict[str, Any] | None = None
        self.container_group_drag_state: dict[str, Any] | None = None
        self.container_group_resize_state: dict[str, Any] | None = None
        self.toolnode_resize_state: dict[str, Any] | None = None
        self.canvas_nodes: dict[str, int] = {}
        self.canvas_container_group_nodes: dict[str, int] = {}
        self.canvas_port_positions: dict[str, tuple[float, float]] = {}
        self.canvas_connection_item_to_index: dict[int, int] = {}
        self.node_drag_state: dict[str, Any] | None = None
        self.palette_drag_state: dict[str, Any] | None = None
        self.canvas_item_to_name: dict[int, str] = {}
        self.log_lines: list[str] = []

        self.project_name_var = tk.StringVar(value=self.project.algorithm_name)
        self.package_name_var = tk.StringVar(value=self.project.package_name)
        self.cpu_var = tk.BooleanVar(value=self.project.cpu_available)
        self.gpu_var = tk.BooleanVar(value=self.project.gpu_available)
        self.container_kind_var = tk.StringVar(value="variable")
        self.container_name_var = tk.StringVar()
        self.container_count_var = tk.IntVar(value=1)
        self.container_stride_var = tk.IntVar(value=4)
        self.rule_name_var = tk.StringVar()
        self.rule_source_var = tk.StringVar()
        self.rule_target_var = tk.StringVar()
        self.reflector_name_var = tk.StringVar()
        self.reflector_fun_var = tk.StringVar(value="direct")
        self.reflector_inputs_varity_var = tk.StringVar()
        self.reflector_inputs_array_var = tk.StringVar()
        self.reflector_output_kind_var = tk.StringVar(value="v")
        self.reflector_output_name_var = tk.StringVar()
        self.reflector_direct_from_var = tk.StringVar()
        self.reflector_direct_to_var = tk.StringVar()
        self.stage_name_var = tk.StringVar()
        self.stage_kind_var = tk.StringVar(value="interventioner")
        self.stage_functions_var = tk.StringVar()
        self.stage_shader_vertex_var = tk.StringVar()
        self.stage_shader_fragment_var = tk.StringVar()
        self.stage_used_vars_var = tk.StringVar()
        self.stage_used_arrays_var = tk.StringVar()
        self.provider_var = tk.StringVar(value="codex")
        self.model_var = tk.StringVar(value="gpt-5.4-mini")
        self.approval_mode_var = tk.StringVar(value="manual")
        self.base_url_var = tk.StringVar()
        self.api_key_var = tk.StringVar()
        self.agent_command_var = tk.StringVar(value="codex")
        self.agent_prompt_var = tk.StringVar(value="Generate a container layout and package skeleton for the selected algorithm.")
        self.agent_output_var = tk.StringVar()
        self.sidebar_collapsed_var = tk.BooleanVar(value=False)
        self.agent_ready_var = tk.BooleanVar(value=False)
        self.chat_history: list[dict[str, str]] = []
        self.chat_busy = False
        self.status_var = tk.StringVar(value="Ready.")
        self.welcome_status_var = tk.StringVar(value="Choose how to connect to an agent.")
        self.preview_text: tk.Text | None = None
        self.log_text: tk.Text | None = None
        self.canvas: tk.Canvas | None = None
        self.container_list: tk.Listbox | None = None
        self.rule_list: tk.Listbox | None = None
        self.reflector_list: tk.Listbox | None = None
        self.stage_list: tk.Listbox | None = None
        self.inspector_text: tk.Text | None = None
        self.agent_text: tk.Text | None = None
        self.agent_output_box: tk.Text | None = None
        self.sidebar_shell: ttk.Frame | None = None
        self.sidebar_body: ttk.Frame | None = None
        self.approval_row: ttk.Frame | None = None
        self.chat_history_text: tk.Text | None = None
        self.chat_input_text: tk.Text | None = None
        self.chat_send_button: ttk.Button | None = None
        self.sidebar_toggle_button: ttk.Button | None = None
        self.welcome_frame: ttk.Frame | None = None
        self.welcome_result: str | None = None
        self.access_rules_path = resolve_access_rules_path()
        self.connection_probe_pending = False

        self._configure_style()
        self._build_ui()
        self._sync_project_to_vars()
        self._refresh_all()
        self._log("Algorithm Studio started.")

    def _configure_style(self) -> None:
        style = ttk.Style(self.root)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass

        style.configure("TFrame", background=COLORS["window"])
        style.configure("TLabel", background=COLORS["window"], foreground=COLORS["text"])
        style.configure("TButton", padding=(10, 6))
        style.configure("TLabelframe", background=COLORS["panel"], foreground=COLORS["text"])
        style.configure("TLabelframe.Label", background=COLORS["panel"], foreground=COLORS["accent"])
        style.configure("TNotebook", background=COLORS["window"], borderwidth=0)
        style.configure("TNotebook.Tab", padding=(12, 8))
        style.map("TNotebook.Tab", foreground=[("selected", COLORS["accent"]), ("!selected", COLORS["text"])])
        style.configure("Dark.Horizontal.TScale", background=COLORS["panel"])
        self.root.option_add("*Font", ("Segoe UI", 10))

    def _build_ui(self) -> None:
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(1, weight=1)

        self._build_toolbar()
        self._build_main_area()
        self._build_welcome_page()

    def _build_toolbar(self) -> None:
        toolbar = ttk.Frame(self.root, padding=(12, 10))
        toolbar.grid(row=0, column=0, sticky="ew")

        buttons = [
            ("New", self._new_project),
            ("Load Package", self._load_package),
            ("Save Package", self._save_package),
        ]
        for index, (label, command) in enumerate(buttons):
            ttk.Button(toolbar, text=label, command=command).grid(row=0, column=index, padx=(0, 8))

    def _build_main_area(self) -> None:
        main = ttk.Frame(self.root, padding=(12, 0, 12, 12))
        main.grid(row=1, column=0, sticky="nsew")
        main.columnconfigure(0, weight=0)
        main.columnconfigure(1, weight=1)
        main.columnconfigure(2, weight=0, minsize=SIDEBAR_EXPANDED_WIDTH)
        main.rowconfigure(0, weight=1)

        self._build_palette_panel(main)
        self._build_canvas_panel(main)
        self._build_sidebar_panel(main)

    def _build_welcome_page(self) -> None:
        overlay = ttk.Frame(self.root, padding=24)
        overlay.place(relx=0, rely=0, relwidth=1, relheight=1)
        overlay.lift()
        overlay.columnconfigure(0, weight=1)
        overlay.rowconfigure(0, weight=1)
        self.welcome_frame = overlay

        shell = ttk.Frame(overlay, padding=24)
        shell.place(relx=0.5, rely=0.5, anchor="center")
        shell.columnconfigure(0, weight=1)

        card = ttk.LabelFrame(shell, text="Welcome", padding=20)
        card.grid(row=0, column=0, sticky="nsew")
        card.columnconfigure(0, weight=1)

        ttk.Label(
            card,
            text="Choose one way to enter: connect to a local Codex agent or wire in your API once.",
            wraplength=440,
            foreground=COLORS["text"],
        ).grid(row=0, column=0, sticky="ew", pady=(0, 12))

        codex_ok = shutil.which(self.agent_command_var.get().strip() or "codex") is not None
        codex_label = "Local Codex detected" if codex_ok else "Local Codex not found"
        ttk.Label(card, text=codex_label, foreground=COLORS["muted"]).grid(row=1, column=0, sticky="ew", pady=(0, 8))

        buttons = ttk.Frame(card)
        buttons.grid(row=2, column=0, sticky="ew", pady=(8, 8))
        buttons.columnconfigure((0, 1), weight=1)

        api_button = ttk.Button(buttons, text="Connect API", command=self._welcome_connect_api)
        api_button.grid(row=0, column=0, sticky="ew", padx=(0, 8))
        codex_button = ttk.Button(buttons, text="Use Existing Agent", command=self._welcome_use_codex)
        codex_button.grid(row=0, column=1, sticky="ew")

        if not codex_ok:
            codex_button.configure(state="disabled")

        ttk.Label(card, textvariable=self.welcome_status_var, wraplength=440, foreground=COLORS["accent"]).grid(
            row=3, column=0, sticky="ew", pady=(8, 0)
        )

    def _destroy_welcome_page(self) -> None:
        if self.welcome_frame is not None:
            self.welcome_frame.destroy()
            self.welcome_frame = None

    def _finalize_welcome(self, provider: str) -> None:
        self.provider_var.set(provider)
        self.agent_ready_var.set(True)
        self._sync_agent_client_settings()
        self._destroy_welcome_page()
        self._append_chat_message("system", f"Connected using {provider}.")
        self._log(f"Connected agent source: {provider}.")
        self.connection_probe_pending = True
        self.root.after(250, self._run_connection_probe)

    def _welcome_use_codex(self) -> None:
        codex_command = self.agent_command_var.get().strip() or "codex"
        if shutil.which(codex_command) is None:
            messagebox.showerror("Codex missing", f"Cannot find local Codex command: {codex_command}")
            self.welcome_status_var.set("Local Codex is not available.")
            return
        self.agent_command_var.set(codex_command)
        self.welcome_status_var.set(f"Using local Codex: {codex_command}")
        self._finalize_welcome("codex")

    def _welcome_connect_api(self) -> None:
        base_url = simpledialog.askstring("API Base URL", "Enter the API base URL:")
        if not base_url:
            self.welcome_status_var.set("API connection cancelled.")
            return
        api_key = simpledialog.askstring("API Key", "Enter the API key:", show="*")
        if api_key is None:
            self.welcome_status_var.set("API connection cancelled.")
            return
        model = simpledialog.askstring("Model", "Enter the default model name:", initialvalue=self.model_var.get() or "gpt-5.4-mini")
        if model:
            self.model_var.set(model.strip() or self.model_var.get())
        self.base_url_var.set(base_url.strip())
        self.api_key_var.set(api_key.strip())
        self.welcome_status_var.set("API connection prepared.")
        self._finalize_welcome("api")

    def _welcome_import_existing_agent(self) -> None:
        self._welcome_use_codex()

    def _build_palette_panel(self, parent: ttk.Frame) -> None:
        palette = ttk.Frame(parent, padding=(0, 0, 12, 0))
        palette.grid(row=0, column=0, sticky="ns")
        palette.columnconfigure(0, weight=1)
        self.root.bind_all("<ButtonRelease-1>", self._finish_palette_drag, add="+")

        title = ttk.Label(palette, text="Drag Palette")
        title.grid(row=0, column=0, sticky="w", pady=(0, 8))

        hint = ttk.Label(palette, text="Drag blueprint nodes into the canvas", foreground=COLORS["muted"])
        hint.grid(row=1, column=0, sticky="w", pady=(0, 12))

        self._create_palette_group(
            palette,
            2,
            "ContainerElement",
            [
                ("variable", "v", "Variable"),
                ("array", "a", "Array"),
            ],
        )
        self._create_palette_group(
            palette,
            3,
            "ToolNodes",
            [
                ("containerelement", "C", "ContainerElement"),
                ("decomposer", "D", "Decomposer"),
                ("reflector", "R", "Reflector"),
                ("function", "ƒ", "Function"),
                ("interventioner", "I", "Interventioner"),
                ("resnode", "M", "resNode"),
            ],
        )

    def _create_palette_group(self, parent: ttk.Frame, row: int, title: str, items: list[tuple[str, str, str]]) -> None:
        group = ttk.LabelFrame(parent, text=title, padding=8)
        group.grid(row=row, column=0, sticky="ew", pady=(0, 12))
        group.columnconfigure(0, weight=1)

        for index, (kind, icon, label) in enumerate(items):
            tile = tk.Frame(group, bg=COLORS["panel_alt"], highlightbackground=COLORS["accent"], highlightthickness=1)
            tile.grid(row=index, column=0, sticky="ew", pady=(0, 8))
            tile.columnconfigure(1, weight=1)
            tile.kind = kind  # type: ignore[attr-defined]

            badge = tk.Label(tile, text=icon, bg=COLORS["panel_alt"], fg=COLORS["accent"], width=3, anchor="center")
            badge.grid(row=0, column=0, rowspan=2, padx=8, pady=8)

            title_label = tk.Label(tile, text=label, bg=COLORS["panel_alt"], fg=COLORS["text"], anchor="w")
            title_label.grid(row=0, column=1, sticky="ew", pady=(8, 0))

            sub_label = tk.Label(tile, text="drag to canvas", bg=COLORS["panel_alt"], fg=COLORS["muted"], anchor="w")
            sub_label.grid(row=1, column=1, sticky="ew", pady=(0, 8))

            for widget in (tile, badge, title_label, sub_label):
                widget.bind("<ButtonPress-1>", lambda event, value=kind: self._start_palette_drag(value, event))
                widget.bind("<B1-Motion>", self._palette_drag_motion)
                widget.bind("<ButtonRelease-1>", self._finish_palette_drag)

    def _start_palette_drag(self, kind: str, event: tk.Event) -> None:
        self.palette_drag_state = {
            "kind": kind,
            "x_root": event.x_root,
            "y_root": event.y_root,
        }

    def _palette_drag_motion(self, event: tk.Event) -> None:
        if not self.palette_drag_state:
            return
        self.palette_drag_state["x_root"] = event.x_root
        self.palette_drag_state["y_root"] = event.y_root

    def _finish_palette_drag(self, event: tk.Event) -> None:
        if not self.palette_drag_state:
            return
        kind = str(self.palette_drag_state["kind"])
        x_root = event.x_root
        y_root = event.y_root
        self.palette_drag_state = None
        if not self.canvas:
            return
        left = self.canvas.winfo_rootx()
        top = self.canvas.winfo_rooty()
        right = left + self.canvas.winfo_width()
        bottom = top + self.canvas.winfo_height()
        if x_root < left or x_root > right or y_root < top or y_root > bottom:
            return
        x = self.canvas.canvasx(x_root - left)
        y = self.canvas.canvasy(y_root - top)
        self._drop_palette_item(kind, x, y)

    def _drop_palette_item(self, kind: str, x: float, y: float) -> None:
        if kind == "containerelement":
            name = self.project.next_container_group_name()
            group = ContainerGroupItem(
                name=name,
                x=x,
                y=y,
                width=360.0,
                height=220.0,
            )
            self.project.validate_container_group(group)
            self.project.container_groups.append(group)
            self.selected_container_group_name = name
            self.selected_container_name = None
            self.selected_rule_name = None
            self.selected_reflector_name = None
            self.selected_stage_name = None
            self.selected_res_node_name = None
            self._refresh_all()
            self._log(f"Added containerElement {name}.")
            return
        if kind == "decomposer":
            name = self.project.next_decomposer_name()
            self.project.decomposer_rules.append(
                DecomposerRule(
                    name=name,
                    source="",
                    target="",
                    x=x,
                    y=y,
                )
            )
            self.selected_rule_name = name
            self.selected_container_name = None
            self.selected_reflector_name = None
            self.selected_stage_name = None
            self.selected_res_node_name = None
            self._refresh_all()
            self._log(f"Added decomposer {name}.")
            return
        if kind == "variable":
            name = self.project.next_container_name(kind)
            self.project.containers.append(
                ContainerItem(
                    name=name,
                    kind=kind,
                    x=x,
                    y=y,
                )
            )
            self.selected_container_name = name
            self.selected_reflector_name = None
            self.selected_stage_name = None
            self.selected_res_node_name = None
            self._refresh_all()
            self._log(f"Added container {name}.")
            return
        if kind == "array":
            name = self.project.next_container_name(kind)
            self.project.containers.append(
                ContainerItem(
                    name=name,
                    kind=kind,
                    stride=12,
                    x=x,
                    y=y,
                )
            )
            self.selected_container_name = name
            self.selected_reflector_name = None
            self.selected_stage_name = None
            self.selected_res_node_name = None
            self._refresh_all()
            self._log(f"Added container {name}.")
            return
        if kind == "reflector":
            name = self.project.next_reflector_name()
            self.project.reflector_items.append(
                ReflectorItem(
                    name=name,
                    x=x,
                    y=y,
                )
            )
            self.selected_reflector_name = name
            self.selected_container_name = None
            self.selected_stage_name = None
            self.selected_res_node_name = None
            self._refresh_all()
            self._log(f"Added reflector {name}.")
            return
        if kind == "interventioner" or kind == "stage":
            name = self.project.next_intervention_name()
            self.project.intervention_stages.append(
                InterventionStage(
                    name=name,
                    kind="interventioner",
                    x=x,
                    y=y,
                )
            )
            self.selected_stage_name = name
            self.selected_container_name = None
            self.selected_reflector_name = None
            self.selected_res_node_name = None
            self._refresh_all()
            self._log(f"Added interventioner {name}.")
            return
        if kind == "resnode":
            name = self.project.next_res_name()
            self.project.res_nodes.append(
                ResourceNodeItem(
                    name=name,
                    resource_types=["mesh", "obj", "gltf"],
                    outputs=["mesh", "obj", "gltf"],
                    resource_kind="mesh",
                    x=x,
                    y=y,
                )
            )
            self.selected_res_node_name = name
            self.selected_container_name = None
            self.selected_rule_name = None
            self.selected_reflector_name = None
            self.selected_stage_name = None
            self._refresh_all()
            self._log(f"Added resNode {name}.")
            return
        if kind == "function":
            name = self.project.next_function_name()
            self.project.function_frames.append(
                FunctionFrameItem(
                    name=name,
                    script="script",
                    input_name="in",
                    output_name="out",
                    x=x,
                    y=y,
                )
            )
            self.selected_function_name = name
            self.selected_container_name = None
            self.selected_rule_name = None
            self.selected_reflector_name = None
            self.selected_res_node_name = None
            self.selected_stage_name = None
            self._refresh_all()
            self._log(f"Added function {name}.")
            return

    def _build_canvas_panel(self, parent: ttk.Frame) -> None:
        frame = ttk.Frame(parent)
        frame.grid(row=0, column=1, sticky="nsew")
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(0, weight=1)

        canvas_frame = ttk.LabelFrame(frame, text="Scene", padding=8)
        canvas_frame.grid(row=0, column=0, sticky="nsew")
        canvas_frame.rowconfigure(0, weight=1)
        canvas_frame.columnconfigure(0, weight=1)

        self.canvas = tk.Canvas(
            canvas_frame,
            bg=COLORS["canvas"],
            highlightthickness=0,
            width=1080,
            height=820,
            relief="flat",
        )
        self.canvas.grid(row=0, column=0, sticky="nsew")
        self.canvas.bind("<Button-1>", self._on_canvas_click)
        self.canvas.bind("<ButtonPress-3>", self._on_canvas_right_press)
        self.canvas.bind("<B3-Motion>", self._on_canvas_right_drag)
        self.canvas.bind("<ButtonRelease-3>", self._on_canvas_right_release)
        self.canvas.bind("<B1-Motion>", self._on_canvas_drag)
        self.canvas.bind("<ButtonRelease-1>", self._on_canvas_release)
        self.canvas.bind("<Configure>", lambda _event: self._redraw_canvas())

    def _build_sidebar_panel(self, parent: ttk.Frame) -> None:
        shell = ttk.Frame(parent, width=SIDEBAR_EXPANDED_WIDTH, padding=(0, 0, 0, 0))
        shell.grid(row=0, column=2, sticky="ns")
        shell.grid_propagate(False)
        shell.columnconfigure(0, weight=1)
        shell.rowconfigure(1, weight=1)
        self.sidebar_shell = shell

        header = ttk.Frame(shell, padding=(0, 0, 0, 8))
        header.grid(row=0, column=0, sticky="ew")
        header.columnconfigure(0, weight=1)

        title_row = ttk.Frame(header)
        title_row.grid(row=0, column=0, sticky="ew")
        title_row.columnconfigure(0, weight=1)

        ttk.Label(title_row, text="ChatBox").grid(row=0, column=0, sticky="w")
        self.sidebar_toggle_button = ttk.Button(title_row, text="⟩", width=3, command=self._toggle_sidebar)
        self.sidebar_toggle_button.grid(row=0, column=1, sticky="e")

        hint = ttk.Label(header, text="Connect API or local Codex, then chat from the selected scene context.", foreground=COLORS["muted"], wraplength=SIDEBAR_EXPANDED_WIDTH - 24)
        hint.grid(row=1, column=0, sticky="ew", pady=(4, 0))

        body = ttk.Frame(shell)
        body.grid(row=1, column=0, sticky="nsew")
        body.columnconfigure(0, weight=1)
        body.rowconfigure(3, weight=1)
        self.sidebar_body = body

        source_row = ttk.LabelFrame(body, text="Source", padding=8)
        source_row.grid(row=0, column=0, sticky="ew")
        source_row.columnconfigure(1, weight=1)
        ttk.Label(source_row, text="Current").grid(row=0, column=0, sticky="w", padx=(0, 8))
        source_picker = ttk.Combobox(source_row, textvariable=self.provider_var, values=("codex", "api", "mock"), state="readonly")
        source_picker.grid(row=0, column=1, sticky="ew")
        source_picker.bind("<<ComboboxSelected>>", self._on_provider_selection_changed)

        approval_row = ttk.LabelFrame(body, text="Approval", padding=8)
        approval_row.grid(row=1, column=0, sticky="ew", pady=(8, 0))
        approval_row.columnconfigure(1, weight=1)
        self.approval_row = approval_row
        ttk.Label(approval_row, text="Mode").grid(row=0, column=0, sticky="w", padx=(0, 8))
        approval_picker = ttk.Combobox(approval_row, textvariable=self.approval_mode_var, values=("manual", "rules"), state="readonly")
        approval_picker.grid(row=0, column=1, sticky="ew")
        approval_picker.bind("<<ComboboxSelected>>", self._on_approval_mode_selection_changed)
        ttk.Label(
            approval_row,
            text=f"Rules file: {self.access_rules_path}",
            foreground=COLORS["muted"],
            wraplength=SIDEBAR_EXPANDED_WIDTH - 32,
        ).grid(row=1, column=0, columnspan=2, sticky="ew", pady=(8, 0))

        action_row = ttk.Frame(body, padding=(0, 8, 0, 8))
        action_row.grid(row=2, column=0, sticky="ew")
        for column in range(3):
            action_row.columnconfigure(column, weight=1)
        ttk.Button(action_row, text="Use Selection", command=self._fill_chat_prompt_from_selection).grid(row=0, column=0, sticky="ew", padx=(0, 6))
        self.chat_send_button = ttk.Button(action_row, text="Send", command=self._send_chat_message)
        self.chat_send_button.grid(row=0, column=1, sticky="ew", padx=(0, 6))
        ttk.Button(action_row, text="Clear", command=self._clear_chat_history).grid(row=0, column=2, sticky="ew")

        history_frame = ttk.LabelFrame(body, text="Conversation", padding=8)
        history_frame.grid(row=3, column=0, sticky="nsew")
        history_frame.columnconfigure(0, weight=1)
        history_frame.rowconfigure(0, weight=1)
        history_scroll = ttk.Scrollbar(history_frame, orient="vertical")
        history_scroll.grid(row=0, column=1, sticky="ns")
        self.chat_history_text = tk.Text(
            history_frame,
            wrap="word",
            height=16,
            bg=COLORS["canvas"],
            fg=COLORS["text"],
            insertbackground=COLORS["text"],
            relief="flat",
            borderwidth=0,
            highlightthickness=1,
            highlightbackground=COLORS["grid"],
            highlightcolor=COLORS["accent"],
            yscrollcommand=history_scroll.set,
        )
        self.chat_history_text.grid(row=0, column=0, sticky="nsew")
        history_scroll.config(command=self.chat_history_text.yview)
        self.chat_history_text.tag_configure("user", foreground=COLORS["accent"])
        self.chat_history_text.tag_configure("assistant", foreground=COLORS["good"])
        self.chat_history_text.tag_configure("system", foreground=COLORS["muted"])
        self.chat_history_text.tag_configure("error", foreground=COLORS["bad"])
        self.chat_history_text.configure(state="disabled")

        input_frame = ttk.LabelFrame(body, text="Prompt", padding=8)
        input_frame.grid(row=4, column=0, sticky="ew", pady=(8, 0))
        input_frame.columnconfigure(0, weight=1)
        input_scroll = ttk.Scrollbar(input_frame, orient="vertical")
        input_scroll.grid(row=0, column=1, sticky="ns")
        self.chat_input_text = tk.Text(
            input_frame,
            wrap="word",
            height=6,
            bg=COLORS["panel_alt"],
            fg=COLORS["text"],
            insertbackground=COLORS["text"],
            relief="flat",
            borderwidth=0,
            highlightthickness=1,
            highlightbackground=COLORS["grid"],
            highlightcolor=COLORS["accent"],
            yscrollcommand=input_scroll.set,
        )
        self.chat_input_text.grid(row=0, column=0, sticky="ew")
        input_scroll.config(command=self.chat_input_text.yview)
        self.chat_input_text.bind("<Return>", self._send_chat_message_from_event)
        self.chat_input_text.bind("<Shift-Return>", self._insert_chat_newline_from_event)
        self._append_chat_message(
            "system",
            f"ChatBox ready. Pick Codex or API, then ask a question. Approval mode: {self.approval_mode_var.get().strip() or 'manual'}.",
        )
        self.agent_text = self.chat_input_text
        self.agent_output_box = self.chat_history_text

        model_row = ttk.LabelFrame(body, text="Model", padding=8)
        model_row.grid(row=5, column=0, sticky="ew", pady=(8, 0))
        model_row.columnconfigure(1, weight=1)
        ttk.Label(model_row, text="Current").grid(row=0, column=0, sticky="w", padx=(0, 8))
        model_picker = ttk.Combobox(model_row, textvariable=self.model_var, values=("gpt-5.2", "gpt-5.4-mini", "mock"), state="readonly")
        model_picker.grid(row=0, column=1, sticky="ew")
        model_picker.bind("<<ComboboxSelected>>", self._on_model_selection_changed)
        self._apply_sidebar_layout()

    def _toggle_sidebar(self) -> None:
        self.sidebar_collapsed_var.set(not self.sidebar_collapsed_var.get())
        self._apply_sidebar_layout()

    def _apply_sidebar_layout(self) -> None:
        if not self.sidebar_shell or not self.sidebar_body or not self.sidebar_toggle_button:
            return
        collapsed = self.sidebar_collapsed_var.get()
        width = SIDEBAR_COLLAPSED_WIDTH if collapsed else SIDEBAR_EXPANDED_WIDTH
        self.sidebar_shell.configure(width=width)
        parent = self.sidebar_shell.master
        if hasattr(parent, "grid_columnconfigure"):
            parent.grid_columnconfigure(2, minsize=width)
        if collapsed:
            self.sidebar_body.grid_remove()
            self.sidebar_toggle_button.configure(text="⟨")
        else:
            if not self.sidebar_body.winfo_ismapped():
                self.sidebar_body.grid()
            self.sidebar_toggle_button.configure(text="⟩")

    def _on_provider_selection_changed(self, _event: tk.Event | None = None) -> None:
        provider = self.provider_var.get().strip().lower()
        previous = self.agent_client.provider
        if provider == previous:
            return
        if provider == "codex":
            command = self.agent_command_var.get().strip() or "codex"
            if shutil.which(command) is None:
                messagebox.showerror("Codex missing", f"Cannot find local Codex command: {command}")
                self.provider_var.set(previous)
                return
            self.agent_command_var.set(command)
        elif provider == "api":
            if not self.base_url_var.get().strip():
                base_url = simpledialog.askstring("API Base URL", "Enter the API base URL:")
                if not base_url:
                    self.provider_var.set(previous)
                    return
                self.base_url_var.set(base_url.strip())
            if not self.api_key_var.get().strip():
                api_key = simpledialog.askstring("API Key", "Enter the API key:", show="*")
                if api_key is None:
                    self.provider_var.set(previous)
                    return
                self.api_key_var.set(api_key.strip())
        self._sync_agent_client_settings()
        self._log(f"Switched agent source to {provider}.")

    def _on_model_selection_changed(self, _event: tk.Event | None = None) -> None:
        self._sync_agent_client_settings()
        self._log(f"Switched model to {self.model_var.get().strip() or 'gpt-5.4-mini'}.")

    def _on_approval_mode_selection_changed(self, _event: tk.Event | None = None) -> None:
        self._sync_agent_client_settings()
        mode = self.approval_mode_var.get().strip() or "manual"
        self._log(f"Switched approval mode to {mode}.")

    def _sync_project_to_vars(self) -> None:
        self.project_name_var.set(self.project.algorithm_name)
        self.package_name_var.set(self.project.package_name)
        self.cpu_var.set(self.project.cpu_available)
        self.gpu_var.set(self.project.gpu_available)

    def _apply_project_vars(self) -> None:
        self.project.algorithm_name = self.project_name_var.get().strip() or "new_algorithm"
        self.project.package_name = self.package_name_var.get().strip() or self.project.algorithm_name
        self.project.cpu_available = self.cpu_var.get()
        self.project.gpu_available = self.gpu_var.get()
        self._refresh_all()

    def _reset_scene(self) -> None:
        self.project = ProjectState()
        self._sync_project_to_vars()
        self._reset_chat_state()
        self.selected_container_name = None
        self.selected_rule_name = None
        self.selected_reflector_name = None
        self.selected_res_node_name = None
        self.selected_function_name = None
        self.selected_stage_name = None
        self.selected_container_group_name = None
        self.connection_drag_state = None
        self.marquee_state = None
        self.canvas_pan_state = None
        self.container_group_drag_state = None
        self.container_group_resize_state = None
        self.toolnode_resize_state = None
        self._refresh_all()
        self._log("Scene reset.")

    def _new_project(self) -> None:
        self._reset_scene()
        self.project.algorithm_name = "new_algorithm"
        self.project.package_name = "new_algorithm"
        self._sync_project_to_vars()

    def _load_default_template(self) -> None:
        if not DEFAULT_TEMPLATE_PATH.exists():
            messagebox.showerror("Template missing", str(DEFAULT_TEMPLATE_PATH))
            return
        payload = json.loads(DEFAULT_TEMPLATE_PATH.read_text(encoding="utf-8"))
        self.project = ProjectState.from_package_json(payload)
        self._reset_chat_state()
        self.connection_drag_state = None
        self.selected_container_group_name = None
        self.selected_function_name = None
        self.marquee_state = None
        self.canvas_pan_state = None
        self.container_group_drag_state = None
        self.container_group_resize_state = None
        self.toolnode_resize_state = None
        self._sync_project_to_vars()
        self._refresh_all()
        self._log(f"Loaded template {DEFAULT_TEMPLATE_PATH}.")

    def _load_package(self) -> None:
        path = filedialog.askopenfilename(
            title="Load algorithm package JSON",
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
        )
        if not path:
            return
        try:
            payload = json.loads(Path(path).read_text(encoding="utf-8"))
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("Load failed", f"{path}\n\n{exc}")
            return
        self.project = ProjectState.from_package_json(payload)
        self._reset_chat_state()
        self.connection_drag_state = None
        self.selected_container_group_name = None
        self.selected_function_name = None
        self.marquee_state = None
        self.canvas_pan_state = None
        self.container_group_drag_state = None
        self.container_group_resize_state = None
        self.toolnode_resize_state = None
        self._sync_project_to_vars()
        self._refresh_all()
        self._log(f"Loaded package {path}.")

    def _save_package(self) -> None:
        path = filedialog.asksaveasfilename(
            title="Save algorithm package JSON",
            defaultextension=".json",
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
        )
        if not path:
            return
        self._apply_project_vars()
        payload = self.project.to_package_json()
        Path(path).write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
        self._log(f"Saved package JSON to {path}.")
        self.status_var.set(f"Saved {Path(path).name}.")

    def _export_cpp_stub(self) -> None:
        path = filedialog.asksaveasfilename(
            title="Save C++ skeleton",
            defaultextension=".cpp",
            filetypes=[("C++ files", "*.cpp"), ("All files", "*.*")],
        )
        if not path:
            return
        self._apply_project_vars()
        cpp = self._generate_cpp_skeleton()
        Path(path).write_text(cpp, encoding="utf-8")
        self._log(f"Exported C++ skeleton to {path}.")
        self.status_var.set(f"Exported {Path(path).name}.")

    def _hot_build_stub(self) -> None:
        self._apply_project_vars()
        self._log("Hot build is not wired yet. This button is reserved for the later DLL + SPV build step.")
        messagebox.showinfo("Hot Build", "The build hook is reserved for the later DLL + SPV integration.")

    def _reset_chat_state(self) -> None:
        self.chat_busy = False
        self.chat_history = []
        if self.chat_history_text:
            self.chat_history_text.configure(state="normal")
            self.chat_history_text.delete("1.0", tk.END)
            self.chat_history_text.configure(state="disabled")
        if self.chat_input_text:
            self.chat_input_text.delete("1.0", tk.END)
        self._append_chat_message(
            "system",
            f"ChatBox ready. Pick Codex or API, then ask a question. Approval mode: {self.approval_mode_var.get().strip() or 'manual'}.",
        )

    def _generate_cpp_skeleton(self) -> str:
        return generate_cpp_skeleton(self.project.algorithm_name)

    def _refresh_all(self) -> None:
        self._sync_all_container_groups()
        self._refresh_container_list()
        self._refresh_rule_list()
        self._refresh_reflector_list()
        self._refresh_stage_list()
        self._refresh_preview()
        self._redraw_canvas()
        self._refresh_inspector()

    def _refresh_container_list(self) -> None:
        if not self.container_list:
            return
        self.container_list.delete(0, tk.END)
        for container in self.project.containers:
            kind = "v" if container.kind == "variable" else "a"
            self.container_list.insert(tk.END, f"{kind}:{container.name}  count={container.count} stride={container.stride}")

    def _refresh_rule_list(self) -> None:
        if not self.rule_list:
            return
        self.rule_list.delete(0, tk.END)
        for rule in self.project.decomposer_rules:
            source = rule.source or "-"
            target = rule.target or "-"
            mode = rule.map_kind or "v2v"
            resource_mode = rule.resource_mode or "default"
            self.rule_list.insert(tk.END, f"{rule.name}: {source} -> {target} [{mode} | {resource_mode}]")

    def _refresh_reflector_list(self) -> None:
        if self.reflector_list is None:
            return
        self.reflector_list.delete(0, tk.END)
        for item in self.project.reflector_items:
            if item.reflect_fun == "direct":
                descriptor = f"{item.name}: direct"
            else:
                descriptor = f"{item.name}: {item.reflect_fun}"
            self.reflector_list.insert(tk.END, descriptor)

    def _refresh_stage_list(self) -> None:
        if not self.stage_list:
            return
        self.stage_list.delete(0, tk.END)
        for stage in self.project.intervention_stages:
            self.stage_list.insert(tk.END, f"{stage.name} (interventioner)")

    def _refresh_preview(self) -> None:
        if not self.preview_text:
            return
        preview = json.dumps(self.project.to_package_json(), indent=2, ensure_ascii=False)
        self.preview_text.delete("1.0", tk.END)
        self.preview_text.insert("1.0", preview)

    def _refresh_inspector(self) -> None:
        if not self.inspector_text:
            return
        self.inspector_text.delete("1.0", tk.END)
        content = self._selected_item_summary()
        self.inspector_text.insert("1.0", content)

    def _selected_item_summary(self) -> str:
        if self.selected_container_group_name:
            group = self._find_container_group(self.selected_container_group_name)
            if group:
                return self._container_group_detail_text(group.name)
        if self.selected_container_name:
            container = self._find_container(self.selected_container_name)
            if container:
                return "\n".join(
                    [
                        "Selected container",
                        f"name: {container.name}",
                        f"kind: {container.kind}",
                        f"count: {container.count}",
                        f"stride: {container.stride}",
                        f"canvas: ({int(container.x)}, {int(container.y)})",
                        "",
                        "Actions:",
                        "- drag to move on canvas",
                        "- right click to duplicate or delete",
                    ]
                )
        if self.selected_rule_name:
            rule = self._find_rule(self.selected_rule_name)
            if rule:
                schema_lines = self._schema_detail_lines(self.project.decomposer_res)
                return "\n".join(
                    [
                        "Selected decomposer",
                        f"name: {rule.name}",
                        f"size: {int(getattr(rule, 'width', 0))} x {int(getattr(rule, 'height', 0))}",
                        f"source: {rule.source or '-'}",
                        f"target: {rule.target or '-'}",
                        f"mapKind: {rule.map_kind or 'v2v'}",
                        f"descriptorScript: {rule.descriptor_script or '-'}",
                        f"resourceMode: {rule.resource_mode or 'default'}",
                        f"resourceScript: {rule.resource_script or '-'}",
                        "resource schema:",
                        *schema_lines,
                    ]
                )
        if self.selected_reflector_name:
            item = self._find_reflector(self.selected_reflector_name)
            if item:
                return "\n".join(
                    [
                        "Selected reflector",
                        f"name: {item.name}",
                        f"size: {int(getattr(item, 'width', 0))} x {int(getattr(item, 'height', 0))}",
                        f"filter: {item.reflect_fun}",
                        f"进(varity): {', '.join(item.inputs_varity) or '-'}",
                        f"进(array): {', '.join(item.inputs_array) or '-'}",
                        f"出: {item.output_kind}:{item.output_name or '-'}",
                    ]
                )
        if self.selected_stage_name:
            stage = self._find_stage(self.selected_stage_name)
            if stage:
                return "\n".join(
                    [
                        "Selected interventioner",
                        f"name: {stage.name}",
                        f"size: {int(getattr(stage, 'width', 0))} x {int(getattr(stage, 'height', 0))}",
                        f"kind: {stage.kind}",
                        f"脚本: {', '.join(stage.functions) or '-'}",
                        f"进(variables): {', '.join(stage.used_variables) or '-'}",
                        f"进(arrays): {', '.join(stage.used_arrays) or '-'}",
                        f"shader.vertex: {stage.shader_vertex or '-'}",
                        f"shader.fragment: {stage.shader_fragment or '-'}",
                    ]
                )
        if self.selected_res_node_name:
            res_node = self._find_res_node(self.selected_res_node_name)
            if res_node:
                resource_types = ", ".join(res_node.resource_types) or "-"
                return "\n".join(
                    [
                        "Selected resNode",
                        f"name: {res_node.name}",
                        f"size: {int(getattr(res_node, 'width', 0))} x {int(getattr(res_node, 'height', 0))}",
                        f"resourceTypes: {resource_types}",
                        f"outputs: {', '.join(res_node.outputs) or '-'}",
                        f"primary: {res_node.resource_kind}",
                        f"canvas: ({int(res_node.x)}, {int(res_node.y)})",
                    ]
                )
        if self.selected_function_name:
            item = self._find_function_frame(self.selected_function_name)
            if item:
                return "\n".join(
                    [
                        "Selected function",
                        f"name: {item.name}",
                        f"size: {int(getattr(item, 'width', 0))} x {int(getattr(item, 'height', 0))}",
                        f"input: {item.input_name or 'in'}",
                        f"output: {item.output_name or 'out'}",
                        f"script: {item.script or '-'}",
                    ]
                )
        return "\n".join(
            [
                "Nothing selected",
                "",
                "Use the drag palette on the left.",
                "The canvas will render your scene and let you drag nodes.",
            ]
        )

    def _find_container(self, name: str) -> ContainerItem | None:
        for container in self.project.containers:
            if container.name == name:
                return container
        return None

    def _find_rule(self, name: str) -> DecomposerRule | None:
        for rule in self.project.decomposer_rules:
            if rule.name == name:
                return rule
        return None

    def _find_reflector(self, name: str) -> ReflectorItem | None:
        for item in self.project.reflector_items:
            if item.name == name:
                return item
        return None

    def _find_res_node(self, name: str) -> ResourceNodeItem | None:
        for item in self.project.res_nodes:
            if item.name == name:
                return item
        return None

    def _find_function_frame(self, name: str) -> Any | None:
        for item in self.project.function_frames:
            if item.name == name:
                return item
        return None

    def _find_stage(self, name: str) -> InterventionStage | None:
        for stage in self.project.intervention_stages:
            if stage.name == name:
                return stage
        return None

    def _find_container_group(self, name: str) -> ContainerGroupItem | None:
        for group in self.project.container_groups:
            if group.name == name:
                return group
        return None

    def _node_by_kind_name(self, kind: str, name: str) -> Any | None:
        if kind == "decomposer":
            return self._find_rule(name)
        if kind == "reflector":
            return self._find_reflector(name)
        if kind == "resnode":
            return self._find_res_node(name)
        if kind == "function":
            return self._find_function_frame(name)
        if kind == "interventioner" or kind == "stage":
            return self._find_stage(name)
        if kind == "containerelement":
            return self._find_container_group(name)
        if kind == "container":
            return self._find_container(name)
        return None

    def _container_center(self, container: ContainerItem) -> tuple[float, float]:
        return container.x + NODE_WIDTH / 2, container.y + NODE_HEIGHT / 2

    def _group_bounds(self, group: ContainerGroupItem) -> tuple[float, float, float, float]:
        return group.x, group.y, group.x + group.width, group.y + group.height

    def _container_rect(self, container: ContainerItem) -> tuple[float, float, float, float]:
        return container.x, container.y, container.x + NODE_WIDTH, container.y + NODE_HEIGHT

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
        left, top, right, bottom = self._group_bounds(group)
        variables: list[str] = []
        arrays: list[str] = []
        for container in self.project.containers:
            c_left, c_top, c_right, c_bottom = self._container_rect(container)
            if self._rects_intersect(left, top, right, bottom, c_left, c_top, c_right, c_bottom):
                if container.kind == "variable":
                    variables.append(container.name)
                elif container.kind == "array":
                    arrays.append(container.name)
                else:
                    raise AssertionError(f"Unsupported container kind in group membership: {container.kind}")
        return variables, arrays

    def _group_child_groups(self, group: ContainerGroupItem) -> list[str]:
        left, top, right, bottom = self._group_bounds(group)
        child_groups: list[str] = []
        for candidate in self.project.container_groups:
            if candidate.name == group.name:
                continue
            c_left, c_top, c_right, c_bottom = self._group_bounds(candidate)
            if self.project._rect_contains_rect(left, top, right, bottom, c_left, c_top, c_right, c_bottom):
                child_groups.append(candidate.name)
        return child_groups

    def _container_parent_group_name(self, name: str) -> str | None:
        container = self._find_container(name)
        if not container:
            return None
        container_rect = self._container_rect(container)
        parent_name: str | None = None
        parent_area = float("inf")
        for group in self.project.container_groups:
            group_rect = self._group_bounds(group)
            if not self.project._rect_contains_rect(*group_rect, *container_rect):
                continue
            area = group.width * group.height
            if area < parent_area:
                parent_area = area
                parent_name = group.name
        return parent_name

    def _group_parent_group_name(self, name: str) -> str | None:
        group = self._find_container_group(name)
        if not group:
            return None
        child_rect = self._group_bounds(group)
        parent_name: str | None = None
        parent_area = float("inf")
        for candidate in self.project.container_groups:
            if candidate.name == group.name:
                continue
            candidate_rect = self._group_bounds(candidate)
            if not self.project._rect_contains_rect(*candidate_rect, *child_rect):
                continue
            area = candidate.width * candidate.height
            if area < parent_area:
                parent_area = area
                parent_name = candidate.name
        return parent_name

    def _sync_container_group_membership(self, group: ContainerGroupItem) -> None:
        variables, arrays = self._container_group_members(group)
        group.variables = variables
        group.arrays = arrays

    def _sync_all_container_groups(self) -> None:
        self.project.sync_container_groups_from_geometry()

    def _create_container_group_from_selection(self, name: str, x: float, y: float, width: float, height: float) -> ContainerGroupItem:
        group = ContainerGroupItem(name=name, x=x, y=y, width=width, height=height)
        self._sync_container_group_membership(group)
        self.project.validate_container_group(group)
        self.project.container_groups.append(group)
        return group

    def _add_container(self, kind: str) -> None:
        name = self.project.next_container_name(kind)
        count = 1
        stride = 4 if kind == "variable" else 16
        if kind == "array":
            stride = 12
        container = ContainerItem(
            name=name,
            kind=kind,
            count=count,
            stride=stride,
            x=CANVAS_PADDING + 20 + len(self.project.containers) * 18,
            y=CANVAS_PADDING + 24 + len(self.project.containers) * 18,
        )
        self.project.containers.append(container)
        self.selected_container_name = container.name
        self.selected_rule_name = None
        self.selected_reflector_name = None
        self.selected_stage_name = None
        self._refresh_all()
        self._log(f"Added container {container.name}.")

    def _add_or_update_container(self) -> None:
        kind = self.container_kind_var.get()
        name = self.container_name_var.get().strip() or self.project.next_container_name(kind)
        count = max(1, int(self.container_count_var.get()))
        stride = max(1, int(self.container_stride_var.get()))
        existing = self._find_container(name)
        if existing:
            existing.kind = kind
            existing.count = count
            existing.stride = stride
            self.selected_container_name = existing.name
            self._log(f"Updated container {name}.")
        else:
            self.project.containers.append(
                ContainerItem(
                    name=name,
                    kind=kind,
                    count=count,
                    stride=stride,
                    x=CANVAS_PADDING + 30 + len(self.project.containers) * 18,
                    y=CANVAS_PADDING + 30 + len(self.project.containers) * 18,
                )
            )
            self.selected_container_name = name
            self._log(f"Created container {name}.")
        self._refresh_all()

    def _duplicate_selected_container(self) -> None:
        container = self._current_container()
        if not container:
            return
        duplicate = copy.deepcopy(container)
        duplicate.name = self.project.next_container_name(container.kind)
        duplicate.x += 36
        duplicate.y += 36
        self.project.containers.append(duplicate)
        self.selected_container_name = duplicate.name
        self._refresh_all()
        self._log(f"Duplicated container to {duplicate.name}.")

    def _delete_selected_container(self) -> None:
        container = self._current_container()
        if not container:
            return
        removed_rule_names = [
            rule.name
            for rule in self.project.decomposer_rules
            if rule.source == container.name or rule.target == container.name
        ]
        removed_reflector_names = [
            item.name
            for item in self.project.reflector_items
            if container.name in item.direct_from
            or container.name in item.direct_to
            or container.name in item.inputs_varity
            or container.name in item.inputs_array
            or item.output_name == container.name
        ]
        removed_stage_names = [
            stage.name
            for stage in self.project.intervention_stages
            if container.name in stage.used_variables or container.name in stage.used_arrays
        ]
        self.project.containers = [item for item in self.project.containers if item.name != container.name]
        self._remove_connections_for_node("container", container.name)
        for name in removed_rule_names:
            self._remove_connections_for_node("decomposer", name)
        for name in removed_reflector_names:
            self._remove_connections_for_node("reflector", name)
        for name in removed_stage_names:
            self._remove_connections_for_node("interventioner", name)
        self.project.decomposer_rules = [
            rule for rule in self.project.decomposer_rules if rule.name not in removed_rule_names
        ]
        self.project.reflector_items = [
            item for item in self.project.reflector_items
            if item.name not in removed_reflector_names
        ]
        self.project.intervention_stages = [
            stage for stage in self.project.intervention_stages
            if stage.name not in removed_stage_names
        ]
        self.selected_container_name = None
        self._refresh_all()
        self._log(f"Deleted container {container.name}.")

    def _current_container(self) -> ContainerItem | None:
        if self.selected_container_name:
            return self._find_container(self.selected_container_name)
        if not self.container_list:
            return None
        selection = self.container_list.curselection()
        if not selection:
            return None
        item = self.project.containers[selection[0]]
        self.selected_container_name = item.name
        return item

    def _add_rule_from_selection(self) -> None:
        selected = [item for item in self.project.containers if item.name in {self.selected_container_name}]
        if len(selected) != 1:
            messagebox.showinfo("Select a container", "Select one container on the canvas first.")
            return
        source = selected[0].name
        target = simpledialog.askstring("Target container", "Enter the target container name:")
        if not target:
            return
        if not self._find_container(target):
            messagebox.showerror("Missing target", f"Container {target} does not exist.")
            return
        rule_name = self.rule_name_var.get().strip() or f"{source}_to_{target}"
        self.project.decomposer_rules.append(DecomposerRule(name=rule_name, source=source, target=target))
        self.selected_rule_name = rule_name
        self._refresh_all()
        self._log(f"Added decomposer rule {rule_name}.")

    def _add_or_update_rule(self) -> None:
        name = self.rule_name_var.get().strip()
        source = self.rule_source_var.get().strip()
        target = self.rule_target_var.get().strip()
        if not name:
            messagebox.showinfo("Rule name required", "Enter a decomposer rule name.")
            return
        if not source or not target:
            messagebox.showinfo("Endpoints required", "Enter both source and target container names.")
            return
        existing = self._find_rule(name)
        if existing:
            existing.source = source
            existing.target = target
            self._log(f"Updated decomposer rule {name}.")
        else:
            self.project.decomposer_rules.append(DecomposerRule(name=name, source=source, target=target))
            self._log(f"Created decomposer rule {name}.")
        self.selected_rule_name = name
        self._refresh_all()

    def _delete_selected_rule(self) -> None:
        rule = self._current_rule()
        if not rule:
            return
        self.project.decomposer_rules = [item for item in self.project.decomposer_rules if item.name != rule.name]
        self._remove_connections_for_node("decomposer", rule.name)
        self.selected_rule_name = None
        self._refresh_all()
        self._log(f"Deleted rule {rule.name}.")

    def _current_rule(self) -> DecomposerRule | None:
        if self.selected_rule_name:
            return self._find_rule(self.selected_rule_name)
        if not self.rule_list:
            return None
        selection = self.rule_list.curselection()
        if not selection:
            return None
        rule = self.project.decomposer_rules[selection[0]]
        self.selected_rule_name = rule.name
        return rule

    def _add_default_stages(self) -> None:
        existing = {stage.name for stage in self.project.intervention_stages}
        defaults = [
            ("preTick", "preTick"),
            ("afterTick", "afterTick"),
            ("resultRender", "resultRender"),
        ]
        for name, kind in defaults:
            if name not in existing:
                self.project.intervention_stages.append(InterventionStage(name=name, kind=kind))
        self._refresh_all()
        self._log("Inserted default intervention stages.")

    def _add_or_update_reflector(self) -> None:
        name = self.reflector_name_var.get().strip() or "reflector_item"
        reflect_fun = self.reflector_fun_var.get().strip() or "direct"
        inputs_varity = [value.strip() for value in self.reflector_inputs_varity_var.get().split(",") if value.strip()]
        inputs_array = [value.strip() for value in self.reflector_inputs_array_var.get().split(",") if value.strip()]
        output_kind = self.reflector_output_kind_var.get().strip() or "v"
        output_name = self.reflector_output_name_var.get().strip()
        direct_from = [value.strip() for value in self.reflector_direct_from_var.get().split(",") if value.strip()]
        direct_to = [value.strip() for value in self.reflector_direct_to_var.get().split(",") if value.strip()]
        existing = self._find_reflector(name)
        if existing:
            existing.reflect_fun = reflect_fun
            existing.inputs_varity = inputs_varity
            existing.inputs_array = inputs_array
            existing.output_kind = output_kind
            existing.output_name = output_name
            existing.direct_from = direct_from
            existing.direct_to = direct_to
            self._log(f"Updated reflector item {name}.")
        else:
            self.project.reflector_items.append(
                ReflectorItem(
                    name=name,
                    reflect_fun=reflect_fun,
                    inputs_varity=inputs_varity,
                    inputs_array=inputs_array,
                    output_kind=output_kind,
                    output_name=output_name,
                    direct_from=direct_from,
                    direct_to=direct_to,
                )
            )
            self._log(f"Created reflector item {name}.")
        self.selected_reflector_name = name
        self._refresh_all()

    def _delete_selected_reflector(self) -> None:
        item = self._current_reflector()
        if not item:
            return
        self.project.reflector_items = [entry for entry in self.project.reflector_items if entry.name != item.name]
        self._remove_connections_for_node("reflector", item.name)
        self.selected_reflector_name = None
        self._refresh_all()
        self._log(f"Deleted reflector item {item.name}.")

    def _delete_selected_res_node(self) -> None:
        item = self._current_res_node()
        if not item:
            return
        self.project.res_nodes = [entry for entry in self.project.res_nodes if entry.name != item.name]
        self._remove_connections_for_node("resnode", item.name)
        self.selected_res_node_name = None
        self._refresh_all()
        self._log(f"Deleted resNode {item.name}.")

    def _delete_selected_function(self) -> None:
        item = self._current_function()
        if not item:
            return
        self.project.function_frames = [entry for entry in self.project.function_frames if entry.name != item.name]
        self._remove_connections_for_node("function", item.name)
        self.selected_function_name = None
        self._refresh_all()
        self._log(f"Deleted function {item.name}.")

    def _add_stage(self) -> None:
        name = self.project.next_stage_name()
        stage = InterventionStage(name=name, kind="interventioner")
        self.project.intervention_stages.append(stage)
        self.selected_stage_name = stage.name
        self._refresh_all()
        self._log(f"Added stage {stage.name}.")

    def _add_or_update_stage(self) -> None:
        name = self.stage_name_var.get().strip() or self.project.next_stage_name()
        kind = self.stage_kind_var.get().strip() or "interventioner"
        functions = [value.strip() for value in self.stage_functions_var.get().split(",") if value.strip()]
        used_variables = [value.strip() for value in self.stage_used_vars_var.get().split(",") if value.strip()]
        used_arrays = [value.strip() for value in self.stage_used_arrays_var.get().split(",") if value.strip()]
        shader_vertex = self.stage_shader_vertex_var.get().strip()
        shader_fragment = self.stage_shader_fragment_var.get().strip()
        existing = self._find_stage(name)
        if existing:
            existing.kind = kind
            existing.functions = functions
            existing.used_variables = used_variables
            existing.used_arrays = used_arrays
            existing.shader_vertex = shader_vertex
            existing.shader_fragment = shader_fragment
            self._log(f"Updated stage {name}.")
        else:
            self.project.intervention_stages.append(
                InterventionStage(
                    name=name,
                    kind=kind,
                    functions=functions,
                    used_variables=used_variables,
                    used_arrays=used_arrays,
                    shader_vertex=shader_vertex,
                    shader_fragment=shader_fragment,
                )
            )
            self._log(f"Created stage {name}.")
        self.selected_stage_name = name
        self._refresh_all()

    def _delete_selected_stage(self) -> None:
        stage = self._current_stage()
        if not stage:
            return
        self.project.intervention_stages = [item for item in self.project.intervention_stages if item.name != stage.name]
        self._remove_connections_for_node("interventioner", stage.name)
        self.selected_stage_name = None
        self._refresh_all()
        self._log(f"Deleted stage {stage.name}.")

    def _delete_selected_container_group(self) -> None:
        group = self._current_container_group()
        if not group:
            return
        self.project.container_groups = [item for item in self.project.container_groups if item.name != group.name]
        self._remove_connections_for_node("containerelement", group.name)
        self.selected_container_group_name = None
        self._refresh_all()
        self._log(f"Deleted containerElement {group.name}.")

    def _current_stage(self) -> InterventionStage | None:
        if self.selected_stage_name:
            return self._find_stage(self.selected_stage_name)
        if not self.stage_list:
            return None
        selection = self.stage_list.curselection()
        if not selection:
            return None
        stage = self.project.intervention_stages[selection[0]]
        self.selected_stage_name = stage.name
        return stage

    def _current_container_group(self) -> ContainerGroupItem | None:
        if self.selected_container_group_name:
            return self._find_container_group(self.selected_container_group_name)
        return None

    def _current_reflector(self) -> ReflectorItem | None:
        if self.selected_reflector_name:
            return self._find_reflector(self.selected_reflector_name)
        if not self.reflector_list:
            return None
        selection = self.reflector_list.curselection()
        if not selection:
            return None
        item = self.project.reflector_items[selection[0]]
        self.selected_reflector_name = item.name
        return item

    def _current_res_node(self) -> ResourceNodeItem | None:
        if self.selected_res_node_name:
            return self._find_res_node(self.selected_res_node_name)
        return None

    def _current_function(self) -> Any | None:
        if self.selected_function_name:
            return self._find_function_frame(self.selected_function_name)
        return None

    def _add_reflector_from_selection(self) -> None:
        container = self._current_container()
        if not container:
            return
        item = ReflectorItem(
            name=f"{container.name}_reflect",
            reflect_fun="direct",
            direct_from=[container.name],
            direct_to=[container.name],
        )
        self.project.reflector_items.append(item)
        self.selected_reflector_name = item.name
        self._refresh_all()
        self._log(f"Added reflector item {item.name}.")

    def _call_agent_for_selection(self) -> None:
        self._fill_chat_prompt_from_selection()
        self._send_chat_message()

    def _fill_agent_prompt_from_selection(self) -> None:
        self._fill_chat_prompt_from_selection()

    def _send_agent_prompt(self) -> None:
        self._send_chat_message()

    def _sync_agent_client_settings(self) -> None:
        self.agent_client.provider = self.provider_var.get().strip() or "mock"
        self.agent_client.model = self.model_var.get().strip() or "gpt-5.4-mini"
        self.agent_client.approval_mode = self.approval_mode_var.get().strip() or "manual"
        self.agent_client.base_url = self.base_url_var.get().strip()
        self.agent_client.api_key = self.api_key_var.get().strip()
        self.agent_client.codex_command = self.agent_command_var.get().strip() or "codex"

    def _chat_selected_context(self) -> str:
        summary = self._selected_item_summary().strip()
        selection = self._selection_label()
        return "\n".join(
            [
                f"Selection: {selection}",
                "",
                "Context:",
                summary,
            ]
        )

    def _fill_chat_prompt_from_selection(self) -> None:
        if not self.chat_input_text:
            return
        prompt = "\n".join(
            [
                self._chat_selected_context(),
                "",
                "Task:",
                "Explain, extend, or modify the selected structure.",
            ]
        ).strip()
        self.chat_input_text.delete("1.0", tk.END)
        self.chat_input_text.insert("1.0", prompt)
        self.agent_prompt_var.set(prompt)
        self._log("Chat prompt filled from current selection.")

    def _clear_chat_history(self) -> None:
        self.chat_history.clear()
        if self.chat_history_text:
            self.chat_history_text.delete("1.0", tk.END)
        self._append_chat_message("system", "Chat history cleared.")

    def _append_chat_message(self, role: str, content: str) -> None:
        message = {"role": role, "content": content}
        self.chat_history.append(message)
        if not self.chat_history_text:
            return
        self.chat_history_text.configure(state="normal")
        prefix_map = {
            "user": "User",
            "assistant": "Assistant",
            "system": "System",
            "error": "Error",
        }
        tag = role if role in {"user", "assistant", "system", "error"} else "system"
        prefix = prefix_map.get(tag, "System")
        self.chat_history_text.insert(tk.END, f"{prefix}\n", (tag,))
        self.chat_history_text.insert(tk.END, content.strip() + "\n\n", (tag,))
        self.chat_history_text.see(tk.END)
        self.chat_history_text.configure(state="disabled")

    def _send_chat_message_from_event(self, event: tk.Event) -> str:
        self._send_chat_message()
        return "break"

    def _insert_chat_newline_from_event(self, event: tk.Event) -> str:
        if not self.chat_input_text:
            raise AssertionError("Chat input box is not initialized.")
        self.chat_input_text.insert(tk.INSERT, "\n")
        return "break"

    def _send_chat_message(self) -> None:
        if self.chat_busy:
            self._log("Agent request already in progress.")
            return
        if not self.chat_input_text:
            raise AssertionError("Chat input box is not initialized.")
        prompt = self.chat_input_text.get("1.0", tk.END).strip()
        if not prompt:
            raise AssertionError("Chat prompt is empty.")
        self._sync_agent_client_settings()
        selection = self._selection_label()
        context = self._chat_selected_context()
        history_lines = [
            f"{item['role'].capitalize()}: {item['content'].strip()}"
            for item in self.chat_history[-11:-1]
            if item["role"] in {"user", "assistant"}
        ]
        transcript = "\n".join(history_lines) if history_lines else "(empty)"
        final_prompt = "\n\n".join(
            [
                context,
                "Conversation history:",
                transcript,
                "User request:",
                prompt,
            ]
        ).strip()
        self.agent_prompt_var.set(final_prompt)
        try:
            approved = self._authorize_chat_request(selection, final_prompt)
        except Exception as exc:  # noqa: BLE001
            message = str(exc)
            self._append_chat_message("error", message)
            self._log(f"Approval check failed: {message}")
            self.status_var.set("Approval check failed.")
            messagebox.showerror("Approval error", message)
            return
        if not approved:
            return
        self._append_chat_message("user", prompt)
        self.chat_input_text.delete("1.0", tk.END)
        self.chat_busy = True
        if self.chat_send_button:
            self.chat_send_button.configure(state="disabled")
        self._append_chat_message("system", f"Sending request to {self.agent_client.provider}...")
        self.status_var.set(f"Sent to {self.agent_client.provider}; waiting for reply...")
        self._log(f"Chat request sent via {self.agent_client.provider}.")

        def worker() -> None:
            try:
                response = self.agent_client.generate(self.project, selection, final_prompt)
            except Exception as exc:  # noqa: BLE001
                self.root.after(0, lambda: self._finish_chat_request_error(exc))
                return
            self.root.after(0, lambda: self._finish_chat_request_success(response))

        import threading

        thread = threading.Thread(target=worker, daemon=True)
        thread.start()

    def _run_connection_probe(self) -> None:
        if not self.connection_probe_pending:
            return
        if self.chat_busy:
            self.root.after(250, self._run_connection_probe)
            return
        self.connection_probe_pending = False
        probe_prompt = (
            "Connection probe: reply with exactly one short line so I can verify the agent is reachable."
        )
        self._append_chat_message("system", "Running connection probe...")
        self.status_var.set(f"Probing {self.agent_client.provider}...")

        def worker() -> None:
            try:
                response = self.agent_client.generate(self.project, self._selection_label(), probe_prompt)
            except Exception as exc:  # noqa: BLE001
                self.root.after(0, lambda: self._finish_connection_probe_error(exc))
                return
            self.root.after(0, lambda: self._finish_connection_probe_success(response))

        import threading

        thread = threading.Thread(target=worker, daemon=True)
        thread.start()

    def _finish_connection_probe_success(self, response: str) -> None:
        self._append_chat_message("system", f"Connection probe reply: {response}")
        self._log(f"Connection probe succeeded via {self.agent_client.provider}.")
        self.status_var.set(f"Connection probe succeeded via {self.agent_client.provider}.")

    def _finish_connection_probe_error(self, exc: Exception) -> None:
        message = str(exc)
        self._append_chat_message("error", f"Connection probe failed: {message}")
        self._log(f"Connection probe failed: {message}")
        self.status_var.set("Connection probe failed.")

    def _authorize_chat_request(self, selection: str, final_prompt: str) -> bool:
        mode = self.approval_mode_var.get().strip().lower() or "manual"
        approval_summary = "\n".join(
            [
                f"Provider: {self.agent_client.provider}",
                f"Model: {self.agent_client.model}",
                f"Selection: {selection}",
                "",
                "Prompt:",
                final_prompt,
            ]
        )
        if mode == "manual":
            approved = messagebox.askyesno("Manual approval", approval_summary)
            if not approved:
                self._log("Chat request cancelled by manual approval.")
                self.status_var.set("Request cancelled by manual approval.")
                self._append_chat_message("system", "Chat request cancelled by manual approval.")
            return approved
        if mode == "rules":
            rule_set = load_access_rules(self.access_rules_path)
            decision = evaluate_access_rules(rule_set, approval_summary)
            if decision.outcome == "deny":
                self._log(f"Chat request denied by rules: {decision.reason}")
                self.status_var.set("Request denied by access rules.")
                messagebox.showerror("Approval denied", decision.reason)
                self._append_chat_message("error", f"Approval denied: {decision.reason}")
                return False
            if decision.outcome == "manual":
                approved = messagebox.askyesno(
                    "Rule approval required",
                    f"{decision.reason}\n\nApprove sending this request?",
                )
                if not approved:
                    self._log("Chat request cancelled after rule fallback to manual approval.")
                    self.status_var.set("Request cancelled by manual approval.")
                    self._append_chat_message("system", "Chat request cancelled by manual approval.")
                return approved
            if decision.approved:
                self._log(
                    "Chat request approved by rules"
                    + (f" ({decision.matched_rule})" if decision.matched_rule else ".")
                )
                return True
            raise AssertionError(f"Unsupported approval decision: {decision.outcome}")
        raise AssertionError(f"Unsupported approval mode: {mode}")

    def _finish_chat_request_success(self, response: str) -> None:
        self.chat_busy = False
        if self.chat_send_button:
            self.chat_send_button.configure(state="normal")
        self._append_chat_message("assistant", response)
        self.agent_output_var.set(response)
        self._log(f"Agent call completed via {self.agent_client.provider}.")
        self.status_var.set(f"Agent reply received from {self.agent_client.provider}.")

    def _finish_chat_request_error(self, exc: Exception) -> None:
        self.chat_busy = False
        if self.chat_send_button:
            self.chat_send_button.configure(state="normal")
        message = str(exc)
        self._append_chat_message("error", message)
        self.agent_output_var.set(message)
        self._log(f"Agent call failed: {message}")
        self.status_var.set("Agent call failed.")

    def _selection_label(self) -> str:
        if self.selected_container_group_name:
            return f"containerElement:{self.selected_container_group_name}"
        if self.selected_container_name:
            return f"container:{self.selected_container_name}"
        if self.selected_rule_name:
            return f"rule:{self.selected_rule_name}"
        if self.selected_reflector_name:
            return f"reflector:{self.selected_reflector_name}"
        if self.selected_res_node_name:
            return f"resnode:{self.selected_res_node_name}"
        if self.selected_function_name:
            return f"function:{self.selected_function_name}"
        if self.selected_stage_name:
            return f"interventioner:{self.selected_stage_name}"
        return "project"

    def _select_container_from_list(self) -> None:
        if not self.container_list:
            return
        selection = self.container_list.curselection()
        if not selection:
            return
        container = self.project.containers[selection[0]]
        self.selected_container_name = container.name
        self.selected_rule_name = None
        self.selected_reflector_name = None
        self.selected_stage_name = None
        self.selected_container_group_name = None
        self.container_name_var.set(container.name)
        self.container_kind_var.set(container.kind)
        self.container_count_var.set(container.count)
        self.container_stride_var.set(container.stride)
        self._refresh_inspector()

    def _select_rule_from_list(self) -> None:
        if not self.rule_list:
            return
        selection = self.rule_list.curselection()
        if not selection:
            return
        rule = self.project.decomposer_rules[selection[0]]
        self.selected_rule_name = rule.name
        self.selected_container_name = None
        self.selected_reflector_name = None
        self.selected_stage_name = None
        self.selected_container_group_name = None
        self.rule_name_var.set(rule.name)
        self.rule_source_var.set(rule.source)
        self.rule_target_var.set(rule.target)
        self._refresh_inspector()

    def _select_stage_from_list(self) -> None:
        if not self.stage_list:
            return
        selection = self.stage_list.curselection()
        if not selection:
            return
        stage = self.project.intervention_stages[selection[0]]
        self.selected_stage_name = stage.name
        self.selected_container_name = None
        self.selected_rule_name = None
        self.selected_reflector_name = None
        self.selected_container_group_name = None
        self.stage_name_var.set(stage.name)
        self.stage_kind_var.set(stage.kind)
        self.stage_functions_var.set(", ".join(stage.functions))
        self.stage_used_vars_var.set(", ".join(stage.used_variables))
        self.stage_used_arrays_var.set(", ".join(stage.used_arrays))
        self.stage_shader_vertex_var.set(stage.shader_vertex)
        self.stage_shader_fragment_var.set(stage.shader_fragment)
        self._refresh_inspector()

    def _select_reflector_from_list(self) -> None:
        if not self.reflector_list:
            return
        selection = self.reflector_list.curselection()
        if not selection:
            return
        item = self.project.reflector_items[selection[0]]
        self.selected_reflector_name = item.name
        self.selected_container_name = None
        self.selected_rule_name = None
        self.selected_stage_name = None
        self.selected_container_group_name = None
        self.reflector_name_var.set(item.name)
        self.reflector_fun_var.set(item.reflect_fun)
        self.reflector_inputs_varity_var.set(", ".join(item.inputs_varity))
        self.reflector_inputs_array_var.set(", ".join(item.inputs_array))
        self.reflector_output_kind_var.set(item.output_kind)
        self.reflector_output_name_var.set(item.output_name)
        self.reflector_direct_from_var.set(", ".join(item.direct_from))
        self.reflector_direct_to_var.set(", ".join(item.direct_to))
        self._refresh_inspector()

    def _focus_selected_container_on_canvas(self) -> None:
        container = self._current_container()
        if not container or not self.canvas:
            return
        self.canvas.tag_raise(f"node:container:{container.name}")
        self._log(f"Focused {container.name} on canvas.")

    def _apply_inspector_edits(self) -> None:
        self._apply_project_vars()
        if self.selected_container_name:
            container = self._find_container(self.selected_container_name)
            if container:
                container.name = self.container_name_var.get().strip() or container.name
                container.kind = self.container_kind_var.get().strip() or container.kind
                container.count = max(1, int(self.container_count_var.get()))
                container.stride = max(1, int(self.container_stride_var.get()))
                self.selected_container_name = container.name
        if self.selected_rule_name:
            rule = self._find_rule(self.selected_rule_name)
            if rule:
                rule.name = self.rule_name_var.get().strip() or rule.name
                rule.source = self.rule_source_var.get().strip() or rule.source
                rule.target = self.rule_target_var.get().strip() or rule.target
                self.selected_rule_name = rule.name
        if self.selected_stage_name:
            stage = self._find_stage(self.selected_stage_name)
            if stage:
                stage.name = self.stage_name_var.get().strip() or stage.name
                stage.kind = self.stage_kind_var.get().strip() or stage.kind
                stage.functions = [value.strip() for value in self.stage_functions_var.get().split(",") if value.strip()]
                stage.used_variables = [value.strip() for value in self.stage_used_vars_var.get().split(",") if value.strip()]
                stage.used_arrays = [value.strip() for value in self.stage_used_arrays_var.get().split(",") if value.strip()]
                stage.shader_vertex = self.stage_shader_vertex_var.get().strip()
                stage.shader_fragment = self.stage_shader_fragment_var.get().strip()
                self.selected_stage_name = stage.name
        self._refresh_all()
        self._log("Applied inspector edits.")

    def _select_item_on_canvas(self, kind: str, name: str) -> None:
        container = self._find_container(name)
        group = self._find_container_group(name)
        reflector = self._find_reflector(name)
        res_node = self._find_res_node(name)
        function_frame = self._find_function_frame(name)
        stage = self._find_stage(name)
        rule = self._find_rule(name)
        self.selected_rule_name = None
        self.selected_function_name = None
        if kind == "container" and container:
            self.selected_container_name = container.name
            self.selected_reflector_name = None
            self.selected_res_node_name = None
            self.selected_stage_name = None
            self.selected_container_group_name = None
        elif kind == "decomposer" and rule:
            self.selected_rule_name = rule.name
            self.selected_container_name = None
            self.selected_reflector_name = None
            self.selected_res_node_name = None
            self.selected_stage_name = None
            self.selected_container_group_name = None
        elif kind == "containerelement" and group:
            self.selected_container_group_name = group.name
            self.selected_container_name = None
            self.selected_reflector_name = None
            self.selected_res_node_name = None
            self.selected_stage_name = None
        elif kind == "reflector" and reflector:
            self.selected_reflector_name = reflector.name
            self.selected_container_name = None
            self.selected_res_node_name = None
            self.selected_stage_name = None
            self.selected_container_group_name = None
        elif kind == "resnode" and res_node:
            self.selected_res_node_name = res_node.name
            self.selected_container_name = None
            self.selected_reflector_name = None
            self.selected_stage_name = None
            self.selected_container_group_name = None
            self.selected_function_name = None
        elif kind == "function" and function_frame:
            self.selected_function_name = function_frame.name
            self.selected_container_name = None
            self.selected_reflector_name = None
            self.selected_res_node_name = None
            self.selected_stage_name = None
            self.selected_container_group_name = None
        elif kind == "interventioner" and stage:
            self.selected_stage_name = stage.name
            self.selected_container_name = None
            self.selected_reflector_name = None
            self.selected_res_node_name = None
            self.selected_container_group_name = None
            self.selected_function_name = None
        elif kind == "stage" and stage:
            self.selected_stage_name = stage.name
            self.selected_container_name = None
            self.selected_reflector_name = None
            self.selected_res_node_name = None
            self.selected_container_group_name = None
            self.selected_function_name = None
        self._redraw_canvas()
        self._refresh_inspector()

    def _on_canvas_click(self, event: tk.Event) -> None:
        if not self.canvas:
            return
        item_id = self._canvas_item_hit(event.x, event.y)
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
                    "x": event.x,
                    "y": event.y,
                    "width": group.width,
                    "height": group.height,
                }
                return
            if resize_kind in {"decomposer", "reflector", "resnode", "interventioner", "stage"}:
                node = self._node_by_kind_name(resize_kind, resize_name)
                if node is None:
                    raise AssertionError(f"Missing {resize_kind} node {resize_name}")
                self._select_item_on_canvas(resize_kind, resize_name)
                self.toolnode_resize_state = {
                    "kind": resize_kind,
                    "name": resize_name,
                    "x": event.x,
                    "y": event.y,
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
                if "group_header" in tags:
                    should_drag = True
                elif "group_body" in tags:
                    if self.marquee_state is not None:
                        raise AssertionError("Marquee state should not already be active.")
                    self.marquee_state = {
                        "x0": event.x,
                        "y0": event.y,
                        "x1": event.x,
                        "y1": event.y,
                        "item_id": self.canvas.create_rectangle(
                            event.x,
                            event.y,
                            event.x,
                            event.y,
                            outline=COLORS["accent"],
                            dash=(3, 2),
                            tags=("marquee",),
                        ),
                        "scope_group": node_name,
                    }
                    self._refresh_inspector()
                    return
            elif kind in {"decomposer", "reflector", "resnode", "function", "interventioner", "stage"}:
                should_drag = "node_header" in tags
            if should_drag:
                self.node_drag_state = {
                    "kind": kind,
                    "name": node_name,
                    "x": event.x,
                    "y": event.y,
                }
                if kind == "containerelement":
                    self.container_group_drag_state = {
                        "name": node_name,
                        "x": event.x,
                        "y": event.y,
                    }
            return
        if item_id is None:
            self.selected_container_name = None
            self.selected_rule_name = None
            self.selected_reflector_name = None
            self.selected_res_node_name = None
            self.selected_stage_name = None
            self.selected_container_group_name = None
            self._redraw_canvas()
            self.marquee_state = {
                "x0": event.x,
                "y0": event.y,
                "x1": event.x,
                "y1": event.y,
                "item_id": self.canvas.create_rectangle(
                    event.x,
                    event.y,
                    event.x,
                    event.y,
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
        if self.canvas_pan_state:
            self._drag_canvas_pan(event.x, event.y)
            return
        if self.connection_drag_state:
            self._update_connection_drag_preview(event.x, event.y)
            return
        if self.marquee_state:
            x0 = float(self.marquee_state["x0"])
            y0 = float(self.marquee_state["y0"])
            x1 = event.x
            y1 = event.y
            self.marquee_state["x1"] = x1
            self.marquee_state["y1"] = y1
            item_id = self.marquee_state.get("item_id")
            if item_id:
                self.canvas.coords(item_id, x0, y0, x1, y1)
            return
        if self.container_group_resize_state:
            name = str(self.container_group_resize_state["name"])
            group = self._find_container_group(name)
            if not group:
                raise AssertionError(f"Missing containerElement {name}")
            min_width = 220.0
            min_height = 160.0
            new_width = max(min_width, float(self.container_group_resize_state["width"]) + (event.x - float(self.container_group_resize_state["x"])))
            new_height = max(min_height, float(self.container_group_resize_state["height"]) + (event.y - float(self.container_group_resize_state["y"])))
            group.width = new_width
            group.height = new_height
            self._refresh_all()
            return
        if self.toolnode_resize_state:
            self._drag_toolnode_resize(event.x, event.y)
            return
        if not self.node_drag_state:
            return
        dx = event.x - self.node_drag_state["x"]
        dy = event.y - self.node_drag_state["y"]
        kind = str(self.node_drag_state["kind"])
        name = str(self.node_drag_state["name"])
        self.node_drag_state["x"] = event.x
        self.node_drag_state["y"] = event.y
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
        elif kind == "interventioner" or kind == "stage":
            stage = self._find_stage(name)
            if stage:
                stage.x += dx
                stage.y += dy
        self._redraw_canvas()

    def _on_canvas_release(self, event: tk.Event) -> None:
        if self.canvas_pan_state:
            self._finish_canvas_pan()
            return
        if self.connection_drag_state:
            self._finish_connection_drag(event)
            return
        if self.marquee_state:
            x0 = float(self.marquee_state["x0"])
            y0 = float(self.marquee_state["y0"])
            x1 = float(self.marquee_state["x1"])
            y1 = float(self.marquee_state["y1"])
            if abs(x1 - x0) >= 8 and abs(y1 - y0) >= 8:
                rect = self._normalize_rect(x0, y0, x1, y1)
                scope_group_name = self.marquee_state.get("scope_group")
                if scope_group_name:
                    group = self._find_container_group(str(scope_group_name))
                    if not group:
                        raise AssertionError(f"Missing containerElement {scope_group_name}")
                    variables, arrays = self._members_inside_group_rect(group, rect)
                else:
                    variables, arrays = self._members_inside_rect(rect)
                if not variables and not arrays:
                    self._log("Marquee selection did not include any variables or arrays.")
                else:
                    prompt_title = "Nested containerElement name" if self.marquee_state.get("scope_group") else "ContainerElement name"
                    prompt_text = "Enter the merged container name:"
                    name = simpledialog.askstring(prompt_title, prompt_text)
                    if name:
                        group_name = name.strip()
                        if not group_name:
                            raise AssertionError("ContainerElement name cannot be empty.")
                        if self._find_container_group(group_name):
                            messagebox.showerror("Duplicate name", f"ContainerElement {group_name} already exists.")
                        else:
                            group = ContainerGroupItem(
                                name=group_name,
                                x=rect[0],
                                y=rect[1],
                                width=max(220.0, rect[2] - rect[0]),
                                height=max(160.0, rect[3] - rect[1]),
                            )
                            group.variables = variables
                            group.arrays = arrays
                            if not group.variables and not group.arrays:
                                raise AssertionError("ContainerElement selection produced no members.")
                            self.project.validate_container_group(group)
                            self.project.container_groups.append(group)
                            self.selected_container_group_name = group.name
                            self._refresh_all()
                            self._log(
                                f"Created containerElement {group.name} with {len(variables)} variable(s) and {len(arrays)} array(s)."
                            )
            item_id = self.marquee_state.get("item_id")
            if item_id:
                self.canvas.delete(item_id)
            self.marquee_state = None
            self.container_group_drag_state = None
            self.container_group_resize_state = None
            self.node_drag_state = None
            self._refresh_all()
            return
        if self.container_group_resize_state:
            self.container_group_resize_state = None
            self.container_group_drag_state = None
            self.node_drag_state = None
            self._refresh_all()
            return
        if self.toolnode_resize_state:
            self.toolnode_resize_state = None
            self.node_drag_state = None
            self._refresh_all()
            return
        if self.node_drag_state:
            self._sync_all_container_groups()
            self.node_drag_state = None
            self.container_group_drag_state = None
            self.container_group_resize_state = None
            self.toolnode_resize_state = None
            self._refresh_all()
            return
        self.node_drag_state = None

    def _on_canvas_right_press(self, event: tk.Event) -> None:
        if not self.canvas:
            return
        if self._canvas_item_hit(event.x, event.y) is not None:
            return
        self.canvas_pan_state = {
            "x": event.x,
            "y": event.y,
        }

    def _on_canvas_right_drag(self, event: tk.Event) -> None:
        if not self.canvas_pan_state:
            return
        self._drag_canvas_pan(event.x, event.y)

    def _on_canvas_right_release(self, event: tk.Event) -> None:
        if self.canvas_pan_state:
            self._finish_canvas_pan()
            return
        self._show_canvas_context_menu(event)

    def _show_canvas_context_menu(self, event: tk.Event) -> None:
        if not self.canvas:
            return
        item_id = self._canvas_item_hit(event.x, event.y)
        if item_id is None:
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
            menu.add_command(label="Duplicate", command=self._duplicate_selected_container)
            menu.add_separator()
            menu.add_command(label="Delete", command=self._delete_selected_container)
        elif kind == "containerelement":
            menu.add_command(label="Show details", command=lambda: self._show_container_group_details(node_name))
            menu.add_separator()
            menu.add_command(label="Delete", command=self._delete_selected_container_group)
        elif kind == "decomposer":
            menu.add_command(label="Delete", command=self._delete_selected_rule)
        elif kind == "reflector":
            menu.add_command(label="Delete", command=self._delete_selected_reflector)
        elif kind == "resnode":
            menu.add_command(label="Delete", command=self._delete_selected_res_node)
        elif kind == "function":
            menu.add_command(label="Delete", command=self._delete_selected_function)
        elif kind == "interventioner" or kind == "stage":
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
        self._refresh_all()

    def _node_name_from_tags(self, tags: tuple[str, ...]) -> str | None:
        kind, name = self._node_info_from_tags(tags)
        if kind and name:
            return name
        return None

    def _port_info_from_tags(self, tags: tuple[str, ...]) -> tuple[str, str, str, str] | None:
        for tag in tags:
            if tag.startswith("port:"):
                parts = tag.split(":", 4)
                if len(parts) == 5:
                    return parts[1], parts[2], parts[3], parts[4]
        return None

    def _resize_handle_info_from_tags(self, tags: tuple[str, ...]) -> tuple[str, str] | None:
        for tag in tags:
            if tag.startswith("resize_handle:"):
                parts = tag.split(":", 2)
                if len(parts) == 3:
                    return parts[1], parts[2]
        return None

    def _normalize_rect(self, x0: float, y0: float, x1: float, y1: float) -> tuple[float, float, float, float]:
        left = min(x0, x1)
        top = min(y0, y1)
        right = max(x0, x1)
        bottom = max(y0, y1)
        return left, top, right, bottom

    def _point_in_rect(self, x: float, y: float, rect: tuple[float, float, float, float]) -> bool:
        left, top, right, bottom = rect
        return left <= x <= right and top <= y <= bottom

    def _group_name_from_resize_handle(self, tags: tuple[str, ...]) -> str | None:
        handle_info = self._resize_handle_info_from_tags(tags)
        if not handle_info:
            return None
        kind, name = handle_info
        if kind != "containerelement":
            raise AssertionError(f"Unsupported resize handle kind: {kind}")
        return name

    def _update_container_group_geometry(self, group: ContainerGroupItem, dx: float, dy: float) -> None:
        self._move_container_group_and_members(group, dx, dy)

    def _move_container_group_and_members(
        self,
        group: ContainerGroupItem,
        dx: float,
        dy: float,
        visited: set[str] | None = None,
    ) -> None:
        if visited is None:
            visited = set()
        if group.name in visited:
            raise AssertionError(f"Cycle detected while moving containerElement {group.name}.")
        visited.add(group.name)
        group.x += dx
        group.y += dy
        for container in self.project.containers:
            if container.name in group.variables or container.name in group.arrays:
                container.x += dx
                container.y += dy
        for child_name in group.groups:
            child_group = self._find_container_group(child_name)
            if not child_group:
                raise AssertionError(f"Missing child containerElement {child_name}")
            self._move_container_group_and_members(child_group, dx, dy, visited)

    def _drag_toolnode_resize(self, x: int, y: int) -> None:
        if not self.toolnode_resize_state:
            raise AssertionError("Toolnode resize requested without active state.")
        kind = str(self.toolnode_resize_state["kind"])
        name = str(self.toolnode_resize_state["name"])
        node = self._node_by_kind_name(kind, name)
        if node is None:
            raise AssertionError(f"Missing {kind} node {name}")
        base_width = float(self.toolnode_resize_state["width"])
        base_height = float(self.toolnode_resize_state["height"])
        start_x = float(self.toolnode_resize_state["x"])
        start_y = float(self.toolnode_resize_state["y"])
        node.width = max(320.0, base_width + (x - start_x))
        node.height = max(180.0, base_height + (y - start_y))
        self._refresh_all()

    def _node_center(self, kind: str, name: str) -> tuple[float, float]:
        if kind == "container":
            container = self._find_container(name)
            if not container:
                raise ValueError(f"Missing container {name}")
            return container.x + NODE_WIDTH / 2, container.y + NODE_HEIGHT / 2
        if kind == "containerelement":
            group = self._find_container_group(name)
            if not group:
                raise ValueError(f"Missing containerElement {name}")
            return group.x + group.width / 2, group.y + group.height / 2
        raise ValueError(f"Unsupported center lookup kind: {kind}")

    def _members_inside_rect(self, rect: tuple[float, float, float, float]) -> tuple[list[str], list[str]]:
        variables: list[str] = []
        arrays: list[str] = []
        for container in self.project.containers:
            c_left, c_top, c_right, c_bottom = self._container_rect(container)
            if self._rects_intersect(rect[0], rect[1], rect[2], rect[3], c_left, c_top, c_right, c_bottom):
                if container.kind == "variable":
                    variables.append(container.name)
                elif container.kind == "array":
                    arrays.append(container.name)
                else:
                    raise AssertionError(f"Unsupported container kind: {container.kind}")
        return variables, arrays

    def _members_inside_group_rect(
        self,
        group: ContainerGroupItem,
        rect: tuple[float, float, float, float],
    ) -> tuple[list[str], list[str]]:
        variables: list[str] = []
        arrays: list[str] = []
        group_left, group_top, group_right, group_bottom = self._group_bounds(group)
        left, top, right, bottom = self._normalize_rect(
            max(rect[0], group_left),
            max(rect[1], group_top),
            min(rect[2], group_right),
            min(rect[3], group_bottom),
        )
        if right < left or bottom < top:
            return variables, arrays
        for container in self.project.containers:
            c_left, c_top, c_right, c_bottom = self._container_rect(container)
            if not self._rects_intersect(left, top, right, bottom, c_left, c_top, c_right, c_bottom):
                continue
            if not self.project._rect_contains_rect(group_left, group_top, group_right, group_bottom, c_left, c_top, c_right, c_bottom):
                continue
            if container.kind == "variable":
                variables.append(container.name)
            elif container.kind == "array":
                arrays.append(container.name)
            else:
                raise AssertionError(f"Unsupported container kind: {container.kind}")
        return variables, arrays

    def _container_group_tree_lines(self, name: str, indent: int = 0, visited: set[str] | None = None) -> list[str]:
        group = self._find_container_group(name)
        if not group:
            raise AssertionError(f"Missing containerElement {name}")
        if visited is None:
            visited = set()
        if name in visited:
            return [f"{'  ' * indent}{name} <cycle>"]
        visited = set(visited)
        visited.add(name)
        prefix = "  " * indent
        members = list(group.variables) + list(group.arrays)
        if not group.groups:
            if members:
                return [f"{prefix}{group.name} {{ " + ", ".join(members) + " }}"]
            return [f"{prefix}{group.name} {{ }}"]
        lines = [f"{prefix}{group.name} {{"]
        if members:
            lines.append(f"{prefix}  " + ", ".join(members))
        for child_name in group.groups:
            child_group = self._find_container_group(child_name)
            if not child_group:
                raise AssertionError(f"Missing child containerElement {child_name}")
            lines.extend(self._container_group_tree_lines(child_group.name, indent + 1, visited))
        lines.append(f"{prefix}}}")
        return lines

    def _container_group_detail_text(self, name: str) -> str:
        group = self._find_container_group(name)
        if not group:
            raise AssertionError(f"Missing containerElement {name}")
        lines = [
            "Selected containerElement",
            f"name: {group.name}",
            f"canvas: ({int(group.x)}, {int(group.y)})",
            f"size: {int(group.width)} x {int(group.height)}",
            "",
            "Structure:",
        ]
        lines.extend(self._container_group_tree_lines(group.name))
        lines.extend(
            [
                "",
                "Actions:",
                "- left-drag inside the box to select only contained variables/arrays",
                "- drag the header to move the group and its members",
                "- drag the corner handle to resize",
                "- right click for details or delete",
            ]
        )
        return "\n".join(lines)

    def _schema_leaf_annotation(self, value: Any) -> str | None:
        text = str(value).strip().lower()
        if text.startswith("v"):
            return "v"
        if text.startswith("a"):
            return "a"
        return None

    def _schema_detail_lines(self, schema: Any, indent: int = 0, name: str | None = None) -> list[str]:
        prefix = "  " * indent
        if isinstance(schema, dict):
            if name is not None:
                lines = [f"{prefix}{name} {{"]
                child_indent = indent + 1
            else:
                lines = []
                child_indent = indent
            for key, value in schema.items():
                lines.extend(self._schema_detail_lines(value, child_indent, str(key)))
            if name is not None:
                lines.append(f"{prefix}}}")
            return lines or ([f"{prefix}{name} {{}}"] if name is not None else [f"{prefix}{{}}"])
        if isinstance(schema, list):
            if name is not None:
                lines = [f"{prefix}{name} ["]
                child_indent = indent + 1
            else:
                lines = []
                child_indent = indent
            for index, value in enumerate(schema, start=1):
                lines.extend(self._schema_detail_lines(value, child_indent, f"[{index}]"))
            if name is not None:
                lines.append(f"{prefix}]")
            return lines or ([f"{prefix}{name} []"] if name is not None else [f"{prefix}[]"])
        text = str(schema).strip()
        if not text:
            text = "-"
        annotation = self._schema_leaf_annotation(text)
        if annotation:
            text = f"{text} [{annotation}]"
        if name is None:
            return [f"{prefix}{text}"]
        return [f"{prefix}{name}: {text}"]

    def _rebuild_group_memberships(self) -> None:
        for group in self.project.container_groups:
            variables, arrays = self._container_group_members(group)
            group.variables = variables
            group.arrays = arrays

    def _canvas_item_at(self, x: int, y: int) -> int | None:
        if not self.canvas:
            return None
        items = self.canvas.find_overlapping(x - 2, y - 2, x + 2, y + 2)
        if items:
            for item_id in reversed(items):
                tags = self.canvas.gettags(item_id)
                if not tags:
                    continue
                if "connection_preview" in tags:
                    continue
                if "marquee" in tags:
                    continue
                if "grid" in tags:
                    continue
                return item_id
        closest = self.canvas.find_closest(x, y)
        if not closest:
            return None
        item_id = closest[0]
        tags = self.canvas.gettags(item_id)
        if not tags or "connection_preview" in tags or "marquee" in tags or "grid" in tags:
            return None
        return item_id

    def _canvas_item_hit(self, x: int, y: int) -> int | None:
        if not self.canvas:
            return None
        items = self.canvas.find_overlapping(x - 2, y - 2, x + 2, y + 2)
        if not items:
            return None
        for item_id in reversed(items):
            tags = self.canvas.gettags(item_id)
            if not tags or "connection_preview" in tags or "marquee" in tags or "grid" in tags:
                continue
            return item_id
        return None

    def _move_scene(self, dx: float, dy: float) -> None:
        for container in self.project.containers:
            container.x += dx
            container.y += dy
        for group in self.project.container_groups:
            group.x += dx
            group.y += dy
        for rule in self.project.decomposer_rules:
            rule.x += dx
            rule.y += dy
        for item in self.project.reflector_items:
            item.x += dx
            item.y += dy
        for item in self.project.res_nodes:
            item.x += dx
            item.y += dy
        for stage in self.project.intervention_stages:
            stage.x += dx
            stage.y += dy

    def _drag_canvas_pan(self, x: int, y: int) -> None:
        if not self.canvas_pan_state:
            raise AssertionError("Canvas pan drag requested without active state.")
        last_x = float(self.canvas_pan_state["x"])
        last_y = float(self.canvas_pan_state["y"])
        dx = x - last_x
        dy = y - last_y
        if dx == 0 and dy == 0:
            return
        self.canvas_pan_state["x"] = x
        self.canvas_pan_state["y"] = y
        self._move_scene(dx, dy)
        if self.canvas:
            self.canvas.move("all", dx, dy)

    def _finish_canvas_pan(self) -> None:
        if not self.canvas_pan_state:
            return
        self.canvas_pan_state = None
        self._refresh_all()

    def _start_connection_drag(self, port_info: tuple[str, str, str, str], event: tk.Event) -> None:
        kind, name, direction, port = port_info
        if direction not in {"in", "out"}:
            raise AssertionError(f"Unsupported port direction: {direction}")
        if self.connection_drag_state is not None:
            raise AssertionError("Connection drag state was unexpectedly left active.")
        start_x, start_y = self._port_canvas_position(kind, name, direction, port)
        self.connection_drag_state = {
            "start": port_info,
            "x": float(event.x),
            "y": float(event.y),
            "line_id": None,
        }
        if self.canvas:
            line_id = self.canvas.create_line(
                start_x,
                start_y,
                event.x,
                event.y,
                fill=COLORS["accent"],
                width=3,
                dash=(5, 3),
                arrow=tk.LAST,
                tags=("connection_preview",),
            )
            self.connection_drag_state["line_id"] = line_id
        self._log(f"Started connection drag from {kind}:{name}:{port}.")

    def _update_connection_drag_preview(self, x: float, y: float) -> None:
        if not self.connection_drag_state:
            raise AssertionError("Connection preview update requested without an active drag.")
        self.connection_drag_state["x"] = float(x)
        self.connection_drag_state["y"] = float(y)
        if not self.canvas:
            return
        start_kind, start_name, start_direction, start_port = self.connection_drag_state["start"]
        start_x, start_y = self._port_canvas_position(start_kind, start_name, start_direction, start_port)
        line_id = self.connection_drag_state.get("line_id")
        if line_id is not None:
            try:
                self.canvas.coords(line_id, start_x, start_y, x, y)
                return
            except tk.TclError:
                pass
        line_id = self.canvas.create_line(
            start_x,
            start_y,
            x,
            y,
            fill=COLORS["accent"],
            width=3,
            dash=(5, 3),
            arrow=tk.LAST,
            tags=("connection_preview",),
        )
        self.connection_drag_state["line_id"] = line_id

    def _finish_connection_drag(self, event: tk.Event) -> None:
        if not self.connection_drag_state:
            raise AssertionError("Connection drag finished without active state.")
        preview_line_id = self.connection_drag_state.get("line_id")
        if self.canvas and preview_line_id:
            self.canvas.delete(preview_line_id)
        start_port_info = self.connection_drag_state["start"]
        self.connection_drag_state = None
        item_id = self._canvas_item_hit(event.x, event.y)
        if item_id is None or not self.canvas:
            self._log("Connection drag cancelled.")
            return
        tags = self.canvas.gettags(item_id)
        end_port_info = self._port_info_from_tags(tags)
        if not end_port_info:
            self._log("Connection drag cancelled.")
            return
        connection = self._connection_from_port_pair(start_port_info, end_port_info)
        if connection is None:
            self._log("Connection drag cancelled.")
            return
        try:
            self.project.validate_connection(connection)
        except ValueError as exc:
            self._flash_invalid_connection(start_port_info, end_port_info, item_id, str(exc))
            messagebox.showerror("Invalid connection", str(exc))
            return
        self.project.connections.append(connection)
        self._refresh_all()
        self._log(
            f"Connected {connection.source_kind}:{connection.source_name}:{connection.source_port} -> "
            f"{connection.target_kind}:{connection.target_name}:{connection.target_port}."
        )

    def _cancel_connection_drag(self) -> None:
        if not self.connection_drag_state:
            return
        preview_line_id = self.connection_drag_state.get("line_id")
        if self.canvas and preview_line_id:
            self.canvas.delete(preview_line_id)
        self.connection_drag_state = None
        self._log("Connection drag cancelled.")

    def _flash_invalid_connection(
        self,
        start_port_info: tuple[str, str, str, str],
        end_port_info: tuple[str, str, str, str],
        target_item_id: int,
        reason: str,
    ) -> None:
        if not self.canvas:
            raise AssertionError(reason)
        start_kind, start_name, start_direction, start_port = start_port_info
        end_kind, end_name, end_direction, end_port = end_port_info
        start_x, start_y = self._port_canvas_position(start_kind, start_name, start_direction, start_port)
        end_x, end_y = self._port_canvas_position(end_kind, end_name, end_direction, end_port)
        flash_line = self.canvas.create_line(
            start_x,
            start_y,
            end_x,
            end_y,
            fill=COLORS["bad"],
            width=4,
            dash=(6, 4),
            arrow=tk.LAST,
            tags=("connection_invalid",),
        )
        bbox = self.canvas.bbox(target_item_id)
        flash_rect: int | None = None
        if bbox is not None:
            left, top, right, bottom = bbox
            flash_rect = self.canvas.create_rectangle(
                left - 4,
                top - 4,
                right + 4,
                bottom + 4,
                outline=COLORS["bad"],
                width=3,
                dash=(4, 2),
                tags=("connection_invalid",),
            )

        def restore() -> None:
            if not self.canvas:
                return
            try:
                self.canvas.delete(flash_line)
            except tk.TclError:
                pass
            if flash_rect is not None:
                try:
                    self.canvas.delete(flash_rect)
                except tk.TclError:
                    pass

        self.canvas.after(250, restore)
        self._log(f"Rejected connection: {reason}")

    def _port_canvas_position(self, kind: str, name: str, direction: str, port: str) -> tuple[float, float]:
        key = self._port_key(kind, name, direction, port)
        if key not in self.canvas_port_positions:
            raise AssertionError(f"Missing port position for {key}")
        return self.canvas_port_positions[key]

    def _connection_from_port_pair(
        self,
        start_port_info: tuple[str, str, str, str],
        end_port_info: tuple[str, str, str, str],
    ) -> ConnectionItem | None:
        start_kind, start_name, start_direction, start_port = start_port_info
        end_kind, end_name, end_direction, end_port = end_port_info
        if start_port_info == end_port_info:
            return None
        if start_direction == end_direction:
            messagebox.showinfo("Connection", "Connect an input port to an output port.")
            return None
        if start_direction == "out" and end_direction == "in":
            connection = ConnectionItem(
                source_kind=start_kind,
                source_name=start_name,
                source_port=start_port,
                target_kind=end_kind,
                target_name=end_name,
                target_port=end_port,
            )
        elif start_direction == "in" and end_direction == "out":
            connection = ConnectionItem(
                source_kind=end_kind,
                source_name=end_name,
                source_port=end_port,
                target_kind=start_kind,
                target_name=start_name,
                target_port=start_port,
            )
        else:
            raise AssertionError(f"Unsupported connection direction pair: {start_direction} -> {end_direction}")
        return connection

    def _delete_connection(self, connection: ConnectionItem) -> None:
        before = len(self.project.connections)
        self.project.connections = [item for item in self.project.connections if item is not connection]
        if len(self.project.connections) == before:
            raise AssertionError("Requested connection was not found in the project.")
        self._refresh_all()
        self._log(
            f"Deleted connection {connection.source_kind}:{connection.source_name}:{connection.source_port} -> "
            f"{connection.target_kind}:{connection.target_name}:{connection.target_port}."
        )

    def _node_info_from_tags(self, tags: tuple[str, ...]) -> tuple[str | None, str | None]:
        for tag in tags:
            if tag.startswith("node:"):
                parts = tag.split(":", 2)
                if len(parts) == 3:
                    return parts[1], parts[2]
        return None, None

    def _port_info_from_tags(self, tags: tuple[str, ...]) -> tuple[str, str, str, str] | None:
        for tag in tags:
            if tag.startswith("port:"):
                parts = tag.split(":", 4)
                if len(parts) == 5:
                    return parts[1], parts[2], parts[3], parts[4]
        return None

    def _port_key(self, kind: str, name: str, direction: str, port: str) -> str:
        return f"{kind}:{name}:{direction}:{port}"

    def _register_port(self, kind: str, name: str, direction: str, port: str, x: float, y: float) -> None:
        self.canvas_port_positions[self._port_key(kind, name, direction, port)] = (x, y)

    def _node_ports(self, kind: str, name: str) -> tuple[list[str], list[str]]:
        if kind == "container":
            return ["in"], ["out"]
        if kind == "containerelement":
            return ["in"], ["out"]
        if kind == "decomposer":
            rule = self._find_rule(name)
            if not rule:
                raise ValueError(f"Missing decomposer node: {name}")
            outputs = self.project.output_ports_for("decomposer", name)
            return ["in"], (outputs if outputs else [rule.target or "out"])
        if kind == "reflector":
            item = self._find_reflector(name)
            if not item:
                raise ValueError(f"Missing reflector node: {name}")
            outputs = list(item.direct_to) if item.direct_to else ([item.output_name] if item.output_name else [])
            if not outputs:
                outputs = [item.output_kind or "out"]
            return ["in"], outputs
        if kind == "resnode":
            item = self._find_res_node(name)
            if not item:
                raise ValueError(f"Missing resNode node: {name}")
            outputs = list(item.outputs) if item.outputs else list(item.resource_types)
            if not outputs:
                outputs = [item.resource_kind or "out"]
            return ["in"], outputs
        if kind == "function":
            item = self._find_function_frame(name)
            if not item:
                raise ValueError(f"Missing function node: {name}")
            return [item.input_name or "in"], [item.output_name or "out"]
        if kind == "interventioner" or kind == "stage":
            stage = self._find_stage(name)
            if not stage:
                raise ValueError(f"Missing interventioner node: {name}")
            outputs = list(stage.functions) if stage.functions else [stage.kind or "out"]
            return ["in"], outputs
        raise ValueError(f"Unsupported node kind: {kind}")

    def _connection_key(self, connection: ConnectionItem) -> str:
        return (
            f"{connection.source_kind}:{connection.source_name}:{connection.source_port}"
            f"->{connection.target_kind}:{connection.target_name}:{connection.target_port}"
        )

    def _remove_connections_for_node(self, kind: str, name: str) -> None:
        before = len(self.project.connections)
        self.project.connections = [
            connection
            for connection in self.project.connections
            if not (
                (connection.source_kind == kind and connection.source_name == name)
                or (connection.target_kind == kind and connection.target_name == name)
            )
        ]
        removed = before - len(self.project.connections)
        if removed:
            self._log(f"Removed {removed} connection(s) attached to {kind}:{name}.")

    def _connection_from_canvas_item(self, item_id: int) -> ConnectionItem | None:
        index = self.canvas_connection_item_to_index.get(item_id)
        if index is None:
            return None
        if index < 0 or index >= len(self.project.connections):
            raise AssertionError(f"Connection index out of range: {index}")
        return self.project.connections[index]

    def _redraw_canvas(self) -> None:
        if not self.canvas:
            return
        canvas = self.canvas
        canvas.delete("all")

        width = max(canvas.winfo_width(), 1)
        height = max(canvas.winfo_height(), 1)
        self._draw_grid(canvas, width, height)
        self.canvas_container_group_nodes.clear()
        self.canvas_nodes.clear()
        self.canvas_item_to_name.clear()
        self.canvas_port_positions.clear()
        self.canvas_connection_item_to_index.clear()
        for group in sorted(self.project.container_groups, key=lambda item: item.width * item.height, reverse=True):
            item_id = self._draw_container_group_node(canvas, group)
            self.canvas_container_group_nodes[group.name] = item_id
            self.canvas_item_to_name[item_id] = group.name
        for rule in self.project.decomposer_rules:
            item_id = self._draw_decomposer_node(canvas, rule)
            self.canvas_nodes[rule.name] = item_id
            self.canvas_item_to_name[item_id] = rule.name
        for container in self.project.containers:
            item_id = self._draw_container_node(canvas, container)
            self.canvas_nodes[container.name] = item_id
            self.canvas_item_to_name[item_id] = container.name
        for item in self.project.reflector_items:
            item_id = self._draw_reflector_node(canvas, item)
            self.canvas_nodes[item.name] = item_id
            self.canvas_item_to_name[item_id] = item.name
        for item in self.project.res_nodes:
            item_id = self._draw_res_node(canvas, item)
            self.canvas_nodes[item.name] = item_id
            self.canvas_item_to_name[item_id] = item.name
        for item in self.project.function_frames:
            item_id = self._draw_function_frame_node(canvas, item)
            self.canvas_nodes[item.name] = item_id
            self.canvas_item_to_name[item_id] = item.name
        for stage in self.project.intervention_stages:
            item_id = self._draw_stage_node(canvas, stage)
            self.canvas_nodes[stage.name] = item_id
            self.canvas_item_to_name[item_id] = stage.name
        self._draw_connections(canvas)
        if self.connection_drag_state:
            self._draw_connection_drag_preview(canvas)

    def _draw_connections(self, canvas: tk.Canvas) -> None:
        for index, connection in enumerate(self.project.connections):
            source_key = self._port_key(connection.source_kind, connection.source_name, "out", connection.source_port)
            target_key = self._port_key(connection.target_kind, connection.target_name, "in", connection.target_port)
            if source_key not in self.canvas_port_positions:
                raise AssertionError(f"Missing source port position for {source_key}")
            if target_key not in self.canvas_port_positions:
                raise AssertionError(f"Missing target port position for {target_key}")
            sx, sy = self.canvas_port_positions[source_key]
            tx, ty = self.canvas_port_positions[target_key]
            line_id = canvas.create_line(
                sx,
                sy,
                (sx + tx) / 2,
                sy,
                (sx + tx) / 2,
                ty,
                tx,
                ty,
                fill=COLORS["edge"],
                width=3,
                smooth=True,
                splinesteps=24,
                arrow=tk.LAST,
                tags=("connection", self._connection_key(connection)),
            )
            self.canvas_connection_item_to_index[line_id] = index
        if self.project.connections:
            canvas.tag_lower("connection")

    def _draw_connection_drag_preview(self, canvas: tk.Canvas) -> None:
        if not self.connection_drag_state:
            return
        start_kind, start_name, start_direction, start_port = self.connection_drag_state["start"]
        start_x, start_y = self._port_canvas_position(start_kind, start_name, start_direction, start_port)
        end_x = float(self.connection_drag_state["x"])
        end_y = float(self.connection_drag_state["y"])
        line_id = canvas.create_line(
            start_x,
            start_y,
            end_x,
            end_y,
            fill=COLORS["accent"],
            width=3,
            dash=(5, 3),
            arrow=tk.LAST,
            tags=("connection_preview",),
        )
        self.connection_drag_state["line_id"] = line_id

    def _draw_container_group_node(self, canvas: tk.Canvas, group: ContainerGroupItem) -> int:
        x = group.x or CANVAS_PADDING + 20
        y = group.y or CANVAS_PADDING + 20
        width = max(group.width, 220.0)
        height = max(group.height, 160.0)
        node_tag = f"node:containerelement:{group.name}"
        outline = COLORS["accent"] if self.selected_container_group_name == group.name else COLORS["good"]

        item_id = canvas.create_rectangle(
            x,
            y,
            x + width,
            y + height,
            fill="#141a22",
            outline=outline,
            width=2,
            dash=(4, 3),
            tags=(node_tag, "group_node", "group_body"),
        )
        header_height = 30
        canvas.create_rectangle(
            x,
            y,
            x + width,
            y + header_height,
            fill=COLORS["good"],
            outline=outline,
            width=2,
            tags=(node_tag, "group_node", "group_header", "draggable"),
        )
        canvas.create_text(
            x + 12,
            y + 7,
            anchor="nw",
            fill=COLORS["window"],
            text=f"ContainerElement {group.name}",
            font=("Segoe UI", 12, "bold"),
            tags=(f"text_of_{item_id}", node_tag, "group_header", "draggable"),
        )

        body_top = y + header_height
        body_bottom = y + height
        center_x = x + width / 2

        canvas.create_rectangle(
            x + 1,
            body_top + 1,
            x + width - 1,
            body_bottom - 1,
            fill="#141a22",
            outline=COLORS["grid"],
            width=1,
            tags=(node_tag, "group_body"),
        )
        for grid_x in range(int(x + 18), int(x + width - 10), 18):
            canvas.create_line(grid_x, body_top + 4, grid_x, body_bottom - 4, fill="#18202a", width=1, tags=(node_tag, "group_body"))
        for grid_y in range(int(body_top + 8), int(body_bottom - 4), 18):
            canvas.create_line(x + 4, grid_y, x + width - 4, grid_y, fill="#18202a", width=1, tags=(node_tag, "group_body"))

        canvas.create_text(x + 10, body_top + 8, anchor="nw", fill=COLORS["muted"], text="IN", font=("Segoe UI", 10, "bold"), tags=(node_tag, "group_body"))
        canvas.create_text(x + width - 10, body_top + 8, anchor="ne", fill=COLORS["muted"], text="OUT", font=("Segoe UI", 10, "bold"), tags=(node_tag, "group_body"))

        canvas.create_oval(
            x + 10,
            body_top + 34,
            x + 22,
            body_top + 46,
            fill=COLORS["good"],
            outline=COLORS["good"],
            tags=(node_tag, "group_body", f"port:containerelement:{group.name}:in:in"),
        )
        canvas.create_oval(
            x + width - 22,
            body_top + 34,
            x + width - 10,
            body_top + 46,
            fill=COLORS["good"],
            outline=COLORS["good"],
            tags=(node_tag, "group_body", f"port:containerelement:{group.name}:out:out"),
        )
        self._register_port("containerelement", group.name, "in", "in", x + 16, body_top + 40)
        self._register_port("containerelement", group.name, "out", "out", x + width - 16, body_top + 40)

        handle_size = 10
        handle_id = canvas.create_rectangle(
            x + width - handle_size - 4,
            y + height - handle_size - 4,
            x + width - 4,
            y + height - 4,
            fill=COLORS["accent"],
            outline=COLORS["accent"],
            tags=(node_tag, "group_resize_handle", f"resize_handle:containerelement:{group.name}"),
        )
        canvas.tag_raise(handle_id)
        return item_id

    def _draw_grid(self, canvas: tk.Canvas, width: int, height: int) -> None:
        step = 48
        for x in range(0, width, step):
            canvas.create_line(x, 0, x, height, fill=COLORS["grid"], width=1, tags=("grid",))
        for y in range(0, height, step):
            canvas.create_line(0, y, width, y, fill=COLORS["grid"], width=1, tags=("grid",))

        canvas.create_text(24, 18, anchor="w", fill=COLORS["muted"], text="drag node headers, left-drag blank space to box-select, right-drag blank space to pan")
        canvas.create_text(width - 24, 18, anchor="e", fill=COLORS["muted"], text="right click a node to delete or duplicate")

    def _draw_node_card(
        self,
        canvas: tk.Canvas,
        kind: str,
        name: str,
        x: float,
        y: float,
        title: str,
        body: str,
        fill: str,
        selected: bool,
        width: float = NODE_WIDTH,
        height: float = NODE_HEIGHT,
    ) -> int:
        outline = COLORS["accent"] if selected else fill
        item_id = canvas.create_rectangle(
            x,
            y,
            x + width,
            y + height,
            fill=COLORS["panel_alt"],
            outline=outline,
            width=2,
            tags=(f"node:{kind}:{name}", "draggable"),
        )
        text_tags = (f"text_of_{item_id}", f"node:{kind}:{name}")
        title_font = ("Segoe UI", 11, "bold") if height <= 48 else ("Segoe UI", 12, "bold")
        body_font = ("Segoe UI", 8) if height <= 48 else ("Segoe UI", 10)
        title_y = y + 8 if height <= 48 else y + 12
        body_y = y + 24 if height <= 48 else y + 40
        canvas.create_text(x + 12, title_y, anchor="nw", fill=fill, text=title, font=title_font, tags=text_tags)
        if body:
            canvas.create_text(x + 12, body_y, anchor="nw", fill=COLORS["text"], text=body, font=body_font, tags=text_tags)
        return item_id

    def _draw_container_node(self, canvas: tk.Canvas, container: ContainerItem) -> int:
        x = container.x or CANVAS_PADDING + 40
        y = container.y or CANVAS_PADDING + 40
        fill = COLORS["container"] if container.kind == "variable" else COLORS["container_array"]
        title = f"{'v' if container.kind == 'variable' else 'a'} {container.name}"
        parent_group_name = self._container_parent_group_name(container.name)
        compact = parent_group_name is not None
        width = 118.0 if compact else float(NODE_WIDTH)
        height = 42.0 if compact else float(NODE_HEIGHT)
        body = f"count={container.count} stride={container.stride}" if not compact else f"c={container.count} s={container.stride}"
        item_id = self._draw_node_card(
            canvas,
            "container",
            container.name,
            x,
            y,
            title,
            body,
            fill,
            self.selected_container_name == container.name,
            width=width,
            height=height,
        )
        input_tag = f"port:container:{container.name}:in:in"
        input_x = x + 14
        input_y = y + height / 2
        canvas.create_oval(
            input_x - 6,
            input_y - 6,
            input_x + 6,
            input_y + 6,
            fill=fill,
            outline=fill,
            tags=(f"node:container:{container.name}", input_tag),
        )
        canvas.create_text(
            input_x + 12,
            input_y - 1,
            anchor="w",
            fill=COLORS["muted"],
            text="in",
            font=("Segoe UI", 9, "bold"),
            tags=(f"node:container:{container.name}", input_tag),
        )
        self._register_port("container", container.name, "in", "in", input_x, input_y)
        port_tag = f"port:container:{container.name}:out:out"
        port_x = x + width - 14
        port_y = y + height / 2
        canvas.create_oval(
            port_x - 6,
            port_y - 6,
            port_x + 6,
            port_y + 6,
            fill=fill,
            outline=fill,
            tags=(f"node:container:{container.name}", port_tag),
        )
        canvas.create_text(
            port_x - 12,
            port_y - 1,
            anchor="e",
            fill=COLORS["muted"],
            text="out",
            font=("Segoe UI", 9, "bold"),
            tags=(f"node:container:{container.name}", port_tag),
        )
        self._register_port("container", container.name, "out", "out", port_x, port_y)
        return item_id

    def _draw_decomposer_node(self, canvas: tk.Canvas, rule: DecomposerRule) -> int:
        x = rule.x or CANVAS_PADDING + 40
        y = rule.y or CANVAS_PADDING + 360
        inputs = ["in"]
        outputs = self.project.output_ports_for("decomposer", rule.name)
        descriptor_lines = self._decomposer_descriptor_lines(rule)
        resource_lines = self._decomposer_resource_lines(rule, outputs)
        return self._draw_decomposer_frame_node(
            canvas,
            rule.name,
            x,
            y,
            inputs,
            outputs,
            descriptor_lines,
            resource_lines,
            float(getattr(rule, "width", BLUEPRINT_NODE_WIDTH)),
            float(getattr(rule, "height", BLUEPRINT_NODE_MIN_HEIGHT)),
            self.selected_rule_name == rule.name,
        )

    def _decomposer_descriptor_lines(self, rule: DecomposerRule) -> list[str]:
        lines = [
            f"mapKind: {rule.map_kind or 'v2v'}",
            f"source: {rule.source or '-'}",
            f"target: {rule.target or '-'}",
        ]
        script = rule.descriptor_script.strip()
        if script:
            lines.append("descriptorScript:")
            lines.extend([f"  {line}" for line in script.splitlines()])
        else:
            lines.append("descriptorScript: -")
        return lines

    def _decomposer_resource_lines(self, rule: DecomposerRule, outputs: list[str]) -> list[str]:
        lines = [
            f"resourceMode: {rule.resource_mode or 'default'}",
            f"outputs: {', '.join(outputs) if outputs else '-'}",
        ]
        script = rule.resource_script.strip()
        if script:
            lines.append("resourceScript:")
            lines.extend([f"  {line}" for line in script.splitlines()])
        else:
            lines.append("resourceScript: -")
        schema_lines = self._schema_detail_lines(self.project.decomposer_res)
        if schema_lines:
            lines.append("resourceSchema:")
            lines.extend([f"  {line}" for line in schema_lines])
        else:
            lines.append("resourceSchema: {}")
        return lines

    def _draw_decomposer_frame_node(
        self,
        canvas: tk.Canvas,
        name: str,
        x: float,
        y: float,
        inputs: list[str],
        outputs: list[str],
        descriptor_lines: list[str],
        resource_lines: list[str],
        width: float,
        height: float,
        selected: bool,
    ) -> int:
        width = max(float(width), 360.0)
        descriptor_line_count = max(len(descriptor_lines), 1)
        resource_line_count = max(len(resource_lines), 1)
        output_count = max(len(outputs), 1)
        height = max(float(height), 260.0, 160.0 + max(descriptor_line_count, resource_line_count, output_count) * 18)
        outline = COLORS["accent"] if selected else COLORS["good"]
        node_tag = f"node:decomposer:{name}"
        item_id = canvas.create_rectangle(
            x,
            y,
            x + width,
            y + height,
            fill=COLORS["panel_alt"],
            outline=outline,
            width=2,
            tags=(node_tag, "node_body"),
        )

        header_height = 30
        canvas.create_rectangle(
            x,
            y,
            x + width,
            y + header_height,
            fill=COLORS["accent"],
            outline=outline,
            width=2,
            tags=(node_tag, "node_header", "draggable"),
        )
        canvas.create_text(
            x + 12,
            y + 7,
            anchor="nw",
            fill=COLORS["window"],
            text=f"Decomposer {name}",
            font=("Segoe UI", 12, "bold"),
            tags=(f"text_of_{item_id}", node_tag, "node_header", "draggable"),
        )

        body_top = y + header_height
        body_bottom = y + height
        left_w = 106
        right_w = 140
        center_x = x + left_w
        right_x = x + width - right_w
        middle_left = center_x + 1
        middle_right = right_x - 1
        middle_width = max(middle_right - middle_left, 1)
        middle_top = body_top + 1
        middle_bottom = body_bottom - 1
        split_y = middle_top + max(72.0, (middle_bottom - middle_top) * 0.45)
        split_y = min(split_y, middle_bottom - 72.0)

        canvas.create_rectangle(
            x + 1,
            body_top + 1,
            center_x - 1,
            body_bottom - 1,
            fill="#161b22",
            outline=COLORS["grid"],
            width=1,
            tags=(node_tag, "node_body"),
        )
        canvas.create_rectangle(
            middle_left,
            middle_top,
            middle_right,
            split_y - 1,
            fill=COLORS["canvas"],
            outline=COLORS["grid"],
            width=1,
            tags=(node_tag, "node_body"),
        )
        canvas.create_rectangle(
            middle_left,
            split_y + 1,
            middle_right,
            middle_bottom,
            fill="#161b22",
            outline=COLORS["grid"],
            width=1,
            tags=(node_tag, "node_body"),
        )
        canvas.create_rectangle(
            right_x + 1,
            body_top + 1,
            x + width - 1,
            body_bottom - 1,
            fill="#161b22",
            outline=COLORS["grid"],
            width=1,
            tags=(node_tag, "node_body"),
        )
        canvas.create_line(
            middle_left,
            split_y,
            middle_right,
            split_y,
            fill=COLORS["grid"],
            width=1,
            tags=(node_tag, "node_body"),
        )

        canvas.create_text(x + 14, body_top + 8, anchor="nw", fill=COLORS["muted"], text="IN", font=("Segoe UI", 10, "bold"), tags=(node_tag,))
        canvas.create_text(x + width - 14, body_top + 8, anchor="ne", fill=COLORS["muted"], text="OUT", font=("Segoe UI", 10, "bold"), tags=(node_tag,))
        canvas.create_text(middle_left + 8, middle_top + 8, anchor="nw", fill=COLORS["muted"], text="DESCRIPTOR", font=("Segoe UI", 10, "bold"), tags=(node_tag,))
        canvas.create_text(middle_left + 8, split_y + 8, anchor="nw", fill=COLORS["muted"], text="RESOURCE", font=("Segoe UI", 10, "bold"), tags=(node_tag,))

        input_ports = inputs or ["in"]
        for index, port_name in enumerate(input_ports):
            port_y = body_top + 38 + index * 24
            port_tag = f"port:decomposer:{name}:in:{port_name}"
            canvas.create_oval(
                x + 10,
                port_y,
                x + 22,
                port_y + 12,
                fill=COLORS["accent"],
                outline=COLORS["accent"],
                tags=(node_tag, port_tag),
            )
            canvas.create_text(
                x + 28,
                port_y - 1,
                anchor="nw",
                fill=COLORS["text"],
                text=port_name,
                font=("Segoe UI", 10),
                width=left_w - 34,
                justify="left",
                tags=(f"text_of_{item_id}", node_tag, port_tag),
            )
            self._register_port("decomposer", name, "in", port_name, x + 16, port_y + 6)

        descriptor_text = "\n".join(descriptor_lines) if descriptor_lines else "-"
        resource_text = "\n".join(resource_lines) if resource_lines else "-"
        canvas.create_text(
            middle_left + 10,
            middle_top + 30,
            anchor="nw",
            fill=COLORS["text"],
            text=descriptor_text,
            font=("Segoe UI", 10),
            width=middle_width - 20,
            justify="left",
            tags=(f"text_of_{item_id}", node_tag, "node_body"),
        )
        canvas.create_text(
            middle_left + 10,
            split_y + 30,
            anchor="nw",
            fill=COLORS["text"],
            text=resource_text,
            font=("Segoe UI", 10),
            width=middle_width - 20,
            justify="left",
            tags=(f"text_of_{item_id}", node_tag, "node_body"),
        )

        output_ports = outputs or ["out"]
        for index, output in enumerate(output_ports):
            port_y = body_top + 38 + index * 24
            port_tag = f"port:decomposer:{name}:out:{output}"
            canvas.create_oval(
                right_x + 10,
                port_y,
                right_x + 22,
                port_y + 12,
                fill=COLORS["accent"],
                outline=COLORS["accent"],
                tags=(node_tag, port_tag),
            )
            canvas.create_text(
                right_x + 28,
                port_y - 1,
                anchor="nw",
                fill=COLORS["text"],
                text=output,
                font=("Segoe UI", 10),
                width=right_w - 36,
                justify="left",
                tags=(f"text_of_{item_id}", node_tag, port_tag),
            )
            self._register_port("decomposer", name, "out", output, right_x + 16, port_y + 6)

        handle_size = 10
        handle_id = canvas.create_rectangle(
            x + width - handle_size - 4,
            y + height - handle_size - 4,
            x + width - 4,
            y + height - 4,
            fill=COLORS["accent"],
            outline=COLORS["accent"],
            tags=(node_tag, "node_resize_handle", f"resize_handle:decomposer:{name}"),
        )
        canvas.tag_raise(handle_id)
        return item_id

    def _draw_reflector_node(self, canvas: tk.Canvas, item: ReflectorItem) -> int:
        x = item.x or CANVAS_PADDING + 40
        y = item.y or CANVAS_PADDING + 180
        inputs = ["in"]
        outputs = list(item.direct_to) if item.direct_to else ([item.output_name] if item.output_name else [item.output_kind or "out"])
        script_lines = [item.reflect_fun]
        return self._draw_blueprint_node(
            canvas,
            "reflector",
            item.name,
            x,
            y,
            f"Reflector {item.name}",
            inputs,
            outputs,
            script_lines,
            float(getattr(item, "width", BLUEPRINT_NODE_WIDTH)),
            float(getattr(item, "height", BLUEPRINT_NODE_MIN_HEIGHT)),
            COLORS["good"],
            self.selected_reflector_name == item.name,
        )

    def _draw_res_node(self, canvas: tk.Canvas, item: ResourceNodeItem) -> int:
        x = item.x or CANVAS_PADDING + 40
        y = item.y or CANVAS_PADDING + 420
        resource_types = list(item.resource_types) if item.resource_types else list(item.outputs)
        if not resource_types:
            resource_types = [item.resource_kind or "mesh"]
        outputs = list(item.outputs) if item.outputs else list(resource_types)
        if len(outputs) < len(resource_types):
            outputs.extend(resource_types[len(outputs):])
        width = max(float(getattr(item, "width", BLUEPRINT_NODE_WIDTH)), 360.0)
        row_count = max(len(resource_types), len(outputs), 1)
        height = max(float(getattr(item, "height", BLUEPRINT_NODE_MIN_HEIGHT)), 152.0 + row_count * 24.0)
        outline = COLORS["accent"] if self.selected_res_node_name == item.name else COLORS["agent"]
        node_tag = f"node:resnode:{item.name}"
        item_id = canvas.create_rectangle(
            x,
            y,
            x + width,
            y + height,
            fill=COLORS["panel_alt"],
            outline=outline,
            width=2,
            tags=(node_tag, "node_body"),
        )

        header_height = 30
        canvas.create_rectangle(
            x,
            y,
            x + width,
            y + header_height,
            fill=COLORS["agent"],
            outline=outline,
            width=2,
            tags=(node_tag, "node_header", "draggable"),
        )
        canvas.create_text(
            x + 12,
            y + 7,
            anchor="nw",
            fill=COLORS["window"],
            text=f"resNode {item.name}",
            font=("Segoe UI", 12, "bold"),
            tags=(f"text_of_{item_id}", node_tag, "node_header", "draggable"),
        )

        body_top = y + header_height
        body_bottom = y + height
        left_w = 92
        right_w = 132
        center_x = x + left_w
        right_x = x + width - right_w
        middle_left = center_x + 1
        middle_right = right_x - 1
        middle_width = max(middle_right - middle_left, 1)

        canvas.create_rectangle(
            x + 1,
            body_top + 1,
            center_x - 1,
            body_bottom - 1,
            fill="#161b22",
            outline=COLORS["grid"],
            width=1,
            tags=(node_tag, "node_body"),
        )
        canvas.create_rectangle(
            middle_left,
            body_top + 1,
            middle_right,
            body_bottom - 1,
            fill=COLORS["canvas"],
            outline=COLORS["grid"],
            width=1,
            tags=(node_tag, "node_body"),
        )
        canvas.create_rectangle(
            right_x + 1,
            body_top + 1,
            x + width - 1,
            body_bottom - 1,
            fill="#161b22",
            outline=COLORS["grid"],
            width=1,
            tags=(node_tag, "node_body"),
        )

        canvas.create_text(x + 14, body_top + 8, anchor="nw", fill=COLORS["muted"], text="IN", font=("Segoe UI", 10, "bold"), tags=(node_tag,))
        canvas.create_text(x + width - 14, body_top + 8, anchor="ne", fill=COLORS["muted"], text="OUT", font=("Segoe UI", 10, "bold"), tags=(node_tag,))
        canvas.create_text(middle_left + 8, body_top + 8, anchor="nw", fill=COLORS["muted"], text="RESOURCES", font=("Segoe UI", 10, "bold"), tags=(node_tag,))

        input_y = body_top + 38
        input_tag = f"port:resnode:{item.name}:in:in"
        canvas.create_oval(
            x + 10,
            input_y,
            x + 22,
            input_y + 12,
            fill=COLORS["agent"],
            outline=COLORS["agent"],
            tags=(node_tag, input_tag),
        )
        canvas.create_text(
            x + 28,
            input_y - 1,
            anchor="nw",
            fill=COLORS["text"],
            text="in",
            font=("Segoe UI", 10),
            width=left_w - 34,
            justify="left",
            tags=(f"text_of_{item_id}", node_tag, input_tag),
        )
        self._register_port("resnode", item.name, "in", "in", x + 16, input_y + 6)

        start_y = body_top + 34
        row_step = 24
        row_total = max(len(resource_types), len(outputs))
        for index in range(row_total):
            resource_name = resource_types[index] if index < len(resource_types) else outputs[index]
            row_y = start_y + index * row_step
            port_name = outputs[index] if index < len(outputs) else resource_name
            canvas.create_rectangle(
                middle_left + 10,
                row_y - 2,
                middle_left + 88,
                row_y + 16,
                fill=COLORS["agent"],
                outline=COLORS["agent"],
                tags=(node_tag, f"resource_node:{item.name}:{resource_name}"),
            )
            canvas.create_text(
                middle_left + 18,
                row_y,
                anchor="nw",
                fill=COLORS["window"],
                text=resource_name,
                font=("Segoe UI", 10, "bold"),
                width=middle_width - 26,
                justify="left",
                tags=(f"text_of_{item_id}", node_tag, f"resource_node:{item.name}:{resource_name}"),
            )
            port_tag = f"port:resnode:{item.name}:out:{port_name}"
            canvas.create_oval(
                right_x + 10,
                row_y,
                right_x + 22,
                row_y + 12,
                fill=COLORS["agent"],
                outline=COLORS["agent"],
                tags=(node_tag, port_tag),
            )
            canvas.create_text(
                right_x + 28,
                row_y - 1,
                anchor="nw",
                fill=COLORS["text"],
                text=port_name,
                font=("Segoe UI", 10),
                width=right_w - 36,
                justify="left",
                tags=(f"text_of_{item_id}", node_tag, port_tag),
            )
            self._register_port("resnode", item.name, "out", port_name, right_x + 16, row_y + 6)

        handle_size = 10
        handle_id = canvas.create_rectangle(
            x + width - handle_size - 4,
            y + height - handle_size - 4,
            x + width - 4,
            y + height - 4,
            fill=COLORS["accent"],
            outline=COLORS["accent"],
            tags=(node_tag, "node_resize_handle", f"resize_handle:resnode:{item.name}"),
        )
        canvas.tag_raise(handle_id)
        return item_id

    def _draw_function_frame_node(self, canvas: tk.Canvas, item: FunctionFrameItem) -> int:
        x = item.x or CANVAS_PADDING + 40
        y = item.y or CANVAS_PADDING + 520
        inputs = [item.input_name or "in"]
        outputs = [item.output_name or "out"]
        script_lines = [line for line in (item.script or "script").splitlines() if line.strip()]
        if not script_lines:
            script_lines = ["script"]
        return self._draw_blueprint_node(
            canvas,
            "function",
            item.name,
            x,
            y,
            f"Function {item.name}",
            inputs,
            outputs,
            script_lines,
            float(getattr(item, "width", BLUEPRINT_NODE_WIDTH)),
            float(getattr(item, "height", BLUEPRINT_NODE_MIN_HEIGHT)),
            COLORS["good"],
            self.selected_function_name == item.name,
        )

    def _draw_stage_node(self, canvas: tk.Canvas, stage: InterventionStage) -> int:
        x = stage.x or CANVAS_PADDING + 40
        y = stage.y or CANVAS_PADDING + 280
        inputs = ["in"]
        outputs = stage.functions or [stage.kind]
        script_lines = [stage.shader_vertex or stage.shader_fragment or "script"]
        return self._draw_blueprint_node(
            canvas,
            "interventioner",
            stage.name,
            x,
            y,
            f"Interventioner {stage.name}",
            inputs,
            outputs,
            script_lines,
            float(getattr(stage, "width", BLUEPRINT_NODE_WIDTH)),
            float(getattr(stage, "height", BLUEPRINT_NODE_MIN_HEIGHT)),
            COLORS["accent_2"],
            self.selected_stage_name == stage.name,
        )

    def _draw_blueprint_node(
        self,
        canvas: tk.Canvas,
        kind: str,
        name: str,
        x: float,
        y: float,
        title: str,
        inputs: list[str],
        outputs: list[str],
        script_lines: list[str],
        width: float,
        height: float,
        fill: str,
        selected: bool,
    ) -> int:
        width = max(float(width), 320.0)
        content_height = 72 + max(len(inputs), len(outputs), len(script_lines), 1) * 24
        height = max(float(height), BLUEPRINT_NODE_MIN_HEIGHT, content_height)
        outline = COLORS["accent"] if selected else fill
        node_tag = f"node:{kind}:{name}"

        item_id = canvas.create_rectangle(
            x,
            y,
            x + width,
            y + height,
            fill=COLORS["panel_alt"],
            outline=outline,
            width=2,
            tags=(node_tag, "node_body"),
        )

        header_height = 30
        canvas.create_rectangle(
            x,
            y,
            x + width,
            y + header_height,
            fill=fill,
            outline=outline,
            width=2,
            tags=(node_tag, "node_header", "draggable"),
        )
        canvas.create_text(
            x + 12,
            y + 7,
            anchor="nw",
            fill=COLORS["window"],
            text=title,
            font=("Segoe UI", 12, "bold"),
            tags=(f"text_of_{item_id}", node_tag, "node_header", "draggable"),
        )

        body_top = y + header_height
        body_bottom = y + height
        left_w = 106
        right_w = 124
        center_x = x + left_w
        right_x = x + width - right_w

        canvas.create_rectangle(
            x + 1,
            body_top + 1,
            center_x - 1,
            body_bottom - 1,
            fill="#161b22",
            outline=COLORS["grid"],
            width=1,
            tags=(node_tag, "node_body"),
        )
        canvas.create_rectangle(
            center_x + 1,
            body_top + 1,
            right_x - 1,
            body_bottom - 1,
            fill=COLORS["canvas"],
            outline=COLORS["grid"],
            width=1,
            tags=(node_tag, "node_body"),
        )
        canvas.create_rectangle(
            right_x + 1,
            body_top + 1,
            x + width - 1,
            body_bottom - 1,
            fill="#161b22",
            outline=COLORS["grid"],
            width=1,
            tags=(node_tag, "node_body"),
        )

        for grid_x in range(int(center_x + 12), int(right_x - 8), 18):
            canvas.create_line(grid_x, body_top + 4, grid_x, body_bottom - 4, fill="#18202a", width=1, tags=(node_tag, "node_body"))
        for grid_y in range(int(body_top + 8), int(body_bottom - 4), 18):
            canvas.create_line(center_x + 4, grid_y, right_x - 4, grid_y, fill="#18202a", width=1, tags=(node_tag, "node_body"))

        canvas.create_text(x + 14, body_top + 8, anchor="nw", fill=COLORS["muted"], text="进", font=("Segoe UI", 10, "bold"), tags=(node_tag,))
        canvas.create_text(x + width - 14, body_top + 8, anchor="ne", fill=COLORS["muted"], text="出", font=("Segoe UI", 10, "bold"), tags=(node_tag,))
        canvas.create_text(center_x + 8, body_top + 8, anchor="nw", fill=COLORS["muted"], text="本体", font=("Segoe UI", 10, "bold"), tags=(node_tag,))

        inputs_text = "\n".join(inputs) if inputs else "-"
        outputs_text = "\n".join(outputs) if outputs else "-"
        script_text = "\n".join(script_lines) if script_lines else "-"

        canvas.create_text(
            x + 14,
            body_top + 28,
            anchor="nw",
            fill=COLORS["text"],
            text=inputs_text,
            font=("Segoe UI", 10),
            width=left_w - 20,
            justify="left",
            tags=(f"text_of_{item_id}", node_tag, "node_body"),
        )
        canvas.create_text(
            center_x + 10,
            body_top + 28,
            anchor="nw",
            fill=COLORS["text"],
            text=script_text,
            font=("Segoe UI", 10),
            width=width - left_w - right_w - 24,
            justify="left",
            tags=(f"text_of_{item_id}", node_tag, "node_body"),
        )
        canvas.create_text(
            right_x + 10,
            body_top + 28,
            anchor="nw",
            fill=COLORS["text"],
            text=outputs_text,
            font=("Segoe UI", 10),
            width=right_w - 18,
            justify="left",
            tags=(f"text_of_{item_id}", node_tag, "node_body"),
        )
        handle_size = 10
        handle_id = canvas.create_rectangle(
            x + width - handle_size - 4,
            y + height - handle_size - 4,
            x + width - 4,
            y + height - 4,
            fill=COLORS["accent"],
            outline=COLORS["accent"],
            tags=(node_tag, "node_resize_handle", f"resize_handle:{kind}:{name}"),
        )
        canvas.tag_raise(handle_id)
        return item_id

    def _draw_blueprint_node(
        self,
        canvas: tk.Canvas,
        kind: str,
        name: str,
        x: float,
        y: float,
        title: str,
        inputs: list[str],
        outputs: list[str],
        script_lines: list[str],
        width: float,
        height: float,
        fill: str,
        selected: bool,
    ) -> int:
        width = max(float(width), 320.0)
        input_count = max(len(inputs), 1)
        output_count = max(len(outputs), 1)
        script_count = max(len(script_lines), 1)
        port_rows = max(input_count, output_count)
        height = max(float(height), BLUEPRINT_NODE_MIN_HEIGHT, 92 + max(port_rows, script_count) * 24)
        outline = COLORS["accent"] if selected else fill
        node_tag = f"node:{kind}:{name}"

        item_id = canvas.create_rectangle(
            x,
            y,
            x + width,
            y + height,
            fill=COLORS["panel_alt"],
            outline=outline,
            width=2,
            tags=(node_tag, "node_body"),
        )

        header_height = 30
        canvas.create_rectangle(
            x,
            y,
            x + width,
            y + header_height,
            fill=fill,
            outline=outline,
            width=2,
            tags=(node_tag, "node_header", "draggable"),
        )
        canvas.create_text(
            x + 12,
            y + 7,
            anchor="nw",
            fill=COLORS["window"],
            text=title,
            font=("Segoe UI", 12, "bold"),
            tags=(f"text_of_{item_id}", node_tag, "node_header", "draggable"),
        )

        body_top = y + header_height
        body_bottom = y + height
        left_w = 106
        right_w = 124
        center_x = x + left_w
        right_x = x + width - right_w

        canvas.create_rectangle(
            x + 1,
            body_top + 1,
            center_x - 1,
            body_bottom - 1,
            fill="#161b22",
            outline=COLORS["grid"],
            width=1,
            tags=(node_tag, "node_body"),
        )
        canvas.create_rectangle(
            center_x + 1,
            body_top + 1,
            right_x - 1,
            body_bottom - 1,
            fill=COLORS["canvas"],
            outline=COLORS["grid"],
            width=1,
            tags=(node_tag, "node_body"),
        )
        canvas.create_rectangle(
            right_x + 1,
            body_top + 1,
            x + width - 1,
            body_bottom - 1,
            fill="#161b22",
            outline=COLORS["grid"],
            width=1,
            tags=(node_tag, "node_body"),
        )

        for grid_x in range(int(center_x + 12), int(right_x - 8), 18):
            canvas.create_line(grid_x, body_top + 4, grid_x, body_bottom - 4, fill="#18202a", width=1, tags=(node_tag, "node_body"))
        for grid_y in range(int(body_top + 8), int(body_bottom - 4), 18):
            canvas.create_line(center_x + 4, grid_y, right_x - 4, grid_y, fill="#18202a", width=1, tags=(node_tag, "node_body"))

        script_text = "\n".join(script_lines) if script_lines else "-"

        canvas.create_text(
            x + 10,
            body_top + 10,
            anchor="nw",
            fill=COLORS["muted"],
            text="IN",
            font=("Segoe UI", 10, "bold"),
            tags=(node_tag,),
        )
        input_ports = inputs or ["in"]
        for index, port_name in enumerate(input_ports):
            port_y = body_top + 34 + index * 24
            port_tag = f"port:{kind}:{name}:in:{port_name}"
            canvas.create_oval(
                x + 10,
                port_y,
                x + 22,
                port_y + 12,
                fill=fill,
                outline=fill,
                tags=(node_tag, port_tag),
            )
            canvas.create_text(
                x + 28,
                port_y - 1,
                anchor="nw",
                fill=COLORS["text"],
                text=port_name,
                font=("Segoe UI", 10),
                width=left_w - 34,
                justify="left",
                tags=(f"text_of_{item_id}", node_tag, port_tag),
            )
            self._register_port(kind, name, "in", port_name, x + 16, port_y + 6)

        canvas.create_text(
            center_x + 10,
            body_top + 10,
            anchor="nw",
            fill=COLORS["muted"],
            text="SCRIPT",
            font=("Segoe UI", 10, "bold"),
            tags=(node_tag,),
        )
        canvas.create_text(
            center_x + 10,
            body_top + 32,
            anchor="nw",
            fill=COLORS["text"],
            text=script_text,
            font=("Segoe UI", 10),
            width=width - left_w - right_w - 20,
            justify="left",
            tags=(f"text_of_{item_id}", node_tag),
        )

        canvas.create_text(
            x + width - 10,
            body_top + 10,
            anchor="ne",
            fill=COLORS["muted"],
            text="OUT",
            font=("Segoe UI", 10, "bold"),
            tags=(node_tag,),
        )
        output_ports = outputs or ["out"]
        for index, output in enumerate(output_ports):
            output_y = body_top + 34 + index * 24
            port_tag = f"port:{kind}:{name}:out:{output}"
            canvas.create_oval(
                right_x + 10,
                output_y,
                right_x + 22,
                output_y + 12,
                fill=fill,
                outline=fill,
                tags=(node_tag, port_tag),
            )
            canvas.create_text(
                right_x + 28,
                output_y - 1,
                anchor="nw",
                fill=COLORS["text"],
                text=output,
                font=("Segoe UI", 10),
                width=right_w - 36,
                justify="left",
                tags=(f"text_of_{item_id}", node_tag, port_tag),
            )
            self._register_port(kind, name, "out", output, right_x + 16, output_y + 6)
        handle_size = 10
        handle_id = canvas.create_rectangle(
            x + width - handle_size - 4,
            y + height - handle_size - 4,
            x + width - 4,
            y + height - 4,
            fill=COLORS["accent"],
            outline=COLORS["accent"],
            tags=(node_tag, "node_resize_handle", f"resize_handle:{kind}:{name}"),
        )
        canvas.tag_raise(handle_id)
        return item_id

    def _log(self, message: str) -> None:
        self.log_lines.append(message)
        if self.log_text:
            self.log_text.insert(tk.END, message + "\n")
            self.log_text.see(tk.END)
        self.status_var.set(message)


def main() -> None:
    root = tk.Tk()
    app = AlgorithmStudioApp(root)
    root.after(50, app._redraw_canvas)
    root.mainloop()


if __name__ == "__main__":
    main()
