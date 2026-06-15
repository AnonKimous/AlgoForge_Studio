#!/usr/bin/env python3

from __future__ import annotations

import copy
import json
from dataclasses import dataclass, field
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog, ttk
from typing import Any


PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_TEMPLATE_PATH = PROJECT_ROOT / "src" / "capabilities" / "algorithm_library" / "algorithm_package_example.json"

NODE_WIDTH = 180
NODE_HEIGHT = 76
SPECIAL_CARD_WIDTH = 210
SPECIAL_CARD_HEIGHT = 108
CANVAS_PADDING = 24

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


from backend import (
    ContainerItem,
    DecomposerRule,
    DEFAULT_TEMPLATE_PATH,
    InterventionStage,
    MockAgentClient,
    ProjectState,
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
        self.selected_stage_name: str | None = None
        self.canvas_nodes: dict[str, int] = {}
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
        self.stage_kind_var = tk.StringVar()
        self.stage_functions_var = tk.StringVar()
        self.stage_shader_vertex_var = tk.StringVar()
        self.stage_shader_fragment_var = tk.StringVar()
        self.stage_used_vars_var = tk.StringVar()
        self.stage_used_arrays_var = tk.StringVar()
        self.provider_var = tk.StringVar(value="mock")
        self.model_var = tk.StringVar(value="mock")
        self.base_url_var = tk.StringVar()
        self.api_key_var = tk.StringVar()
        self.agent_prompt_var = tk.StringVar(value="Generate a container layout and package skeleton for the selected algorithm.")
        self.agent_output_var = tk.StringVar()
        self.status_var = tk.StringVar(value="Ready.")
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
        main.rowconfigure(0, weight=1)

        self._build_palette_panel(main)
        self._build_canvas_panel(main)

    def _build_palette_panel(self, parent: ttk.Frame) -> None:
        palette = ttk.Frame(parent, padding=(0, 0, 12, 0))
        palette.grid(row=0, column=0, sticky="ns")
        palette.columnconfigure(0, weight=1)
        self.root.bind_all("<ButtonRelease-1>", self._finish_palette_drag, add="+")

        title = ttk.Label(palette, text="Drag Palette")
        title.grid(row=0, column=0, sticky="w", pady=(0, 8))

        hint = ttk.Label(palette, text="Drag blocks into the canvas", foreground=COLORS["muted"])
        hint.grid(row=1, column=0, sticky="w", pady=(0, 12))

        self._create_palette_group(
            palette,
            2,
            "Containers",
            [
                ("variable", "v", "Variable"),
                ("array", "a", "Array"),
            ],
        )
        self._create_palette_group(
            palette,
            3,
            "Blocks",
            [
                ("reflector", "R", "Reflector"),
                ("stage", "S", "Stage"),
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
            self._refresh_all()
            self._log(f"Added reflector {name}.")
            return
        if kind == "stage":
            name = self.project.next_stage_name()
            self.project.intervention_stages.append(
                InterventionStage(
                    name=name,
                    kind="stage",
                    x=x,
                    y=y,
                )
            )
            self.selected_stage_name = name
            self.selected_container_name = None
            self.selected_reflector_name = None
            self._refresh_all()
            self._log(f"Added stage {name}.")

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
        self.canvas.bind("<Button-3>", self._on_canvas_right_click)
        self.canvas.bind("<B1-Motion>", self._on_canvas_drag)
        self.canvas.bind("<ButtonRelease-1>", self._on_canvas_release)
        self.canvas.bind("<Configure>", lambda _event: self._redraw_canvas())

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
        self.selected_container_name = None
        self.selected_rule_name = None
        self.selected_reflector_name = None
        self.selected_stage_name = None
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

    def _generate_cpp_skeleton(self) -> str:
        return generate_cpp_skeleton(self.project.algorithm_name)

    def _refresh_all(self) -> None:
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
            self.rule_list.insert(tk.END, f"{rule.name}: {rule.source} -> {rule.target}")

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
            self.stage_list.insert(tk.END, f"{stage.name} ({stage.kind})")

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
                return "\n".join(
                    [
                        "Selected decomposer rule",
                        f"name: {rule.name}",
                        f"source: {rule.source}",
                        f"target: {rule.target}",
                    ]
                )
        if self.selected_reflector_name:
            item = self._find_reflector(self.selected_reflector_name)
            if item:
                return "\n".join(
                    [
                        "Selected reflector item",
                        f"name: {item.name}",
                        f"filter: {item.reflect_fun}",
                        f"inputs.varity: {', '.join(item.inputs_varity) or '-'}",
                        f"inputs.array: {', '.join(item.inputs_array) or '-'}",
                        f"output: {item.output_kind}:{item.output_name or '-'}",
                    ]
                )
        if self.selected_stage_name:
            stage = self._find_stage(self.selected_stage_name)
            if stage:
                return "\n".join(
                    [
                        "Selected intervention stage",
                        f"name: {stage.name}",
                        f"kind: {stage.kind}",
                        f"functions: {', '.join(stage.functions) or '-'}",
                        f"variables: {', '.join(stage.used_variables) or '-'}",
                        f"arrays: {', '.join(stage.used_arrays) or '-'}",
                        f"shader.vertex: {stage.shader_vertex or '-'}",
                        f"shader.fragment: {stage.shader_fragment or '-'}",
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

    def _find_stage(self, name: str) -> InterventionStage | None:
        for stage in self.project.intervention_stages:
            if stage.name == name:
                return stage
        return None

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
        self.project.containers = [item for item in self.project.containers if item.name != container.name]
        self.project.decomposer_rules = [
            rule for rule in self.project.decomposer_rules if rule.source != container.name and rule.target != container.name
        ]
        self.project.reflector_items = [
            item for item in self.project.reflector_items
            if container.name not in item.direct_from
            and container.name not in item.direct_to
            and container.name not in item.inputs_varity
            and container.name not in item.inputs_array
            and item.output_name != container.name
        ]
        self.project.intervention_stages = [
            stage for stage in self.project.intervention_stages
            if container.name not in stage.used_variables and container.name not in stage.used_arrays
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
        self.selected_reflector_name = None
        self._refresh_all()
        self._log(f"Deleted reflector item {item.name}.")

    def _add_stage(self) -> None:
        name = self.project.next_stage_name()
        stage = InterventionStage(name=name, kind="resultRender")
        self.project.intervention_stages.append(stage)
        self.selected_stage_name = stage.name
        self._refresh_all()
        self._log(f"Added stage {stage.name}.")

    def _add_or_update_stage(self) -> None:
        name = self.stage_name_var.get().strip() or self.project.next_stage_name()
        kind = self.stage_kind_var.get().strip() or "resultRender"
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
        self.selected_stage_name = None
        self._refresh_all()
        self._log(f"Deleted stage {stage.name}.")

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
        summary = self._selected_item_summary()
        prompt = self.agent_prompt_var.get().strip() or "Analyze the current algorithm structure and propose a patch."
        response = self.agent_client.generate(self.project, summary.splitlines()[0], prompt + "\n\nContext:\n" + summary)
        if self.agent_output_box:
            self.agent_output_box.delete("1.0", tk.END)
            self.agent_output_box.insert("1.0", response)
        self.agent_output_var.set(response)
        self._log("Agent call completed.")

    def _fill_agent_prompt_from_selection(self) -> None:
        summary = self._selected_item_summary()
        prompt = "\n".join(
            [
                "Task:",
                "Generate a safe package-level change for the selected item.",
                "",
                "Context:",
                summary,
            ]
        )
        if self.agent_text:
            self.agent_text.delete("1.0", tk.END)
            self.agent_text.insert("1.0", prompt)
        self.agent_prompt_var.set(prompt)
        self._log("Agent prompt filled from current selection.")

    def _send_agent_prompt(self) -> None:
        if not self.agent_text:
            return
        prompt = self.agent_text.get("1.0", tk.END).strip()
        self.agent_prompt_var.set(prompt)
        self.agent_client.provider = self.provider_var.get().strip() or "mock"
        self.agent_client.model = self.model_var.get().strip() or "mock"
        self.agent_client.base_url = self.base_url_var.get().strip()
        self.agent_client.api_key = self.api_key_var.get().strip()
        response = self.agent_client.generate(self.project, self._selection_label(), prompt)
        if self.agent_output_box:
            self.agent_output_box.delete("1.0", tk.END)
            self.agent_output_box.insert("1.0", response)
        self._log(f"Sent prompt to provider {self.agent_client.provider}.")

    def _selection_label(self) -> str:
        if self.selected_container_name:
            return f"container:{self.selected_container_name}"
        if self.selected_rule_name:
            return f"rule:{self.selected_rule_name}"
        if self.selected_reflector_name:
            return f"reflector:{self.selected_reflector_name}"
        if self.selected_stage_name:
            return f"stage:{self.selected_stage_name}"
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
        reflector = self._find_reflector(name)
        stage = self._find_stage(name)
        self.selected_rule_name = None
        if kind == "container" and container:
            self.selected_container_name = container.name
            self.selected_reflector_name = None
            self.selected_stage_name = None
        elif kind == "reflector" and reflector:
            self.selected_reflector_name = reflector.name
            self.selected_container_name = None
            self.selected_stage_name = None
        elif kind == "stage" and stage:
            self.selected_stage_name = stage.name
            self.selected_container_name = None
            self.selected_reflector_name = None
        self._redraw_canvas()
        self._refresh_inspector()

    def _on_canvas_click(self, event: tk.Event) -> None:
        if not self.canvas:
            return
        item = self.canvas.find_closest(event.x, event.y)
        if not item:
            self.selected_container_name = None
            self.selected_rule_name = None
            self.selected_reflector_name = None
            self.selected_stage_name = None
            self._refresh_inspector()
            self._redraw_canvas()
            return
        tags = self.canvas.gettags(item[0])
        kind, node_name = self._node_info_from_tags(tags)
        if kind and node_name:
            self._select_item_on_canvas(kind, node_name)
            self.node_drag_state = {
                "kind": kind,
                "name": node_name,
                "x": event.x,
                "y": event.y,
            }
        else:
            self.node_drag_state = None

    def _on_canvas_drag(self, event: tk.Event) -> None:
        if not self.canvas or not self.node_drag_state:
            return
        dx = event.x - self.node_drag_state["x"]
        dy = event.y - self.node_drag_state["y"]
        kind = str(self.node_drag_state["kind"])
        name = str(self.node_drag_state["name"])
        self.canvas.move(f"node:{kind}:{name}", dx, dy)
        self.node_drag_state["x"] = event.x
        self.node_drag_state["y"] = event.y
        if kind == "container":
            container = self._find_container(name)
            if container:
                container.x += dx
                container.y += dy
        elif kind == "reflector":
            item = self._find_reflector(name)
            if item:
                item.x += dx
                item.y += dy
        elif kind == "stage":
            stage = self._find_stage(name)
            if stage:
                stage.x += dx
                stage.y += dy

    def _on_canvas_release(self, _event: tk.Event) -> None:
        self.node_drag_state = None

    def _on_canvas_right_click(self, event: tk.Event) -> None:
        if not self.canvas:
            return
        item = self.canvas.find_closest(event.x, event.y)
        if not item:
            return
        tags = self.canvas.gettags(item[0])
        kind, node_name = self._node_info_from_tags(tags)
        if not kind or not node_name:
            return
        self._select_item_on_canvas(kind, node_name)
        menu = tk.Menu(self.root, tearoff=0)
        if kind == "container":
            menu.add_command(label="Duplicate", command=self._duplicate_selected_container)
            menu.add_separator()
            menu.add_command(label="Delete", command=self._delete_selected_container)
        elif kind == "reflector":
            menu.add_command(label="Delete", command=self._delete_selected_reflector)
        elif kind == "stage":
            menu.add_command(label="Delete", command=self._delete_selected_stage)
        menu.tk_popup(event.x_root, event.y_root)

    def _node_name_from_tags(self, tags: tuple[str, ...]) -> str | None:
        kind, name = self._node_info_from_tags(tags)
        if kind and name:
            return name
        return None

    def _node_info_from_tags(self, tags: tuple[str, ...]) -> tuple[str | None, str | None]:
        for tag in tags:
            if tag.startswith("node:"):
                parts = tag.split(":", 2)
                if len(parts) == 3:
                    return parts[1], parts[2]
        return None, None

    def _redraw_canvas(self) -> None:
        if not self.canvas:
            return
        canvas = self.canvas
        canvas.delete("all")

        width = max(canvas.winfo_width(), 1)
        height = max(canvas.winfo_height(), 1)
        self._draw_grid(canvas, width, height)
        self._draw_edges(canvas)
        self.canvas_nodes.clear()
        self.canvas_item_to_name.clear()
        for container in self.project.containers:
            item_id = self._draw_container_node(canvas, container)
            self.canvas_nodes[container.name] = item_id
            self.canvas_item_to_name[item_id] = container.name
        for item in self.project.reflector_items:
            item_id = self._draw_reflector_node(canvas, item)
            self.canvas_nodes[item.name] = item_id
            self.canvas_item_to_name[item_id] = item.name
        for stage in self.project.intervention_stages:
            item_id = self._draw_stage_node(canvas, stage)
            self.canvas_nodes[stage.name] = item_id
            self.canvas_item_to_name[item_id] = stage.name

    def _draw_grid(self, canvas: tk.Canvas, width: int, height: int) -> None:
        step = 48
        for x in range(0, width, step):
            canvas.create_line(x, 0, x, height, fill=COLORS["grid"], width=1)
        for y in range(0, height, step):
            canvas.create_line(0, y, width, y, fill=COLORS["grid"], width=1)

        canvas.create_text(24, 18, anchor="w", fill=COLORS["muted"], text="drag palette items into the canvas")
        canvas.create_text(width - 24, 18, anchor="e", fill=COLORS["muted"], text="right click a node to delete or duplicate")

    def _draw_node_card(self, canvas: tk.Canvas, kind: str, name: str, x: float, y: float, title: str, body: str, fill: str, selected: bool) -> int:
        outline = COLORS["accent"] if selected else fill
        item_id = canvas.create_rectangle(
            x,
            y,
            x + NODE_WIDTH,
            y + NODE_HEIGHT,
            fill=COLORS["panel_alt"],
            outline=outline,
            width=2,
            tags=(f"node:{kind}:{name}", "draggable"),
        )
        text_tags = (f"text_of_{item_id}", f"node:{kind}:{name}")
        canvas.create_text(x + 14, y + 12, anchor="nw", fill=fill, text=title, font=("Segoe UI", 12, "bold"), tags=text_tags)
        canvas.create_text(x + 14, y + 40, anchor="nw", fill=COLORS["text"], text=body, font=("Segoe UI", 10), tags=text_tags)
        return item_id

    def _draw_container_node(self, canvas: tk.Canvas, container: ContainerItem) -> int:
        x = container.x or CANVAS_PADDING + 40
        y = container.y or CANVAS_PADDING + 40
        fill = COLORS["container"] if container.kind == "variable" else COLORS["container_array"]
        title = f"{'v' if container.kind == 'variable' else 'a'} {container.name}"
        body = f"count={container.count}  stride={container.stride}"
        return self._draw_node_card(canvas, "container", container.name, x, y, title, body, fill, self.selected_container_name == container.name)

    def _draw_reflector_node(self, canvas: tk.Canvas, item: ReflectorItem) -> int:
        x = item.x or CANVAS_PADDING + 40
        y = item.y or CANVAS_PADDING + 160
        body = f"filter={item.reflect_fun}"
        return self._draw_node_card(canvas, "reflector", item.name, x, y, f"Reflector {item.name}", body, COLORS["good"], self.selected_reflector_name == item.name)

    def _draw_stage_node(self, canvas: tk.Canvas, stage: InterventionStage) -> int:
        x = stage.x or CANVAS_PADDING + 40
        y = stage.y or CANVAS_PADDING + 280
        body = f"kind={stage.kind}"
        return self._draw_node_card(canvas, "stage", stage.name, x, y, f"Stage {stage.name}", body, COLORS["accent_2"], self.selected_stage_name == stage.name)

    def _draw_edges(self, canvas: tk.Canvas) -> None:
        for rule in self.project.decomposer_rules:
            source = self._find_container(rule.source)
            target = self._find_container(rule.target)
            if not source or not target:
                continue
            x1 = source.x + NODE_WIDTH
            y1 = source.y + NODE_HEIGHT / 2
            x2 = target.x
            y2 = target.y + NODE_HEIGHT / 2
            canvas.create_line(x1, y1, x2, y2, fill=COLORS["edge"], width=2, arrow=tk.LAST)
            mid_x = (x1 + x2) / 2
            mid_y = (y1 + y2) / 2 - 8
            canvas.create_text(mid_x, mid_y, text=rule.name, fill=COLORS["muted"], font=("Segoe UI", 9))

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
