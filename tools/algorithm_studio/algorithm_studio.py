#!/usr/bin/env python3

from __future__ import annotations

import copy
import ctypes
import json
import mimetypes
import os
import re
import shutil
import subprocess
import tempfile
import time
from datetime import datetime
from pathlib import Path
import tkinter as tk
from tkinter import font as tkfont
from tkinter import filedialog, messagebox, simpledialog, ttk
from typing import Any
from ctypes import wintypes
from urllib.parse import unquote, urlparse

LRESULT_TYPE = getattr(ctypes, "c_ssize_t", ctypes.c_longlong if ctypes.sizeof(ctypes.c_void_p) == ctypes.sizeof(ctypes.c_longlong) else ctypes.c_long)
CHAT_HISTORY_FULL_TURN_LIMIT = 12
CHAT_HISTORY_RECENT_TURN_COUNT = 8

try:
    from .approval_rules import ApprovalDecision, ApprovalRuleSet, evaluate_access_rules, load_access_rules, resolve_access_rules_path
except ImportError:
    from approval_rules import ApprovalDecision, ApprovalRuleSet, evaluate_access_rules, load_access_rules, resolve_access_rules_path

try:
    from .shared import (
        BLUEPRINT_NODE_MIN_HEIGHT,
        BLUEPRINT_NODE_WIDTH,
        CANVAS_PADDING,
        COLORS,
        NODE_HEIGHT,
        NODE_WIDTH,
        PROJECT_ROOT,
        SETTINGS_PATH,
        SIDEBAR_COLLAPSED_WIDTH,
        SIDEBAR_EXPANDED_WIDTH,
        SPECIAL_CARD_HEIGHT,
        SPECIAL_CARD_WIDTH,
        _load_algorithm_studio_settings,
        _resolve_codex_command,
        _save_algorithm_studio_settings,
    )
    from .ui_shell import (
        build_main_area,
        build_toolbar,
        build_ui,
        build_welcome_page,
        configure_style,
        destroy_welcome_page,
        finalize_welcome,
        welcome_connect_api,
        welcome_import_existing_agent,
        welcome_use_codex,
    )
except ImportError:
    from shared import (
        BLUEPRINT_NODE_MIN_HEIGHT,
        BLUEPRINT_NODE_WIDTH,
        CANVAS_PADDING,
        COLORS,
        NODE_HEIGHT,
        NODE_WIDTH,
        PROJECT_ROOT,
        SETTINGS_PATH,
        SIDEBAR_COLLAPSED_WIDTH,
        SIDEBAR_EXPANDED_WIDTH,
        SPECIAL_CARD_HEIGHT,
        SPECIAL_CARD_WIDTH,
        _load_algorithm_studio_settings,
        _resolve_codex_command,
        _save_algorithm_studio_settings,
    )
    from ui_shell import (
        build_main_area,
        build_toolbar,
        build_ui,
        build_welcome_page,
        configure_style,
        destroy_welcome_page,
        finalize_welcome,
        welcome_connect_api,
        welcome_import_existing_agent,
        welcome_use_codex,
    )

try:
    from .backend import (
        ContainerGroupItem,
        ConnectionItem,
        ContainerItem,
        DecomposerRule,
        FunctionFrameItem,
        FunctionTextItem,
        InterventionStage,
        ProjectState,
        ResourceNodeItem,
        ReflectorItem,
    )
    from .agent_client import MockAgentClient, generate_cpp_skeleton
except ImportError:
    from backend import (
        ContainerGroupItem,
        ConnectionItem,
        ContainerItem,
        DecomposerRule,
        FunctionFrameItem,
        FunctionTextItem,
        InterventionStage,
        ProjectState,
        ResourceNodeItem,
        ReflectorItem,
    )
    from agent_client import MockAgentClient, generate_cpp_skeleton


class AlgorithmStudioApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("Algorithm Studio")
        self.root.geometry("1640x980")
        self.root.configure(bg=COLORS["window"])

        self.project = ProjectState()
        self.project_manifest_text_cache = self.project.rebuild_manifest_text()
        self.project_manifest_revision = 1
        self.document_editor_dirty = False
        self.document_editor_applying = False
        self.document_apply_after_id: str | None = None
        self.document_last_error: str | None = None
        self.agent_client = MockAgentClient()
        self.selected_container_name: str | None = None
        self.selected_rule_name: str | None = None
        self.selected_reflector_name: str | None = None
        self.selected_res_node_name: str | None = None
        self.selected_function_name: str | None = None
        self.selected_function_text_name: str | None = None
        self.selected_stage_name: str | None = None
        self.selected_container_group_name: str | None = None
        self.selection_state: dict[str, Any] | None = None
        self.selection_clipboard: dict[str, Any] | None = None
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
        self.canvas_zoom: float = 1.0
        self.palette_canvas: tk.Canvas | None = None
        self.palette_inner_frame: ttk.Frame | None = None
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
        self.chat_history: list[dict[str, Any]] = []
        self.chat_attachments: list[dict[str, str]] = []
        self.chat_attachment_summary_var = tk.StringVar(value="No attachments.")
        self.chat_busy = False
        self.status_var = tk.StringVar(value="Ready.")
        self.welcome_status_var = tk.StringVar(value="Choose how to connect to an agent.")
        self.execution_panel_collapsed_var = tk.BooleanVar(value=False)
        self.execution_summary_var = tk.StringVar(value="No execution trace yet.")
        self.execution_started_at: float | None = None
        self.execution_elapsed_after_id: str | None = None
        self.execution_runs: list[dict[str, Any]] = []
        self.execution_current_run: dict[str, Any] | None = None
        self.preview_text: tk.Text | None = None
        self.log_text: tk.Text | None = None
        self.canvas: tk.Canvas | None = None
        self.container_list: tk.Listbox | None = None
        self.rule_list: tk.Listbox | None = None
        self.reflector_list: tk.Listbox | None = None
        self.stage_list: tk.Listbox | None = None
        self.inspector_text: tk.Text | None = None
        self.selection_frame: ttk.Frame | None = None
        self.selection_body_frame: ttk.Frame | None = None
        self.selection_summary_text: tk.Text | None = None
        self.selection_editor_frame: ttk.Frame | None = None
        self.selection_toggle_button: ttk.Button | None = None
        self.selection_name_var = tk.StringVar(value=self.project.next_container_group_name())
        self.selection_panel_collapsed_var = tk.BooleanVar(value=False)
        self.selection_value_text: tk.Text | None = None
        self.selection_value_entry: ttk.Entry | None = None
        self.selection_apply_button: ttk.Button | None = None
        self.agent_text: tk.Text | None = None
        self.agent_output_box: tk.Text | None = None
        self.sidebar_shell: ttk.Frame | None = None
        self.sidebar_body: ttk.Frame | None = None
        self.approval_row: ttk.Frame | None = None
        self.connection_row: ttk.LabelFrame | None = None
        self.connection_toggle_button: ttk.Button | None = None
        self.build_button: ttk.Button | None = None
        self.chat_history_text: tk.Text | None = None
        self.chat_input_text: tk.Text | None = None
        self.chat_attachment_label: ttk.Label | None = None
        self.chat_send_button: ttk.Button | None = None
        self.chat_rendered_images: list[tk.PhotoImage] = []
        self._chat_drop_wndproc: Any | None = None
        self._chat_drop_old_wndproc: Any | None = None
        self._chat_drop_hwnd: int | None = None
        self.sidebar_toggle_button: ttk.Button | None = None
        self.connection_panel_visible_var = tk.BooleanVar(value=False)
        self.welcome_frame: ttk.Frame | None = None
        self.welcome_result: str | None = None
        self.execution_body_frame: ttk.Frame | None = None
        self.execution_toggle_button: ttk.Button | None = None
        self.access_rules_path = resolve_access_rules_path()
        self._apply_saved_settings()
        self._configure_style()
        self._build_ui()
        self._install_settings_persistence()
        self._apply_selection_panel_layout()
        self._apply_execution_panel_layout()
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self._sync_project_to_vars()
        self._refresh_all()
        self._log("Algorithm Studio started.")

    def _configure_style(self) -> None:
        configure_style(self)

    def _build_ui(self) -> None:
        build_ui(self)

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
        self._save_settings()

    def _on_settings_changed(self, *_args: object) -> None:
        self._save_settings()

    def _save_settings(self) -> None:
        _save_algorithm_studio_settings(self._settings_payload())

    def _on_close(self) -> None:
        self._save_settings()
        self._cancel_document_apply()
        self._uninstall_chat_input_drop_target()
        self.root.destroy()

    def _build_toolbar(self) -> None:
        build_toolbar(self)

    def _build_main_area(self) -> None:
        build_main_area(self)

    def _build_welcome_page(self) -> None:
        build_welcome_page(self)

    def _destroy_welcome_page(self) -> None:
        destroy_welcome_page(self)

    def _finalize_welcome(self, provider: str) -> None:
        finalize_welcome(self, provider)

    def _welcome_use_codex(self) -> None:
        welcome_use_codex(self)

    def _welcome_connect_api(self) -> None:
        welcome_connect_api(self)

    def _welcome_import_existing_agent(self) -> None:
        welcome_import_existing_agent(self)

    def _build_palette_panel(self, parent: ttk.Frame) -> None:
        palette_shell = ttk.Frame(parent, padding=(0, 0, 12, 0))
        palette_shell.grid(row=0, column=0, sticky="ns")
        palette_shell.columnconfigure(0, weight=1)
        palette_shell.rowconfigure(2, weight=1)
        self.root.bind_all("<ButtonRelease-1>", self._finish_palette_drag, add="+")

        title = ttk.Label(palette_shell, text="Drag Palette")
        title.grid(row=0, column=0, sticky="w", pady=(0, 8))
        self._bind_palette_wheel(title)

        hint = ttk.Label(palette_shell, text="Drag blueprint nodes into the canvas", foreground=COLORS["muted"])
        hint.grid(row=1, column=0, sticky="w", pady=(0, 12))
        self._bind_palette_wheel(hint)

        palette_scroll_frame = ttk.Frame(palette_shell)
        palette_scroll_frame.grid(row=2, column=0, sticky="nsew")
        palette_scroll_frame.columnconfigure(0, weight=1)
        palette_scroll_frame.rowconfigure(0, weight=1)

        palette_canvas = tk.Canvas(
            palette_scroll_frame,
            bg=COLORS["window"],
            highlightthickness=0,
            width=280,
        )
        palette_scrollbar = ttk.Scrollbar(palette_scroll_frame, orient="vertical", command=palette_canvas.yview)
        palette_canvas.configure(yscrollcommand=palette_scrollbar.set)
        palette_canvas.grid(row=0, column=0, sticky="nsew")
        palette_scrollbar.grid(row=0, column=1, sticky="ns")
        self.palette_canvas = palette_canvas

        inner_frame = ttk.Frame(palette_canvas)
        self.palette_inner_frame = inner_frame
        inner_window = palette_canvas.create_window((0, 0), window=inner_frame, anchor="nw")

        def _sync_palette_scrollregion(_event: tk.Event | None = None) -> None:
            if not self.palette_canvas:
                return
            self.palette_canvas.configure(scrollregion=self.palette_canvas.bbox("all"))

        def _sync_palette_width(event: tk.Event) -> None:
            if not self.palette_canvas:
                return
            self.palette_canvas.itemconfigure(inner_window, width=event.width)

        inner_frame.bind("<Configure>", _sync_palette_scrollregion)
        palette_canvas.bind("<Configure>", _sync_palette_width)
        self._bind_palette_wheel(palette_canvas)
        self._bind_palette_wheel(inner_frame)

        self._create_palette_group(
            inner_frame,
            2,
            "Container",
            [
                ("variable", "v", "Variable"),
                ("array", "a", "Array"),
            ],
        )
        self._create_palette_group(
            inner_frame,
            3,
            "ToolNodes",
            [
                ("containerelement", "C", "container"),
                ("decomposer", "D", "Decomposer"),
                ("reflector", "R", "Reflector"),
                ("function", "ƒ", "Function"),
                ("interventioner", "I", "Interventioner"),
            ],
        )

        self._create_palette_group(
            inner_frame,
            4,
            "MeshNode",
            [
                ("resnode", "M", "meshNode"),
            ],
        )
        _sync_palette_scrollregion()

    def _create_palette_group(self, parent: ttk.Frame, row: int, title: str, items: list[tuple[str, str, str]]) -> None:
        group = ttk.LabelFrame(parent, text=title, padding=8)
        group.grid(row=row, column=0, sticky="ew", pady=(0, 12))
        group.columnconfigure(0, weight=1)
        self._bind_palette_wheel(group)

        for index, (kind, icon, label) in enumerate(items):
            display_icon = "fn" if kind == "function" else icon
            display_label = "fun" if kind == "function" else label
            tile = tk.Frame(group, bg=COLORS["panel_alt"], highlightbackground=COLORS["accent"], highlightthickness=1)
            tile.grid(row=index, column=0, sticky="ew", pady=(0, 8))
            tile.columnconfigure(1, weight=1)
            tile.rowconfigure(1, weight=1)
            tile.kind = kind  # type: ignore[attr-defined]

            badge = tk.Label(tile, text=display_icon, bg=COLORS["panel_alt"], fg=COLORS["accent"], width=3, anchor="center")
            badge.grid(row=0, column=0, rowspan=2, padx=8, pady=8)

            title_label = tk.Label(tile, text=display_label, bg=COLORS["panel_alt"], fg=COLORS["text"], anchor="w")
            title_label.grid(row=0, column=1, sticky="ew", pady=(8, 0))

            sub_label = tk.Label(tile, text="drag to canvas", bg=COLORS["panel_alt"], fg=COLORS["muted"], anchor="w")
            sub_label.grid(row=1, column=1, sticky="ew", pady=(0, 8))
            bind_targets = [tile, badge, title_label, sub_label]

            for widget in bind_targets:
                widget.bind(
                    "<ButtonPress-1>",
                    lambda event, value=kind, variant=label if kind == "resnode" else None: self._start_palette_drag(value, event, variant),
                )
                widget.bind("<B1-Motion>", self._palette_drag_motion)
                widget.bind("<ButtonRelease-1>", self._finish_palette_drag)
                self._bind_palette_wheel(widget)

    def _bind_palette_wheel(self, widget: tk.Widget) -> None:
        widget.bind("<MouseWheel>", self._on_palette_mouse_wheel)
        widget.bind("<Button-4>", self._on_palette_mouse_wheel)
        widget.bind("<Button-5>", self._on_palette_mouse_wheel)

    def _start_palette_drag(self, kind: str, event: tk.Event, variant: str | None = None) -> None:
        self.palette_drag_state = {
            "kind": kind,
            "variant": variant,
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
        variant = self.palette_drag_state.get("variant")
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
        x = self.canvas.canvasx(x_root - left) / self._canvas_zoom_factor()
        y = self.canvas.canvasy(y_root - top) / self._canvas_zoom_factor()
        self._drop_palette_item(kind, x, y, variant=str(variant) if variant is not None else None)

    def _drop_palette_item(self, kind: str, x: float, y: float, variant: str | None = None) -> None:
        if kind == "containerelement":
            name = self._singleton_ui_node_name(kind) or self.project.next_container_group_name()
            existing_group = self._find_container_group(name)
            if existing_group is not None:
                existing_group.x = x
                existing_group.y = y
                self.selected_container_group_name = name
                self.selected_container_name = None
                self.selected_rule_name = None
                self.selected_reflector_name = None
                self.selected_stage_name = None
                self.selected_res_node_name = None
                self._refresh_all()
                self._log(f"Moved container {name}.")
                return
            group = ContainerGroupItem(name=name, x=x, y=y, width=360.0, height=220.0)
            self.project.validate_container_group(group)
            self.project.container_groups.append(group)
            self.selected_container_group_name = name
            self.selected_container_name = None
            self.selected_rule_name = None
            self.selected_reflector_name = None
            self.selected_stage_name = None
            self.selected_res_node_name = None
            self._refresh_all()
            self._log(f"Added container {name}.")
            return
        if kind == "decomposer":
            name = self._singleton_ui_node_name(kind) or self.project.next_decomposer_name()
            existing_rule = self._find_rule(name)
            if existing_rule is not None:
                existing_rule.x = x
                existing_rule.y = y
                self.selected_rule_name = name
                self.selected_container_name = None
                self.selected_reflector_name = None
                self.selected_stage_name = None
                self.selected_res_node_name = None
                self._refresh_all()
                self._log(f"Moved decomposer {name}.")
                return
            self.project.decomposer_rules.append(DecomposerRule(name=name, source="", target="", x=x, y=y))
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
            name = self._singleton_ui_node_name(kind) or self.project.next_reflector_name()
            existing_reflector = self._find_reflector(name)
            if existing_reflector is not None:
                existing_reflector.x = x
                existing_reflector.y = y
                self.selected_reflector_name = name
                self.selected_container_name = None
                self.selected_stage_name = None
                self.selected_res_node_name = None
                self._refresh_all()
                self._log(f"Moved reflector {name}.")
                return
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
            name = self._singleton_ui_node_name(kind) or self.project.next_intervention_name()
            existing_stage = self._find_stage(name)
            if existing_stage is not None:
                existing_stage.x = x
                existing_stage.y = y
                self.selected_stage_name = name
                self.selected_container_name = None
                self.selected_reflector_name = None
                self.selected_res_node_name = None
                self._refresh_all()
                self._log(f"Moved interventioner {name}.")
                return
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
            name = self._singleton_ui_node_name(kind) or self.project.next_res_name()
            existing_res = self._find_res_node(name)
            resource_kind = variant or "mesh"
            if existing_res is not None:
                existing_res.x = x
                existing_res.y = y
                existing_res.resource_kind = resource_kind
                existing_res.resource_types = [resource_kind]
                existing_res.outputs = [resource_kind]
                self.selected_res_node_name = name
                self.selected_container_name = None
                self.selected_rule_name = None
                self.selected_reflector_name = None
                self.selected_stage_name = None
                self._refresh_all()
                self._log(f"Moved meshNode {name}.")
                return
            self.project.res_nodes.append(
                ResourceNodeItem(
                    name=name,
                    resource_types=[resource_kind],
                    outputs=[resource_kind],
                    resource_kind=resource_kind,
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
            self._log(f"Added meshNode {name}.")
            return
        if kind == "function":
            name = self._singleton_ui_node_name(kind) or self.project.next_function_name()
            existing_function = self._find_function_frame(name)
            if existing_function is not None:
                existing_function.x = x
                existing_function.y = y
                self.selected_function_name = name
                self.selected_container_name = None
                self.selected_rule_name = None
                self.selected_reflector_name = None
                self.selected_res_node_name = None
                self.selected_stage_name = None
                self._refresh_all()
                self._log(f"Moved function {name}.")
                return
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
            self._log(f"Added fun {name}.")
            return

    def _resource_output_ports(self, resource_kind: str, outputs: list[str] | None = None) -> list[str]:
        if outputs:
            return list(outputs)
        kind = resource_kind.strip().lower()
        if kind:
            return [kind]
        return ["out"]

    def _build_canvas_panel(self, parent: ttk.Frame) -> None:
        frame = ttk.Frame(parent)
        frame.grid(row=0, column=1, sticky="nsew")
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(0, weight=3)
        frame.rowconfigure(1, weight=2)

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
        self.canvas.bind("<Double-Button-1>", self._on_canvas_double_click)
        self.canvas.bind("<MouseWheel>", self._on_canvas_mouse_wheel)
        self.canvas.bind("<Button-4>", self._on_canvas_mouse_wheel)
        self.canvas.bind("<Button-5>", self._on_canvas_mouse_wheel)
        self.canvas.bind("<Configure>", lambda _event: self._redraw_canvas())

        document_frame = ttk.LabelFrame(frame, text="Document", padding=8)
        document_frame.grid(row=1, column=0, sticky="nsew", pady=(8, 0))
        document_frame.rowconfigure(0, weight=1)
        document_frame.columnconfigure(0, weight=1)
        document_scroll = ttk.Scrollbar(document_frame, orient="vertical")
        document_scroll.grid(row=0, column=1, sticky="ns")
        preview_text = tk.Text(
            document_frame,
            wrap="none",
            bg=COLORS["canvas"],
            fg=COLORS["text"],
            insertbackground=COLORS["text"],
            relief="flat",
            borderwidth=0,
            highlightthickness=1,
            highlightbackground=COLORS["grid"],
            highlightcolor=COLORS["accent"],
            yscrollcommand=document_scroll.set,
            font=("Consolas", 9),
        )
        preview_text.grid(row=0, column=0, sticky="nsew")
        preview_text.bind("<<Modified>>", self._on_document_text_modified)
        document_scroll.config(command=preview_text.yview)
        self.preview_text = preview_text

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
        self.sidebar_toggle_button = ttk.Button(title_row, text=">", width=3, command=self._toggle_sidebar)
        self.sidebar_toggle_button.grid(row=0, column=1, sticky="e")

        body = ttk.Frame(shell)
        body.grid(row=1, column=0, sticky="nsew")
        body.columnconfigure(0, weight=1)
        body.rowconfigure(1, weight=1)
        self.sidebar_body = body

        selection_shell = ttk.Frame(body)
        selection_shell.grid(row=0, column=0, sticky="ew")
        selection_shell.columnconfigure(0, weight=1)
        self.selection_frame = selection_shell

        selection_header = ttk.Frame(selection_shell)
        selection_header.grid(row=0, column=0, sticky="ew")
        selection_header.columnconfigure(0, weight=1)
        ttk.Label(selection_header, text="Selection").grid(row=0, column=0, sticky="w")
        selection_toggle = ttk.Button(selection_header, text="收起", width=8, command=self._toggle_selection_panel)
        selection_toggle.grid(row=0, column=1, sticky="e")
        self.selection_toggle_button = selection_toggle

        selection_body = ttk.Frame(selection_shell, padding=8)
        selection_body.grid(row=1, column=0, sticky="ew")
        selection_body.columnconfigure(0, weight=1)
        selection_body.columnconfigure(1, weight=1)
        selection_body.columnconfigure(2, weight=1)
        self.selection_body_frame = selection_body

        selection_summary = tk.Text(
            selection_body,
            wrap="word",
            height=6,
            bg=COLORS["canvas"],
            fg=COLORS["text"],
            insertbackground=COLORS["text"],
            relief="flat",
            borderwidth=0,
            highlightthickness=1,
            highlightbackground=COLORS["grid"],
            highlightcolor=COLORS["accent"],
        )
        selection_summary.grid(row=0, column=0, columnspan=3, sticky="ew")
        selection_summary.configure(state="disabled")
        self.selection_summary_text = selection_summary

        selection_name_row = ttk.Frame(selection_body)
        selection_name_row.grid(row=1, column=0, columnspan=3, sticky="ew", pady=(8, 0))
        selection_name_row.columnconfigure(1, weight=1)
        ttk.Label(selection_name_row, text="Merge name").grid(row=0, column=0, sticky="w", padx=(0, 8))
        selection_name_entry = ttk.Entry(selection_name_row, textvariable=self.selection_name_var)
        selection_name_entry.grid(row=0, column=1, sticky="ew")

        selection_buttons = ttk.Frame(selection_body)
        selection_buttons.grid(row=2, column=0, columnspan=3, sticky="ew", pady=(8, 0))
        selection_buttons.columnconfigure((0, 1, 2, 3), weight=1)
        ttk.Button(selection_buttons, text="Copy", command=self._copy_current_selection).grid(row=0, column=0, sticky="ew", padx=(0, 6))
        ttk.Button(selection_buttons, text="Merge", command=self._merge_current_selection).grid(row=0, column=1, sticky="ew", padx=6)
        ttk.Button(selection_buttons, text="Delete", command=self._delete_current_selection).grid(row=0, column=2, sticky="ew", padx=6)
        ttk.Button(selection_buttons, text="Paste", command=self._paste_selection_from_clipboard).grid(row=0, column=3, sticky="ew", padx=(6, 0))

        history_frame = ttk.LabelFrame(body, text="Conversation", padding=8)
        history_frame.grid(row=1, column=0, sticky="nsew", pady=(8, 0))
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
        self.chat_history_text.tag_configure(
            "user",
            foreground=COLORS["good"],
            justify="right",
            lmargin1=120,
            lmargin2=120,
            rmargin=12,
        )
        self.chat_history_text.tag_configure(
            "assistant",
            foreground=COLORS["accent"],
            justify="left",
            lmargin1=12,
            lmargin2=12,
            rmargin=120,
        )
        self.chat_history_text.tag_configure(
            "system",
            foreground=COLORS["bad"],
            justify="left",
            lmargin1=12,
            lmargin2=12,
            rmargin=120,
        )
        self.chat_history_text.tag_configure(
            "error",
            foreground=COLORS["bad"],
            justify="left",
            lmargin1=12,
            lmargin2=12,
            rmargin=120,
        )
        self.chat_history_text.tag_configure(
            "activity",
            foreground=COLORS["muted"],
            justify="left",
            lmargin1=12,
            lmargin2=12,
            rmargin=120,
            font=("Segoe UI", 9),
        )
        self.chat_history_text.tag_configure(
            "chat_code",
            lmargin1=24,
            lmargin2=24,
            rmargin=120,
            spacing1=4,
            spacing3=4,
            font=("Consolas", 9),
            background=COLORS["panel_alt"],
        )
        self.chat_history_text.tag_configure(
            "chat_code_label",
            lmargin1=24,
            lmargin2=24,
            rmargin=120,
            spacing1=2,
            font=("Consolas", 8, "bold"),
            foreground=COLORS["muted"],
            background=COLORS["panel_alt"],
        )
        self.chat_history_text.configure(state="disabled")

        input_frame = ttk.LabelFrame(body, text="Prompt", padding=8)
        input_frame.grid(row=2, column=0, sticky="ew", pady=(8, 0))
        input_frame.columnconfigure(0, weight=1)
        input_frame.rowconfigure(0, weight=1)
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
        self.chat_input_text.bind("<Control-v>", self._handle_chat_paste_event)
        self.chat_input_text.bind("<Control-V>", self._handle_chat_paste_event)
        self.chat_input_text.bind("<Shift-Insert>", self._handle_chat_paste_event)

        action_row = ttk.Frame(input_frame)
        action_row.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        action_row.columnconfigure(0, weight=1)
        action_row.columnconfigure(1, weight=1)
        ttk.Button(action_row, text="Clear Chat", command=self._clear_chat_history).grid(row=0, column=0, sticky="ew", padx=(0, 6))
        self.chat_send_button = ttk.Button(action_row, text="Send", command=self._send_chat_message)
        self.chat_send_button.grid(row=0, column=1, sticky="ew", padx=(6, 0))
        self._install_chat_input_drop_target()

        settings_row = ttk.Frame(body)
        settings_row.grid(row=3, column=0, sticky="ew", pady=(8, 0))
        settings_row.columnconfigure(0, weight=1)
        settings_row.columnconfigure(1, weight=1)
        settings_row.columnconfigure(2, weight=1)

        provider_button = ttk.Button(settings_row, text="Provider / Key >", width=16, command=self._toggle_connection_panel)
        provider_button.grid(row=0, column=0, sticky="ew", padx=(0, 8))
        self.connection_toggle_button = provider_button

        model_row = ttk.Frame(settings_row)
        model_row.grid(row=0, column=1, sticky="ew", padx=(0, 8))
        model_row.columnconfigure(1, weight=1)
        ttk.Label(model_row, text="Model").grid(row=0, column=0, sticky="w", padx=(0, 8))
        model_picker = ttk.Combobox(model_row, textvariable=self.model_var, values=("gpt-5.2", "gpt-5.4-mini", "deepseek-v4-flash", "deepseek-v4-pro", "mock"), state="readonly", width=16)
        model_picker.grid(row=0, column=1, sticky="ew")
        model_picker.bind("<<ComboboxSelected>>", self._on_model_selection_changed)

        approval_row = ttk.Frame(settings_row)
        approval_row.grid(row=0, column=2, sticky="ew")
        approval_row.columnconfigure(1, weight=1)
        self.approval_row = approval_row
        ttk.Label(approval_row, text="Approval").grid(row=0, column=0, sticky="w", padx=(0, 8))
        approval_picker = ttk.Combobox(approval_row, textvariable=self.approval_mode_var, values=("manual", "rules"), state="readonly", width=12)
        approval_picker.grid(row=0, column=1, sticky="ew")
        approval_picker.bind("<<ComboboxSelected>>", self._on_approval_mode_selection_changed)

        connection_row = ttk.LabelFrame(body, text="Connection", padding=8)
        connection_row.grid(row=4, column=0, sticky="ew", pady=(6, 0))
        connection_row.columnconfigure(1, weight=1)
        ttk.Label(connection_row, text="Source").grid(row=0, column=0, sticky="w", padx=(0, 8))
        source_picker = ttk.Combobox(connection_row, textvariable=self.provider_var, values=("codex", "api", "mock"), state="readonly")
        source_picker.grid(row=0, column=1, sticky="ew")
        source_picker.bind("<<ComboboxSelected>>", self._on_provider_selection_changed)
        ttk.Label(connection_row, text="Base URL").grid(row=1, column=0, sticky="w", padx=(0, 8), pady=(8, 0))
        base_url_entry = ttk.Entry(connection_row, textvariable=self.base_url_var)
        base_url_entry.grid(row=1, column=1, sticky="ew", pady=(8, 0))
        ttk.Label(connection_row, text="API Key").grid(row=2, column=0, sticky="w", padx=(0, 8), pady=(8, 0))
        api_key_entry = ttk.Entry(connection_row, textvariable=self.api_key_var, show="*")
        api_key_entry.grid(row=2, column=1, sticky="ew", pady=(8, 0))
        self.connection_row = connection_row
        self.connection_row.grid_remove()

        self.agent_text = self.chat_input_text
        self.agent_output_box = self.chat_history_text
        self._apply_sidebar_layout()
        self._apply_connection_panel_layout()
        self._refresh_selection_panel()

    def _toggle_sidebar(self) -> None:
        self.sidebar_collapsed_var.set(not self.sidebar_collapsed_var.get())
        self._apply_sidebar_layout()

    def _toggle_connection_panel(self) -> None:
        self.connection_panel_visible_var.set(not self.connection_panel_visible_var.get())
        self._apply_connection_panel_layout()

    def _toggle_selection_panel(self) -> None:
        self.selection_panel_collapsed_var.set(not self.selection_panel_collapsed_var.get())
        self._apply_selection_panel_layout()

    def _toggle_execution_panel(self) -> None:
        self.execution_panel_collapsed_var.set(not self.execution_panel_collapsed_var.get())
        self._apply_execution_panel_layout()

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
            self.sidebar_toggle_button.configure(text=">")
        else:
            if not self.sidebar_body.winfo_ismapped():
                self.sidebar_body.grid()
            self.sidebar_toggle_button.configure(text="<")

    def _apply_connection_panel_layout(self) -> None:
        if not self.connection_row or not self.connection_toggle_button:
            return
        if self.connection_panel_visible_var.get():
            self.connection_row.grid()
            self.connection_toggle_button.configure(text="Provider / Key v")
        else:
            self.connection_row.grid_remove()
            self.connection_toggle_button.configure(text="Provider / Key >")

    def _apply_selection_panel_layout(self) -> None:
        if not self.selection_body_frame or not self.selection_toggle_button:
            return
        if self.selection_panel_collapsed_var.get():
            self.selection_body_frame.grid_remove()
            self.selection_toggle_button.configure(text="唤起")
        else:
            if not self.selection_body_frame.winfo_ismapped():
                self.selection_body_frame.grid()
            self.selection_toggle_button.configure(text="收起")

    def _apply_execution_panel_layout(self) -> None:
        if not self.execution_body_frame or not self.execution_toggle_button:
            return
        if self.execution_panel_collapsed_var.get():
            self.execution_body_frame.grid_remove()
            self.execution_toggle_button.configure(text="唤起")
        else:
            if not self.execution_body_frame.winfo_ismapped():
                self.execution_body_frame.grid()
            self.execution_toggle_button.configure(text="收起")

    def _on_provider_selection_changed(self, _event: tk.Event | None = None) -> None:
        provider = self.provider_var.get().strip().lower() or "codex"
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

    def _new_skeleton_project(self) -> ProjectState:
        return ProjectState()

    def _apply_project_vars(self) -> None:
        self.project.algorithm_name = self.project_name_var.get().strip() or "new_algorithm"
        self.project.package_name = self.package_name_var.get().strip() or self.project.algorithm_name
        self.project.cpu_available = self.cpu_var.get()
        self.project.gpu_available = self.gpu_var.get()
        self._refresh_all()

    def _reset_scene(self) -> None:
        self._replace_project_state(self._new_skeleton_project(), source="Scene reset.", reset_chat=True)
        self._log("Scene reset.")

    def _new_project(self) -> None:
        self._replace_project_state(self._new_skeleton_project(), source="New project.", reset_chat=True)
        self._log("Created new project skeleton.")

    def _load_default_template(self) -> None:
        self._replace_project_state(self._new_skeleton_project(), source="Loaded skeleton template.", reset_chat=True)
        self._log("Loaded skeleton template.")

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
        self._replace_project_state(ProjectState.from_package_json(payload), source=f"Loaded package {path}.", reset_chat=True)
        self._log(f"Loaded package {path}.")

    def _save_package(self) -> None:
        if not self._commit_document_editor_or_report():
            return
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

    def _canonical_build_algorithm_name(self) -> str:
        self._apply_project_vars()
        algorithm_name = self.project.algorithm_name.strip()
        package_name = self.project.package_name.strip() or algorithm_name
        if not algorithm_name:
            raise RuntimeError("Algorithm name is required before build.")
        if not package_name:
            raise RuntimeError("Package name is required before build.")
        if algorithm_name != package_name:
            raise RuntimeError("Build currently requires algorithm_name and package_name to match.")
        if not re.fullmatch(r"[A-Za-z0-9_]+", algorithm_name):
            raise RuntimeError("Algorithm name must use only letters, numbers, and underscores for build.")
        return algorithm_name

    def _is_inline_shader_source(self, text: str) -> bool:
        stripped = text.strip()
        if not stripped:
            return False
        if "\n" in stripped or "\r" in stripped:
            return True
        shader_tokens = ("#version", "void main", "layout(", "gl_", "vec2", "vec3", "vec4", "mat4")
        lowered = stripped.lower()
        return any(token.lower() in lowered for token in shader_tokens)

    def _existing_algorithm_folder(self, algorithm_name: str) -> Path:
        return PROJECT_ROOT / "src" / "capabilities" / "algorithm_library" / algorithm_name

    def _existing_algorithm_catalog_path(self) -> Path:
        return PROJECT_ROOT / "src" / "capabilities" / "algorithm_library" / "algorithm_catalog.json"

    def _materialize_stage_shader(
        self,
        bundle_dir: Path,
        algorithm_name: str,
        stage: InterventionStage,
        shader_kind: str,
        field_value: str,
    ) -> str:
        text = field_value.strip()
        if not text:
            return ""
        expected_ext = ".vert" if shader_kind == "vertex" else ".frag"
        if self._is_inline_shader_source(text):
            stage_token = re.sub(r"[^A-Za-z0-9_]+", "_", stage.name.strip() or stage.kind.strip() or "stage").strip("_") or "stage"
            filename = f"{algorithm_name}_{stage_token}{expected_ext}"
            (bundle_dir / filename).write_text(text.rstrip() + "\n", encoding="utf-8")
            return filename
        filename = Path(text).name
        if not filename.endswith(expected_ext):
            raise RuntimeError(f"Stage {stage.name} {shader_kind} shader must end with {expected_ext}.")
        if not (bundle_dir / filename).exists():
            raise RuntimeError(f"Stage {stage.name} {shader_kind} shader file is missing: {filename}")
        return filename

    def _function_script_payload(self) -> str | None:
        scripts: list[tuple[str, str]] = []
        for item in self.project.function_frames:
            text = item.script.strip()
            if not text or text == "agent writes text and code here":
                continue
            scripts.append((item.name, text))
        if not scripts:
            return None
        if len(scripts) > 1:
            names = ", ".join(name for name, _text in scripts)
            raise RuntimeError(f"Build currently supports one non-empty fun script. Found: {names}")
        return scripts[0][1].rstrip() + "\n"

    def _update_algorithm_catalog_entry(self, algorithm_name: str, manifest_name: str) -> None:
        catalog_path = self._existing_algorithm_catalog_path()
        payload: dict[str, Any]
        if catalog_path.exists():
            payload = json.loads(catalog_path.read_text(encoding="utf-8"))
            if not isinstance(payload, dict):
                raise RuntimeError("algorithm_catalog.json root must be an object.")
        else:
            payload = {}
        raw_algorithms = payload.get("algorithms", [])
        if raw_algorithms is None:
            raw_algorithms = []
        if not isinstance(raw_algorithms, list):
            raise RuntimeError("algorithm_catalog.json algorithms must be a list.")
        algorithms: list[dict[str, Any]] = []
        for entry in raw_algorithms:
            if not isinstance(entry, dict):
                raise RuntimeError("algorithm_catalog.json contains a non-object algorithm entry.")
            if str(entry.get("name") or "").strip() == algorithm_name:
                continue
            algorithms.append(entry)
        algorithms.append(
            {
                "name": algorithm_name,
                "display_name": algorithm_name,
                "folder": algorithm_name,
                "container_manifest": manifest_name,
                "decomposer": manifest_name,
                "reflector": manifest_name,
                "intervention": manifest_name,
            }
        )
        algorithms.sort(key=lambda entry: str(entry.get("name") or ""))
        payload["algorithms"] = algorithms
        catalog_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    def _materialize_algorithm_bundle(self) -> tuple[str, Path]:
        if not self._commit_document_editor_or_report():
            raise RuntimeError("Document is invalid.")
        algorithm_name = self._canonical_build_algorithm_name()
        bundle_dir = self._existing_algorithm_folder(algorithm_name)
        bundle_dir.mkdir(parents=True, exist_ok=True)

        expected_manifest_name = f"{algorithm_name}_package.json"
        conflicting_manifests = [path.name for path in bundle_dir.glob("*_package.json") if path.name != expected_manifest_name]
        if conflicting_manifests:
            raise RuntimeError(f"Remove old package manifests from {bundle_dir.name}: {', '.join(conflicting_manifests)}")

        plugin_path = bundle_dir / f"{algorithm_name}_plugin.cpp"
        conflicting_plugins = [path.name for path in bundle_dir.glob("*_plugin.cpp") if path.name != plugin_path.name]
        if conflicting_plugins:
            raise RuntimeError(f"Remove old plugin sources from {bundle_dir.name}: {', '.join(conflicting_plugins)}")

        manifest = copy.deepcopy(self.project.to_package_json())
        manifest["algorithm_name"] = algorithm_name
        manifest["package_name"] = algorithm_name
        manifest.pop("ui", None)
        manifest.pop("notes", None)
        manifest.pop("function", None)
        manifest.pop("functionText", None)

        stage_lookup = {stage.name: stage for stage in self.project.intervention_stages}
        stages_payload = manifest.get("intervention", {}).get("stages", {})
        if not isinstance(stages_payload, dict):
            raise RuntimeError("intervention.stages must be an object before build.")
        for stage_name, stage_payload in stages_payload.items():
            if not isinstance(stage_payload, dict):
                raise RuntimeError(f"Stage payload must be an object: {stage_name}")
            stage = stage_lookup.get(stage_name)
            if stage is None:
                raise RuntimeError(f"Missing stage model during build: {stage_name}")
            vertex_ref = self._materialize_stage_shader(bundle_dir, algorithm_name, stage, "vertex", stage.shader_vertex)
            fragment_ref = self._materialize_stage_shader(bundle_dir, algorithm_name, stage, "fragment", stage.shader_fragment)
            if bool(vertex_ref) != bool(fragment_ref):
                raise RuntimeError(f"Stage {stage_name} must provide both vertex and fragment shader content.")
            shader_payload = stage_payload.get("shader")
            if vertex_ref and fragment_ref:
                if not isinstance(shader_payload, dict):
                    shader_payload = {}
                shader_payload["pipeline"] = str(shader_payload.get("pipeline") or stage.pipeline or "graphics")
                shader_payload["vertex"] = vertex_ref
                shader_payload["fragment"] = fragment_ref
                stage_payload["shader"] = shader_payload
            elif isinstance(shader_payload, dict):
                shader_payload.pop("vertex", None)
                shader_payload.pop("fragment", None)
                if not shader_payload:
                    stage_payload.pop("shader", None)

        has_vertex_shader = any(bundle_dir.glob("*.vert"))
        has_fragment_shader = any(bundle_dir.glob("*.frag"))
        if not has_vertex_shader or not has_fragment_shader:
            raise RuntimeError("Build requires shader sources in the algorithm folder. Provide vertex/fragment shader text or existing .vert/.frag files.")

        manifest_path = bundle_dir / expected_manifest_name
        manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

        plugin_text = self._function_script_payload()
        if plugin_text is not None:
            plugin_path.write_text(plugin_text, encoding="utf-8")

        self._update_algorithm_catalog_entry(algorithm_name, expected_manifest_name)
        return algorithm_name, bundle_dir

    def _summarize_build_output(self, stdout_text: str, stderr_text: str) -> str:
        lines = [line.strip() for line in (stdout_text + "\n" + stderr_text).splitlines() if line.strip()]
        if not lines:
            return "Build failed without output."
        preview = lines[-6:]
        return " | ".join(self._compact_activity_text(line, limit=72) for line in preview)

    def _run_build_command(self, algorithm_name: str) -> str:
        build_script = PROJECT_ROOT / "build_algorithm_hot.bat"
        if not build_script.exists():
            raise RuntimeError(f"Build script is missing: {build_script}")
        completed = subprocess.run(
            ["cmd.exe", "/c", str(build_script), algorithm_name],
            cwd=str(PROJECT_ROOT),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
        if completed.returncode != 0:
            raise RuntimeError(self._summarize_build_output(completed.stdout, completed.stderr))
        return self._summarize_build_output(completed.stdout, completed.stderr)

    def _build_current_algorithm(self) -> None:
        try:
            if not self._commit_document_editor_or_report():
                return
            algorithm_name = self._canonical_build_algorithm_name()
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("Build error", str(exc))
            return

        if self.build_button:
            self.build_button.configure(state="disabled")
        self._start_execution_trace("构建算法", f"准备把 {algorithm_name} 的 doc / cpp / shader 落盘并编译。")
        self.status_var.set(f"Building {algorithm_name}...")
        self._append_execution_trace("正在整理当前文档和脚本。", "reasoning")

        def finish_success(bundle_dir: Path, output_summary: str) -> None:
            if self.build_button:
                self.build_button.configure(state="normal")
            runtime_dir = PROJECT_ROOT / "algorithmLib" / "algorithmruntimeLib" / bundle_dir.name
            detail = f"已构建到 {runtime_dir}"
            if output_summary:
                detail = f"{detail} | {output_summary}"
            self._finish_execution_trace("构建算法", True, detail)
            self.status_var.set(f"Build finished for {bundle_dir.name}.")
            self._log(f"Built algorithm {bundle_dir.name}.")

        def finish_error(exc: Exception) -> None:
            if self.build_button:
                self.build_button.configure(state="normal")
            message = self._compact_activity_text(str(exc), limit=180) or "Build failed."
            self._finish_execution_trace("构建算法", False, message)
            self.status_var.set("Build failed.")
            self._log(f"Build failed: {message}")
            messagebox.showerror("Build error", str(exc))

        def worker() -> None:
            try:
                self.root.after(0, lambda: self._append_execution_trace("正在写入算法包目录。", "reasoning"))
                resolved_name, bundle_dir = self._materialize_algorithm_bundle()
                self.root.after(0, lambda: self._append_execution_trace("正在调用专用构建批处理。", "reasoning"))
                output_summary = self._run_build_command(resolved_name)
            except Exception as exc:  # noqa: BLE001
                self.root.after(0, lambda exc=exc: finish_error(exc))
                return
            self.root.after(0, lambda bundle_dir=bundle_dir, output_summary=output_summary: finish_success(bundle_dir, output_summary))

        import threading

        threading.Thread(target=worker, daemon=True).start()

    def _reset_chat_state(self) -> None:
        self.chat_busy = False
        self.chat_history = []
        self.chat_rendered_images.clear()
        self._clear_chat_attachments()
        if self.chat_history_text:
            self.chat_history_text.configure(state="normal")
            self.chat_history_text.delete("1.0", tk.END)
            self.chat_history_text.configure(state="disabled")
        if self.chat_input_text:
            self.chat_input_text.delete("1.0", tk.END)
        self._log(f"ChatBox ready. Approval mode: {self.approval_mode_var.get().strip() or 'manual'}.")

    def _generate_cpp_skeleton(self) -> str:
        return generate_cpp_skeleton(self.project.algorithm_name)

    def _sync_project_manifest_cache(self) -> None:
        manifest_text = self.project.rebuild_manifest_text()
        if manifest_text != self.project_manifest_text_cache:
            self.project_manifest_revision += 1
        self.project_manifest_text_cache = manifest_text

    def _project_manifest_text(self) -> str:
        if not self.project_manifest_text_cache:
            self._sync_project_manifest_cache()
        return self.project_manifest_text_cache

    def _current_document_text_for_agent(self) -> str:
        if self.preview_text is not None:
            text = self.preview_text.get("1.0", tk.END).strip()
            if text:
                return text
        cached = self._project_manifest_text().strip()
        if not cached:
            raise AssertionError("Document text is unavailable.")
        return cached

    def _agent_document_context(self) -> str:
        return "\n".join(
            [
                "Current document:",
                self._current_document_text_for_agent(),
            ]
        )

    def _extract_agent_tool_calls(self, response: str) -> tuple[list[dict[str, Any]], str]:
        text = str(response or "")
        tool_pattern = re.compile(r"```algorithm-studio-tool\s*(.*?)\s*```", re.IGNORECASE | re.DOTALL)
        calls: list[dict[str, Any]] = []
        for match in tool_pattern.finditer(text):
            payload_text = match.group(1).strip()
            try:
                payload = json.loads(payload_text)
            except json.JSONDecodeError as exc:
                raise RuntimeError(f"Agent tool payload is not valid JSON: {exc}") from exc
            if not isinstance(payload, dict):
                raise RuntimeError("Agent tool payload must be a JSON object.")
            calls.append(payload)
        visible_text = tool_pattern.sub("", text).strip()
        if calls:
            return calls, visible_text
        stripped = text.strip()
        if stripped.startswith("{") and stripped.endswith("}"):
            try:
                payload = json.loads(stripped)
            except json.JSONDecodeError:
                return [], stripped
            if isinstance(payload, dict) and str(payload.get("tool") or "").strip():
                return [payload], ""
        return [], stripped

    def _apply_agent_update_document_tool(self, payload: dict[str, Any]) -> str:
        tool_name = str(payload.get("tool") or "").strip()
        if tool_name != "update_document":
            raise RuntimeError(f"Unsupported agent tool: {tool_name or '(empty)'}")
        if "document" not in payload:
            raise RuntimeError("Agent update_document tool call is missing document.")
        document_value = payload["document"]
        if isinstance(document_value, str):
            try:
                document_payload = json.loads(document_value)
            except json.JSONDecodeError as exc:
                raise RuntimeError(f"Agent document string is not valid JSON: {exc}") from exc
        elif isinstance(document_value, dict):
            document_payload = copy.deepcopy(document_value)
        else:
            raise RuntimeError("Agent document must be a JSON object or a JSON string.")
        if not isinstance(document_payload, dict):
            raise RuntimeError("Agent document root must be a JSON object.")
        project = ProjectState.from_package_json(document_payload)
        self._cancel_document_apply()
        self.document_editor_dirty = False
        self.document_last_error = None
        self._replace_project_state(project, source="Agent updated document.")
        self._set_document_text(self._project_manifest_text())
        message = self._compact_activity_text(str(payload.get("message") or payload.get("summary") or ""), limit=120)
        if message:
            return message
        return "Document updated."

    def _consume_agent_tool_response(self, response: str) -> str:
        tool_calls, visible_text = self._extract_agent_tool_calls(response)
        if not tool_calls:
            return visible_text
        summaries: list[str] = []
        for payload in tool_calls:
            tool_name = str(payload.get("tool") or "").strip()
            if tool_name in {"ui_add_node", "ui_update_node", "ui_delete_node", "ui_add_rule"}:
                summaries.append(self._apply_agent_ui_tool(payload))
                continue
            if tool_name == "update_document":
                summaries.append(self._apply_agent_update_document_tool(payload))
                continue
            raise RuntimeError(f"Unsupported agent tool: {tool_name or '(empty)'}")
        if visible_text:
            return visible_text
        summary_text = "\n".join(part for part in summaries if part)
        if summary_text:
            return summary_text
        raise AssertionError("Agent tool response produced no visible message.")

    def _agent_tool_int(self, payload: dict[str, Any], key: str, default: int) -> int:
        value = payload.get(key, default)
        try:
            return int(value)
        except Exception as exc:  # noqa: BLE001
            raise RuntimeError(f"Agent tool field {key} must be an integer.") from exc

    def _agent_tool_float(self, payload: dict[str, Any], key: str, default: float) -> float:
        value = payload.get(key, default)
        try:
            return float(value)
        except Exception as exc:  # noqa: BLE001
            raise RuntimeError(f"Agent tool field {key} must be a number.") from exc

    def _agent_tool_list_of_str(self, payload: dict[str, Any], key: str) -> list[str]:
        value = payload.get(key, [])
        if value is None:
            return []
        if not isinstance(value, list):
            raise RuntimeError(f"Agent tool field {key} must be a list.")
        return [str(item) for item in value]

    def _singleton_ui_node_name(self, kind: str) -> str | None:
        normalized = str(kind).strip().lower()
        if normalized in {"container", "containerelement", "container_group", "containergroup"}:
            return "container"
        if normalized == "decomposer":
            return "decomposer"
        if normalized == "reflector":
            return "reflector"
        if normalized in {"stage", "interventioner"}:
            return "interventioner"
        if normalized == "resnode":
            return "meshNode"
        if normalized == "function":
            return "fun"
        return None

    def _agent_ui_default_position(self, sequence_index: int, lane: str = "tool") -> tuple[float, float]:
        if lane == "container":
            return CANVAS_PADDING + 36.0, CANVAS_PADDING + 48.0 + sequence_index * 110.0
        if lane == "group":
            return CANVAS_PADDING + 260.0, CANVAS_PADDING + 48.0
        if lane == "tool":
            return CANVAS_PADDING + 320.0, CANVAS_PADDING + 48.0 + sequence_index * 120.0
        base = CANVAS_PADDING + 36.0
        offset = sequence_index * 18.0
        return base + offset, base + offset

    def _clear_agent_selection(self) -> None:
        self.selected_container_name = None
        self.selected_rule_name = None
        self.selected_reflector_name = None
        self.selected_res_node_name = None
        self.selected_function_name = None
        self.selected_function_text_name = None
        self.selected_stage_name = None
        self.selected_container_group_name = None

    def _apply_agent_ui_add_node(self, payload: dict[str, Any]) -> str:
        kind = str(payload.get("kind") or "").strip().lower()
        if not kind:
            raise RuntimeError("ui_add_node requires kind.")
        self._clear_agent_selection()
        if kind in {"functiontext", "funtext", "textnode", "text"}:
            function_name = str(payload.get("function_name") or payload.get("function") or "fun").strip()
            if not function_name:
                raise RuntimeError("ui_add_node requires function_name for functiontext.")
            function_item = self._find_function_frame(function_name)
            if function_item is None:
                raise RuntimeError(f"Function not found for functiontext: {function_name}")
            existing_for_function = self._find_function_text_for_function(function_name)
            default_name = existing_for_function.name if existing_for_function is not None else self.project.next_function_text_name(function_name)
            name = str(payload.get("name") or default_name).strip()
            if not name:
                raise RuntimeError("ui_add_node requires a non-empty functiontext name.")
            x_default = function_item.x + 420.0
            y_default = function_item.y
            existing_item = self._find_function_text_item(name)
            if existing_item is not None:
                existing_item.function_name = function_name
                existing_item.text = str(payload.get("text") or existing_item.text)
                existing_item.x = self._agent_tool_float(payload, "x", existing_item.x if "x" in payload else x_default)
                existing_item.y = self._agent_tool_float(payload, "y", existing_item.y if "y" in payload else y_default)
                if "width" in payload:
                    existing_item.width = self._agent_tool_float(payload, "width", existing_item.width)
                if "height" in payload:
                    existing_item.height = self._agent_tool_float(payload, "height", existing_item.height)
                self.selected_function_text_name = name
                self._refresh_all()
                return f"Reused function text {name}."
            text_item = FunctionTextItem(
                name=name,
                function_name=function_name,
                text=str(payload.get("text") or ""),
                x=self._agent_tool_float(payload, "x", x_default),
                y=self._agent_tool_float(payload, "y", y_default),
                width=self._agent_tool_float(payload, "width", 360.0),
                height=self._agent_tool_float(payload, "height", 220.0),
            )
            self.project.function_text_items.append(text_item)
            self.selected_function_text_name = name
            self._refresh_all()
            return f"Added function text {name}."
        if kind in {"container", "containerelement", "container_group", "containergroup"}:
            name = str(payload.get("name") or self._singleton_ui_node_name(kind) or self.project.next_container_group_name()).strip()
            if not name:
                raise RuntimeError("ui_add_node requires a non-empty containerElement name.")
            x_default, y_default = self._agent_ui_default_position(len(self.project.container_groups), lane="group")
            existing_group = self._find_container_group(name)
            if existing_group is not None:
                existing_group.x = self._agent_tool_float(payload, "x", existing_group.x if "x" in payload else x_default)
                existing_group.y = self._agent_tool_float(payload, "y", existing_group.y if "y" in payload else y_default)
                if "width" in payload:
                    existing_group.width = self._agent_tool_float(payload, "width", existing_group.width)
                if "height" in payload:
                    existing_group.height = self._agent_tool_float(payload, "height", existing_group.height)
                self.project.validate_container_group(existing_group)
                self.selected_container_group_name = name
                self._refresh_all()
                return f"Reused container {name}."
            group = ContainerGroupItem(name=name, x=self._agent_tool_float(payload, "x", x_default), y=self._agent_tool_float(payload, "y", y_default), width=self._agent_tool_float(payload, "width", 360.0), height=self._agent_tool_float(payload, "height", 220.0))
            self.project.validate_container_group(group)
            self.project.container_groups.append(group)
            self.selected_container_group_name = name
            self._refresh_all()
            return f"Added container {name}."
        if kind in {"variable", "array"}:
            name = str(payload.get("name") or self.project.next_container_name(kind)).strip()
            if not name:
                raise RuntimeError("ui_add_node requires a non-empty container name.")
            if self._find_container(name):
                raise RuntimeError(f"Container already exists: {name}")
            x_default, y_default = self._agent_ui_default_position(len(self.project.containers), lane="container")
            count = max(1, self._agent_tool_int(payload, "count", 1))
            stride_default = 4 if kind == "variable" else 12
            stride = max(1, self._agent_tool_int(payload, "stride", stride_default))
            container = ContainerItem(
                name=name,
                kind=kind,
                count=count,
                stride=stride,
                value=str(payload.get("value") or ""),
                values=self._agent_tool_list_of_str(payload, "values"),
                structure=self._agent_tool_list_of_str(payload, "structure"),
                x=self._agent_tool_float(payload, "x", x_default),
                y=self._agent_tool_float(payload, "y", y_default),
            )
            self.project.containers.append(container)
            self.selected_container_name = name
            self._refresh_all()
            return f"Added {kind} node {name}."
        if kind in {"stage", "interventioner"}:
            name = str(payload.get("name") or self._singleton_ui_node_name(kind) or self.project.next_stage_name()).strip()
            if not name:
                raise RuntimeError("ui_add_node requires a non-empty stage name.")
            x_default, y_default = self._agent_ui_default_position(len(self.project.intervention_stages), lane="tool")
            existing_stage = self._find_stage(name)
            if existing_stage is not None:
                existing_stage.x = self._agent_tool_float(payload, "x", existing_stage.x if "x" in payload else x_default)
                existing_stage.y = self._agent_tool_float(payload, "y", existing_stage.y if "y" in payload else y_default)
                if "stage_kind" in payload:
                    existing_stage.kind = str(payload.get("stage_kind") or existing_stage.kind)
                self.selected_stage_name = name
                self._refresh_all()
                return f"Reused stage {name}."
            stage_kind = str(payload.get("stage_kind") or payload.get("kind_name") or "interventioner").strip() or "interventioner"
            stage = InterventionStage(
                name=name,
                kind=stage_kind,
                used_variables=self._agent_tool_list_of_str(payload, "used_variables"),
                used_arrays=self._agent_tool_list_of_str(payload, "used_arrays"),
                functions=self._agent_tool_list_of_str(payload, "functions"),
                shader_vertex=str(payload.get("shader_vertex") or ""),
                shader_fragment=str(payload.get("shader_fragment") or ""),
                x=self._agent_tool_float(payload, "x", x_default),
                y=self._agent_tool_float(payload, "y", y_default),
            )
            self.project.intervention_stages.append(stage)
            self.selected_stage_name = name
            self._refresh_all()
            return f"Added stage {name}."
        if kind == "resnode":
            name = str(payload.get("name") or self._singleton_ui_node_name(kind) or self.project.next_res_name()).strip()
            if not name:
                raise RuntimeError("ui_add_node requires a non-empty resnode name.")
            x_default, y_default = self._agent_ui_default_position(len(self.project.res_nodes), lane="tool")
            resource_kind = str(payload.get("resource_kind") or "mesh").strip() or "mesh"
            outputs = self._agent_tool_list_of_str(payload, "outputs") or [resource_kind]
            existing_res = self._find_res_node(name)
            if existing_res is not None:
                existing_res.resource_kind = resource_kind
                existing_res.resource_types = [resource_kind]
                existing_res.outputs = outputs
                existing_res.x = self._agent_tool_float(payload, "x", existing_res.x if "x" in payload else x_default)
                existing_res.y = self._agent_tool_float(payload, "y", existing_res.y if "y" in payload else y_default)
                self.selected_res_node_name = name
                self._refresh_all()
                return f"Reused meshNode {name}."
            item = ResourceNodeItem(
                name=name,
                resource_types=[resource_kind],
                outputs=outputs,
                resource_kind=resource_kind,
                x=self._agent_tool_float(payload, "x", x_default),
                y=self._agent_tool_float(payload, "y", y_default),
            )
            self.project.res_nodes.append(item)
            self.selected_res_node_name = name
            self._refresh_all()
            return f"Added meshNode {name}."
        if kind == "function":
            name = str(payload.get("name") or self._singleton_ui_node_name(kind) or self.project.next_function_name()).strip()
            if not name:
                raise RuntimeError("ui_add_node requires a non-empty function name.")
            x_default, y_default = self._agent_ui_default_position(len(self.project.function_frames), lane="tool")
            existing_function = self._find_function_frame(name)
            if existing_function is not None:
                existing_function.x = self._agent_tool_float(payload, "x", existing_function.x if "x" in payload else x_default)
                existing_function.y = self._agent_tool_float(payload, "y", existing_function.y if "y" in payload else y_default)
                if "script" in payload:
                    existing_function.script = str(payload.get("script") or "")
                self.selected_function_name = name
                self._refresh_all()
                return f"Reused fun {name}."
            item = FunctionFrameItem(
                name=name,
                script=str(payload.get("script") or "agent writes text and code here"),
                input_name=str(payload.get("input_name") or "in"),
                output_name=str(payload.get("output_name") or "out"),
                expected_input=str(payload.get("expected_input") or ""),
                expected_output=str(payload.get("expected_output") or ""),
                x=self._agent_tool_float(payload, "x", x_default),
                y=self._agent_tool_float(payload, "y", y_default),
            )
            self.project.function_frames.append(item)
            self.selected_function_name = name
            self._refresh_all()
            return f"Added fun {name}."
        if kind == "reflector":
            name = str(payload.get("name") or self._singleton_ui_node_name(kind) or self.project.next_reflector_name()).strip()
            if not name:
                raise RuntimeError("ui_add_node requires a non-empty reflector name.")
            x_default, y_default = self._agent_ui_default_position(len(self.project.reflector_items), lane="tool")
            existing_reflector = self._find_reflector(name)
            if existing_reflector is not None:
                existing_reflector.x = self._agent_tool_float(payload, "x", existing_reflector.x if "x" in payload else x_default)
                existing_reflector.y = self._agent_tool_float(payload, "y", existing_reflector.y if "y" in payload else y_default)
                self.selected_reflector_name = name
                self._refresh_all()
                return f"Reused reflector {name}."
            item = ReflectorItem(
                name=name,
                reflect_fun=str(payload.get("reflect_fun") or "direct"),
                inputs_varity=self._agent_tool_list_of_str(payload, "inputs_varity"),
                inputs_array=self._agent_tool_list_of_str(payload, "inputs_array"),
                output_kind=str(payload.get("output_kind") or "v"),
                output_name=str(payload.get("output_name") or ""),
                direct_from=self._agent_tool_list_of_str(payload, "direct_from"),
                direct_to=self._agent_tool_list_of_str(payload, "direct_to"),
                x=self._agent_tool_float(payload, "x", x_default),
                y=self._agent_tool_float(payload, "y", y_default),
            )
            self.project.reflector_items.append(item)
            self.selected_reflector_name = name
            self._refresh_all()
            return f"Added reflector {name}."
        raise RuntimeError(f"ui_add_node does not support kind: {kind}")

    def _apply_agent_ui_update_node(self, payload: dict[str, Any]) -> str:
        kind = str(payload.get("kind") or "").strip().lower()
        name = str(payload.get("name") or "").strip()
        if not kind or not name:
            raise RuntimeError("ui_update_node requires kind and name.")
        if kind in {"functiontext", "funtext", "textnode", "text"}:
            item = self._find_function_text_item(name)
            if item is None:
                raise RuntimeError(f"Function text not found: {name}")
            if "function_name" in payload:
                function_name = str(payload.get("function_name") or "").strip()
                if not function_name:
                    raise RuntimeError("function_name cannot be empty.")
                if self._find_function_frame(function_name) is None:
                    raise RuntimeError(f"Function not found for functiontext: {function_name}")
                item.function_name = function_name
            if "text" in payload:
                item.text = str(payload.get("text") or "")
            if "x" in payload:
                item.x = self._agent_tool_float(payload, "x", item.x)
            if "y" in payload:
                item.y = self._agent_tool_float(payload, "y", item.y)
            if "width" in payload:
                item.width = self._agent_tool_float(payload, "width", item.width)
            if "height" in payload:
                item.height = self._agent_tool_float(payload, "height", item.height)
            self.selected_function_text_name = name
            self._refresh_all()
            return f"Updated function text {name}."
        if kind in {"container", "containerelement", "container_group", "containergroup"}:
            item = self._find_container_group(name)
            if item is None:
                raise RuntimeError(f"containerElement not found: {name}")
            if "x" in payload:
                item.x = self._agent_tool_float(payload, "x", item.x)
            if "y" in payload:
                item.y = self._agent_tool_float(payload, "y", item.y)
            if "width" in payload:
                item.width = self._agent_tool_float(payload, "width", item.width)
            if "height" in payload:
                item.height = self._agent_tool_float(payload, "height", item.height)
            self.project.validate_container_group(item)
            self.selected_container_group_name = name
            self._refresh_all()
            return f"Updated container {name}."
        if kind in {"variable", "array"}:
            item = self._find_container(name)
            if item is None:
                raise RuntimeError(f"Container not found: {name}")
            if "count" in payload:
                item.count = max(1, self._agent_tool_int(payload, "count", item.count))
            if "stride" in payload:
                item.stride = max(1, self._agent_tool_int(payload, "stride", item.stride))
            if "value" in payload:
                item.value = str(payload.get("value") or "")
            if "values" in payload:
                item.values = self._agent_tool_list_of_str(payload, "values")
            if "structure" in payload:
                item.structure = self._agent_tool_list_of_str(payload, "structure")
            if "x" in payload:
                item.x = self._agent_tool_float(payload, "x", item.x)
            if "y" in payload:
                item.y = self._agent_tool_float(payload, "y", item.y)
            self.selected_container_name = name
            self._refresh_all()
            return f"Updated {kind} node {name}."
        if kind in {"stage", "interventioner"}:
            item = self._find_stage(name)
            if item is None:
                raise RuntimeError(f"Stage not found: {name}")
            if "stage_kind" in payload:
                item.kind = str(payload.get("stage_kind") or item.kind)
            if "functions" in payload:
                item.functions = self._agent_tool_list_of_str(payload, "functions")
            if "used_variables" in payload:
                item.used_variables = self._agent_tool_list_of_str(payload, "used_variables")
            if "used_arrays" in payload:
                item.used_arrays = self._agent_tool_list_of_str(payload, "used_arrays")
            if "shader_vertex" in payload:
                item.shader_vertex = str(payload.get("shader_vertex") or "")
            if "shader_fragment" in payload:
                item.shader_fragment = str(payload.get("shader_fragment") or "")
            if "x" in payload:
                item.x = self._agent_tool_float(payload, "x", item.x)
            if "y" in payload:
                item.y = self._agent_tool_float(payload, "y", item.y)
            self.selected_stage_name = name
            self._refresh_all()
            return f"Updated stage {name}."
        if kind == "function":
            item = self._find_function_frame(name)
            if item is None:
                raise RuntimeError(f"Function not found: {name}")
            if "script" in payload:
                item.script = str(payload.get("script") or "")
            if "input_name" in payload:
                item.input_name = str(payload.get("input_name") or "in")
            if "output_name" in payload:
                item.output_name = str(payload.get("output_name") or "out")
            if "expected_input" in payload:
                item.expected_input = str(payload.get("expected_input") or "")
            if "expected_output" in payload:
                item.expected_output = str(payload.get("expected_output") or "")
            if "x" in payload:
                item.x = self._agent_tool_float(payload, "x", item.x)
            if "y" in payload:
                item.y = self._agent_tool_float(payload, "y", item.y)
            self.selected_function_name = name
            self._refresh_all()
            return f"Updated function {name}."
        raise RuntimeError(f"ui_update_node does not support kind: {kind}")

    def _apply_agent_ui_delete_node(self, payload: dict[str, Any]) -> str:
        kind = str(payload.get("kind") or "").strip().lower()
        name = str(payload.get("name") or "").strip()
        if not kind or not name:
            raise RuntimeError("ui_delete_node requires kind and name.")
        self._clear_agent_selection()
        if kind in {"functiontext", "funtext", "textnode", "text"}:
            if self._find_function_text_item(name) is None:
                raise RuntimeError(f"Function text not found: {name}")
            self.selected_function_text_name = name
            self._delete_selected_function_text()
            return f"Deleted function text {name}."
        if kind in {"container", "containerelement", "container_group", "containergroup"}:
            if self._find_container_group(name) is None:
                raise RuntimeError(f"containerElement not found: {name}")
            self.selected_container_group_name = name
            self._delete_selected_container_group()
            return f"Deleted container {name}."
        if kind in {"variable", "array"}:
            if self._find_container(name) is None:
                raise RuntimeError(f"Container not found: {name}")
            self.selected_container_name = name
            self._delete_selected_container()
            return f"Deleted {kind} node {name}."
        if kind in {"stage", "interventioner"}:
            if self._find_stage(name) is None:
                raise RuntimeError(f"Stage not found: {name}")
            self.selected_stage_name = name
            self._delete_selected_stage()
            return f"Deleted stage {name}."
        if kind == "resnode":
            if self._find_res_node(name) is None:
                raise RuntimeError(f"resNode not found: {name}")
            self.selected_res_node_name = name
            self._delete_selected_res_node()
            return f"Deleted resNode {name}."
        if kind == "function":
            if self._find_function_frame(name) is None:
                raise RuntimeError(f"Function not found: {name}")
            self.selected_function_name = name
            self._delete_selected_function()
            return f"Deleted function {name}."
        if kind == "reflector":
            if self._find_reflector(name) is None:
                raise RuntimeError(f"Reflector not found: {name}")
            self.selected_reflector_name = name
            self._delete_selected_reflector()
            return f"Deleted reflector {name}."
        raise RuntimeError(f"ui_delete_node does not support kind: {kind}")

    def _apply_agent_ui_add_rule(self, payload: dict[str, Any]) -> str:
        source = str(payload.get("source") or "").strip()
        target = str(payload.get("target") or "").strip()
        if not source or not target:
            raise RuntimeError("ui_add_rule requires source and target.")
        if self._find_container(source) is None:
            raise RuntimeError(f"Rule source container not found: {source}")
        if self._find_container(target) is None:
            raise RuntimeError(f"Rule target container not found: {target}")
        name = str(payload.get("name") or f"{source}_to_{target}").strip()
        if not name:
            raise RuntimeError("ui_add_rule requires a non-empty rule name.")
        if self._find_rule(name):
            raise RuntimeError(f"Rule already exists: {name}")
        x_default, y_default = self._agent_ui_default_position(len(self.project.decomposer_rules))
        rule = DecomposerRule(
            name=name,
            source=source,
            target=target,
            map_kind=str(payload.get("map_kind") or "v2v"),
            descriptor_script=str(payload.get("descriptor_script") or ""),
            resource_mode=str(payload.get("resource_mode") or "default"),
            resource_script=str(payload.get("resource_script") or ""),
            x=self._agent_tool_float(payload, "x", x_default),
            y=self._agent_tool_float(payload, "y", y_default),
        )
        self.project.decomposer_rules.append(rule)
        self.selected_rule_name = name
        self._refresh_all()
        return f"Added rule {name}."

    def _apply_agent_ui_tool(self, payload: dict[str, Any]) -> str:
        tool_name = str(payload.get("tool") or "").strip()
        if tool_name == "ui_add_node":
            return self._apply_agent_ui_add_node(payload)
        if tool_name == "ui_update_node":
            return self._apply_agent_ui_update_node(payload)
        if tool_name == "ui_delete_node":
            return self._apply_agent_ui_delete_node(payload)
        if tool_name == "ui_add_rule":
            return self._apply_agent_ui_add_rule(payload)
        raise RuntimeError(f"Unsupported UI tool: {tool_name or '(empty)'}")

    def _set_document_text(self, text: str) -> None:
        if not self.preview_text:
            return
        self.document_editor_applying = True
        try:
            self.preview_text.delete("1.0", tk.END)
            self.preview_text.insert("1.0", text)
            self.preview_text.edit_modified(False)
        finally:
            self.document_editor_applying = False

    def _cancel_document_apply(self) -> None:
        if self.document_apply_after_id is None:
            return
        self.root.after_cancel(self.document_apply_after_id)
        self.document_apply_after_id = None

    def _on_document_text_modified(self, _event: tk.Event) -> None:
        if not self.preview_text:
            return
        if self.document_editor_applying:
            self.preview_text.edit_modified(False)
            return
        if not self.preview_text.edit_modified():
            return
        self.preview_text.edit_modified(False)
        self.document_editor_dirty = True
        self.document_last_error = None
        self._cancel_document_apply()
        self.document_apply_after_id = self.root.after(450, self._apply_document_editor_to_project)

    def _replace_project_state(self, project: ProjectState, *, source: str, reset_chat: bool = False) -> None:
        self.project = project
        self.project_manifest_text_cache = self.project.current_manifest_text()
        self.project_manifest_revision += 1
        if reset_chat:
            self._reset_chat_state()
        self.connection_drag_state = None
        self.marquee_state = None
        self.canvas_pan_state = None
        self.container_group_drag_state = None
        self.container_group_resize_state = None
        self.toolnode_resize_state = None
        self.node_drag_state = None
        self.palette_drag_state = None
        self.selection_state = None
        self.selected_container_name = None
        self.selected_rule_name = None
        self.selected_reflector_name = None
        self.selected_res_node_name = None
        self.selected_function_name = None
        self.selected_stage_name = None
        self.selected_container_group_name = None
        self._sync_project_to_vars()
        self._refresh_all()
        self.status_var.set(source)

    def _apply_document_editor_to_project(self) -> None:
        self.document_apply_after_id = None
        if not self.preview_text:
            raise AssertionError("Document editor is not initialized.")
        raw_text = self.preview_text.get("1.0", tk.END).strip()
        if not raw_text:
            self.document_last_error = "Document is empty."
            self.status_var.set("Document is empty.")
            return
        try:
            payload = json.loads(raw_text)
            if not isinstance(payload, dict):
                raise ValueError("Document root must be a JSON object.")
            project = ProjectState.from_package_json(payload)
        except Exception as exc:  # noqa: BLE001
            self.document_last_error = str(exc)
            self.status_var.set(f"Document invalid: {self._compact_activity_text(str(exc), limit=96)}")
            return
        self.document_editor_dirty = False
        self.document_last_error = None
        self._replace_project_state(project, source="Document applied.")
        self._set_document_text(self._project_manifest_text())
        self._log("Applied document changes to the scene.")

    def _commit_document_editor_or_report(self) -> bool:
        if not self.document_editor_dirty:
            return True
        self._apply_document_editor_to_project()
        if self.document_editor_dirty:
            message = self.document_last_error or "Document is invalid."
            messagebox.showerror("Document error", message)
            return False
        return True

    def _refresh_all(self) -> None:
        self._sync_all_container_groups()
        self._sync_project_manifest_cache()
        self._refresh_container_list()
        self._refresh_rule_list()
        self._refresh_reflector_list()
        self._refresh_stage_list()
        self._refresh_preview()
        self._redraw_canvas()
        self._refresh_inspector()
        self._refresh_selection_panel()

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
        if self.document_editor_dirty:
            return
        preview = self._project_manifest_text()
        self._set_document_text(preview)

    def _refresh_inspector(self) -> None:
        if not self.inspector_text:
            self._refresh_selection_panel()
            return
        self.inspector_text.delete("1.0", tk.END)
        content = self._selected_item_summary()
        self.inspector_text.insert("1.0", content)
        self._refresh_selection_panel()

    def _refresh_selection_panel(self) -> None:
        if self.selection_summary_text:
            self.selection_summary_text.configure(state="normal")
            self.selection_summary_text.delete("1.0", tk.END)
            self.selection_summary_text.insert("1.0", self._selected_item_summary())
            self.selection_summary_text.configure(state="disabled")
        if self.selection_state:
            self.selection_name_var.set(str(self.selection_state.get("suggested_name") or self.project.next_container_group_name()))
        elif not self.selection_name_var.get().strip():
            self.selection_name_var.set(self.project.next_container_group_name())
        self._render_selection_editor()

    def _render_selection_editor(self) -> None:
        if self.selection_editor_frame:
            self.selection_editor_frame.destroy()
            self.selection_editor_frame = None
        self.selection_value_text = None
        self.selection_value_entry = None
        self.selection_apply_button = None
        if not self.selection_body_frame:
            return
        parent = self.selection_body_frame
        editor_row = ttk.Frame(parent)
        editor_row.grid(row=3, column=0, columnspan=3, sticky="ew", pady=(8, 0))
        editor_row.columnconfigure(1, weight=1)
        self.selection_editor_frame = editor_row
        if self.selected_container_name:
            container = self._find_container(self.selected_container_name)
            if not container:
                return
            ttk.Label(editor_row, text="Data").grid(row=0, column=0, sticky="w", padx=(0, 8))
            if container.kind == "variable":
                entry = ttk.Entry(editor_row)
                entry.grid(row=0, column=1, sticky="ew")
                entry.insert(0, container.value)
                self.selection_value_entry = entry
                self.selection_value_text = None
            else:
                text = tk.Text(
                    editor_row,
                    wrap="none",
                    height=4,
                    bg=COLORS["panel_alt"],
                    fg=COLORS["text"],
                    insertbackground=COLORS["text"],
                    relief="flat",
                    borderwidth=0,
                    highlightthickness=1,
                    highlightbackground=COLORS["grid"],
                    highlightcolor=COLORS["accent"],
                )
                text.grid(row=0, column=1, sticky="ew")
                text.insert("1.0", "\n".join(container.values))
                self.selection_value_text = text
                self.selection_value_entry = None
            apply_button = ttk.Button(editor_row, text="Apply", command=self._apply_selection_editor)
            apply_button.grid(row=0, column=2, sticky="e", padx=(8, 0))
            self.selection_apply_button = apply_button
            return
        if self.selection_state:
            ttk.Label(editor_row, text="Batch").grid(row=0, column=0, sticky="w", padx=(0, 8))
            ttk.Label(
                editor_row,
                text=f'{len(self.selection_state.get("variables", []))} v / {len(self.selection_state.get("arrays", []))} a',
                foreground=COLORS["muted"],
            ).grid(row=0, column=1, sticky="w")

    def _apply_selection_editor(self) -> None:
        if self.selected_container_name:
            container = self._find_container(self.selected_container_name)
            if not container:
                return
            if container.kind == "variable" and self.selection_value_entry:
                container.value = self.selection_value_entry.get().strip()
            elif container.kind == "array" and self.selection_value_text:
                container.values = [line.strip() for line in self.selection_value_text.get("1.0", tk.END).splitlines() if line.strip()]
            self._refresh_all()
            self._log(f"Applied inline edits to {container.name}.")

    def _selected_item_summary(self) -> str:
        if self.selection_state:
            variables = ", ".join(self.selection_state.get("variables", [])) or "-"
            arrays = ", ".join(self.selection_state.get("arrays", [])) or "-"
            return "\n".join(
                [
                    "Batch selection",
                    f"variables: {variables}",
                    f"arrays: {arrays}",
                    f"count: {len(self.selection_state.get('variables', [])) + len(self.selection_state.get('arrays', []))}",
                    "",
                    "Actions:",
                    "- Copy: store the current batch",
                    "- Merge: build a containerElement from the batch",
                    "- Delete: remove the selected containers",
                    "- Paste: duplicate copied containers on the canvas",
                ]
            )
        if self.selected_container_group_name:
            group = self._find_container_group(self.selected_container_group_name)
            if group:
                return self._container_group_detail_text(group.name)
        if self.selected_container_name:
            container = self._find_container(self.selected_container_name)
            if container:
                structure = self._container_structure_signature(container)
                if container.kind == "variable":
                    detail_line = f"value: {container.value or '-'}"
                else:
                    preview = ", ".join(self._container_value_preview(container, 4))
                    detail_line = f"values: {preview}"
                return "\n".join(
                    [
                        "Selected container",
                        f"name: {container.name}",
                        f"kind: {container.kind}",
                        f"count: {container.count}",
                        f"stride: {container.stride}",
                        f"structure: {structure}",
                        detail_line,
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
                return "\n".join(
                    [
                        "Selected resNode",
                        f"name: {res_node.name}",
                        f"size: {int(getattr(res_node, 'width', 0))} x {int(getattr(res_node, 'height', 0))}",
                        f"primary: {res_node.resource_kind or 'mesh'}",
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
                        f"expected input: {item.expected_input or '-'}",
                        f"expected output: {item.expected_output or '-'}",
                        f"script: {item.script or '-'}",
                    ]
                )
        if self.selected_function_text_name:
            item = self._find_function_text_item(self.selected_function_text_name)
            if item:
                return "\n".join(
                    [
                        "Selected text",
                        f"name: {item.name}",
                        f"function: {item.function_name or '-'}",
                        f"size: {int(getattr(item, 'width', 0))} x {int(getattr(item, 'height', 0))}",
                        f"text: {self._compact_activity_text(item.text, limit=180) or '-'}",
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

    def _current_batch_selection(self) -> dict[str, Any] | None:
        return self.selection_state

    def _copy_current_selection(self) -> None:
        selection = self._current_batch_selection()
        if not selection:
            self._log("No batch selection to copy.")
            return
        containers: list[dict[str, Any]] = []
        names = list(selection.get("variables", [])) + list(selection.get("arrays", []))
        for name in names:
            container = self._find_container(name)
            if container:
                containers.append(
                    {
                        "name": container.name,
                        "kind": container.kind,
                        "count": container.count,
                        "stride": container.stride,
                        "value": container.value,
                        "values": list(container.values),
                        "structure": list(container.structure),
                        "view_offset": container.view_offset,
                        "x": container.x,
                        "y": container.y,
                    }
                )
        self.selection_clipboard = {
            "containers": containers,
            "anchor_x": float(selection.get("rect", (0.0, 0.0, 0.0, 0.0))[0]),
            "anchor_y": float(selection.get("rect", (0.0, 0.0, 0.0, 0.0))[1]),
        }
        self._log(f"Copied {len(containers)} container(s) from the batch selection.")

    def _merge_current_selection(self) -> None:
        selection = self._current_batch_selection()
        if not selection:
            self._log("No batch selection to merge.")
            return
        name = self.selection_name_var.get().strip() or self.project.next_container_group_name()
        if self._find_container_group(name):
            raise AssertionError(f"ContainerElement {name} already exists.")
        rect = selection.get("rect", (0.0, 0.0, 0.0, 0.0))
        group = ContainerGroupItem(
            name=name,
            x=float(rect[0]),
            y=float(rect[1]),
            width=max(220.0, float(rect[2]) - float(rect[0])),
            height=max(160.0, float(rect[3]) - float(rect[1])),
        )
        group.variables = list(selection.get("variables", []))
        group.arrays = list(selection.get("arrays", []))
        self._pack_container_group_contents(group)
        self.project.validate_container_group(group)
        self.project.container_groups.append(group)
        self.selection_state = None
        self.selected_container_group_name = group.name
        self._refresh_all()
        self._log(f"Merged batch selection into containerElement {group.name}.")

    def _delete_current_selection(self) -> None:
        selection = self._current_batch_selection()
        if not selection:
            self._log("No batch selection to delete.")
            return
        names = list(selection.get("variables", [])) + list(selection.get("arrays", []))
        for name in names:
            self._remove_connections_for_node("container", name)
        self.project.containers = [item for item in self.project.containers if item.name not in names]
        self._sync_all_container_groups()
        self.selection_state = None
        self._refresh_all()
        self._log(f"Deleted {len(names)} selected container(s).")

    def _paste_selection_from_clipboard(self, x: float | None = None, y: float | None = None) -> None:
        clipboard = self.selection_clipboard
        if not clipboard:
            self._log("Clipboard is empty.")
            return
        anchor_x = float(clipboard.get("anchor_x", 0.0))
        anchor_y = float(clipboard.get("anchor_y", 0.0))
        paste_x = float(x if x is not None else anchor_x + 48.0)
        paste_y = float(y if y is not None else anchor_y + 48.0)
        created: list[str] = []
        for entry in clipboard.get("containers", []):
            kind = str(entry["kind"])
            name = self.project.next_container_name(kind)
            duplicate = ContainerItem(
                name=name,
                kind=kind,
                count=int(entry["count"]),
                stride=int(entry["stride"]),
                value=str(entry["value"]),
                values=[str(value) for value in entry.get("values", [])],
                structure=[str(value) for value in entry.get("structure", [])],
                view_offset=int(entry.get("view_offset", 0)),
                x=paste_x + float(entry["x"]) - anchor_x,
                y=paste_y + float(entry["y"]) - anchor_y,
            )
            self.project.containers.append(duplicate)
            created.append(duplicate.name)
        self._refresh_all()
        self._log(f"Pasted {len(created)} container(s) from clipboard.")

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

    def _find_function_text_item(self, name: str) -> FunctionTextItem | None:
        for item in self.project.function_text_items:
            if item.name == name:
                return item
        return None

    def _find_function_text_for_function(self, function_name: str) -> FunctionTextItem | None:
        for item in self.project.function_text_items:
            if item.function_name == function_name:
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
        if kind == "functiontext":
            return self._find_function_text_item(name)
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
        self._pack_container_group_contents(group)
        self.project.validate_container_group(group)
        self.project.container_groups.append(group)
        return group

    def _pack_container_group_contents(self, group: ContainerGroupItem) -> None:
        members: list[ContainerItem] = []
        for name in group.variables:
            container = self._find_container(name)
            if container:
                members.append(container)
        for name in group.arrays:
            container = self._find_container(name)
            if container:
                members.append(container)
        columns = 2
        margin_x = 16.0
        margin_y = 42.0
        cell_width = 130.0
        cell_height = 56.0
        for index, container in enumerate(members):
            column = index % columns
            row = index // columns
            container.x = group.x + margin_x + column * (cell_width + 12.0)
            container.y = group.y + margin_y + row * (cell_height + 12.0)
        row_count = (len(members) + columns - 1) // columns
        group.width = max(group.width, margin_x * 2 + columns * cell_width + (columns - 1) * 12.0)
        group.height = max(group.height, margin_y + row_count * cell_height + max(row_count - 1, 0) * 12.0 + 18.0)

    def _pack_all_container_groups(self) -> None:
        for group in sorted(self.project.container_groups, key=lambda item: item.width * item.height):
            self._pack_container_group_contents(group)

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
        self.project.function_text_items = [entry for entry in self.project.function_text_items if entry.function_name != item.name]
        self._remove_connections_for_node("function", item.name)
        self.selected_function_name = None
        self._refresh_all()
        self._log(f"Deleted function {item.name}.")

    def _delete_selected_function_text(self) -> None:
        item = self._current_function_text()
        if not item:
            return
        self.project.function_text_items = [entry for entry in self.project.function_text_items if entry.name != item.name]
        self.selected_function_text_name = None
        self._refresh_all()
        self._log(f"Deleted function text {item.name}.")

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
        self._delete_container_group_recursive(group.name)
        self.selected_container_group_name = None
        self._refresh_all()
        self._log(f"Deleted containerElement {group.name}.")

    def _delete_container_group_recursive(self, name: str) -> None:
        group = self._find_container_group(name)
        if not group:
            return
        for child_name in list(group.groups):
            self._delete_container_group_recursive(child_name)
        self._remove_connections_for_node("containerelement", name)
        for candidate in self.project.container_groups:
            if name in candidate.groups:
                candidate.groups = [value for value in candidate.groups if value != name]
        self.project.container_groups = [item for item in self.project.container_groups if item.name != name]

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

    def _current_function_text(self) -> FunctionTextItem | None:
        if self.selected_function_text_name:
            return self._find_function_text_item(self.selected_function_text_name)
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
                "",
                self._agent_document_context(),
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
            self.chat_history_text.configure(state="normal")
            self.chat_history_text.delete("1.0", tk.END)
            self.chat_history_text.configure(state="disabled")
        self.chat_rendered_images.clear()
        self._clear_chat_attachments()
        self._log("Chat history cleared.")

    def _chat_attachment_kind(self, path: Path) -> str:
        mime_type = mimetypes.guess_type(str(path))[0] or ""
        if mime_type.startswith("image/"):
            return "image"
        return "file"

    def _refresh_chat_attachment_summary(self) -> None:
        if not self.chat_attachments:
            self.chat_attachment_summary_var.set("No attachments.")
            return
        names = ", ".join(item["name"] for item in self.chat_attachments[:3])
        if len(self.chat_attachments) > 3:
            names += f" +{len(self.chat_attachments) - 3}"
        self.chat_attachment_summary_var.set(names)

    def _report_chat_attachment_state(self, source: str) -> None:
        self._refresh_chat_attachment_summary()
        if not self.chat_attachments:
            self.status_var.set("Ready.")
            return
        self.status_var.set(f"Attached {len(self.chat_attachments)} item(s) via {source}.")
        self._log(f"Attached {len(self.chat_attachments)} item(s) via {source}: {self.chat_attachment_summary_var.get()}")

    def _add_chat_attachments(self, paths: tuple[str, ...] | list[str], *, source: str) -> None:
        existing_paths = {str(item["path"]) for item in self.chat_attachments}
        for path_text in paths:
            path = Path(str(path_text).strip())
            if not path.exists():
                raise FileNotFoundError(f"Attachment not found: {path}")
            if not path.is_file():
                raise AssertionError(f"Attachment must be a file: {path}")
            normalized = str(path.resolve())
            if normalized in existing_paths:
                continue
            existing_paths.add(normalized)
            self.chat_attachments.append(
                {
                    "path": normalized,
                    "name": path.name,
                    "kind": self._chat_attachment_kind(path),
                    "mime_type": mimetypes.guess_type(str(path))[0] or "application/octet-stream",
                }
            )
        self._report_chat_attachment_state(source)

    def _clear_chat_attachments(self) -> None:
        self.chat_attachments.clear()
        self._report_chat_attachment_state("clear")

    def _widget_is_chat_input_target(self, widget: Any) -> bool:
        current = widget
        while current is not None:
            if current is self.chat_input_text:
                return True
            current = getattr(current, "master", None)
        return False

    def _install_chat_input_drop_target(self) -> None:
        if os.name != "nt":
            return
        if self.chat_input_text is None:
            raise AssertionError("Chat input box is not initialized.")
        if self._chat_drop_wndproc is not None:
            return
        self.root.update_idletasks()
        hwnd = int(self.root.winfo_id())
        user32 = ctypes.windll.user32
        shell32 = ctypes.windll.shell32
        shell32.DragAcceptFiles.argtypes = [wintypes.HWND, wintypes.BOOL]
        shell32.DragAcceptFiles.restype = None
        user32.GetWindowLongPtrW.argtypes = [wintypes.HWND, ctypes.c_int]
        user32.GetWindowLongPtrW.restype = ctypes.c_void_p
        user32.SetWindowLongPtrW.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.c_void_p]
        user32.SetWindowLongPtrW.restype = ctypes.c_void_p
        user32.CallWindowProcW.argtypes = [ctypes.c_void_p, wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        user32.CallWindowProcW.restype = LRESULT_TYPE
        shell32.DragQueryFileW.argtypes = [wintypes.HANDLE, wintypes.UINT, wintypes.LPWSTR, wintypes.UINT]
        shell32.DragQueryFileW.restype = wintypes.UINT
        shell32.DragQueryPoint.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.POINT)]
        shell32.DragQueryPoint.restype = wintypes.BOOL
        shell32.DragFinish.argtypes = [wintypes.HANDLE]
        shell32.DragFinish.restype = None
        wm_dropfiles = 0x0233
        gwlp_wndproc = -4
        old_wndproc = user32.GetWindowLongPtrW(hwnd, gwlp_wndproc)
        if not old_wndproc:
            raise OSError("Failed to read current window procedure.")
        wndproc_type = ctypes.WINFUNCTYPE(LRESULT_TYPE, wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM)

        def _chat_drop_wndproc(window_handle: int, message: int, wparam: int, lparam: int) -> int:
            if message == wm_dropfiles:
                drop_handle = wintypes.HANDLE(wparam)
                file_count = shell32.DragQueryFileW(drop_handle, 0xFFFFFFFF, None, 0)
                point = wintypes.POINT()
                if not shell32.DragQueryPoint(drop_handle, ctypes.byref(point)):
                    shell32.DragFinish(drop_handle)
                    raise AssertionError("Drop point is unavailable for WM_DROPFILES.")
                dropped_paths: list[str] = []
                for file_index in range(file_count):
                    buffer_length = shell32.DragQueryFileW(drop_handle, file_index, None, 0) + 1
                    buffer = ctypes.create_unicode_buffer(buffer_length)
                    shell32.DragQueryFileW(drop_handle, file_index, buffer, buffer_length)
                    dropped_paths.append(buffer.value)
                shell32.DragFinish(drop_handle)
                screen_x = self.root.winfo_rootx() + int(point.x)
                screen_y = self.root.winfo_rooty() + int(point.y)
                self.root.after(
                    0,
                    lambda paths=dropped_paths, x=screen_x, y=screen_y: self._handle_chat_external_drop(paths, x, y),
                )
                return 0
            return user32.CallWindowProcW(old_wndproc, window_handle, message, wparam, lparam)

        wndproc = wndproc_type(_chat_drop_wndproc)
        new_wndproc_pointer = ctypes.cast(wndproc, ctypes.c_void_p).value
        previous_wndproc = user32.SetWindowLongPtrW(hwnd, gwlp_wndproc, new_wndproc_pointer)
        if not previous_wndproc:
            raise OSError("Failed to install drop target window procedure.")
        shell32.DragAcceptFiles(hwnd, True)
        self._chat_drop_hwnd = hwnd
        self._chat_drop_old_wndproc = previous_wndproc
        self._chat_drop_wndproc = wndproc

    def _uninstall_chat_input_drop_target(self) -> None:
        if os.name != "nt":
            return
        if self._chat_drop_hwnd is None or self._chat_drop_old_wndproc is None:
            return
        user32 = ctypes.windll.user32
        shell32 = ctypes.windll.shell32
        shell32.DragAcceptFiles.argtypes = [wintypes.HWND, wintypes.BOOL]
        shell32.DragAcceptFiles.restype = None
        user32.SetWindowLongPtrW.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.c_void_p]
        user32.SetWindowLongPtrW.restype = ctypes.c_void_p
        shell32.DragAcceptFiles(self._chat_drop_hwnd, False)
        restored = user32.SetWindowLongPtrW(self._chat_drop_hwnd, -4, self._chat_drop_old_wndproc)
        if not restored:
            raise OSError("Failed to restore original window procedure.")
        self._chat_drop_hwnd = None
        self._chat_drop_old_wndproc = None
        self._chat_drop_wndproc = None

    def _handle_chat_external_drop(self, paths: list[str], screen_x: int, screen_y: int) -> None:
        widget = self.root.winfo_containing(screen_x, screen_y)
        if not self._widget_is_chat_input_target(widget):
            self.status_var.set("Drop ignored outside the chat input.")
            return
        if not paths:
            raise AssertionError("External drop did not contain any file paths.")
        self._add_chat_attachments(paths, source="drop")
        if self.chat_input_text:
            self.chat_input_text.focus_set()

    def _clipboard_file_paths(self) -> list[str]:
        if os.name != "nt":
            raise AssertionError("Clipboard file attachments are only supported on Windows.")
        user32 = ctypes.windll.user32
        shell32 = ctypes.windll.shell32
        cf_hdrop = 15
        if not user32.OpenClipboard(None):
            raise OSError("Failed to open the clipboard.")
        try:
            if not user32.IsClipboardFormatAvailable(cf_hdrop):
                return []
            drop_handle = user32.GetClipboardData(cf_hdrop)
            if not drop_handle:
                raise OSError("Clipboard reported file data but returned an invalid handle.")
            shell32.DragQueryFileW.argtypes = [wintypes.HANDLE, wintypes.UINT, wintypes.LPWSTR, wintypes.UINT]
            shell32.DragQueryFileW.restype = wintypes.UINT
            file_count = shell32.DragQueryFileW(drop_handle, 0xFFFFFFFF, None, 0)
            paths: list[str] = []
            for file_index in range(file_count):
                buffer_length = shell32.DragQueryFileW(drop_handle, file_index, None, 0) + 1
                buffer = ctypes.create_unicode_buffer(buffer_length)
                shell32.DragQueryFileW(drop_handle, file_index, buffer, buffer_length)
                paths.append(buffer.value)
            return paths
        finally:
            if not user32.CloseClipboard():
                raise OSError("Failed to close the clipboard.")

    def _clipboard_has_image(self) -> bool:
        if os.name != "nt":
            raise AssertionError("Clipboard image attachments are only supported on Windows.")
        user32 = ctypes.windll.user32
        cf_bitmap = 2
        cf_dib = 8
        cf_dibv5 = 17
        if not user32.OpenClipboard(None):
            raise OSError("Failed to open the clipboard.")
        try:
            return bool(
                user32.IsClipboardFormatAvailable(cf_bitmap)
                or user32.IsClipboardFormatAvailable(cf_dib)
                or user32.IsClipboardFormatAvailable(cf_dibv5)
            )
        finally:
            if not user32.CloseClipboard():
                raise OSError("Failed to close the clipboard.")

    def _save_clipboard_image_to_file(self) -> str:
        if os.name != "nt":
            raise AssertionError("Clipboard image attachments are only supported on Windows.")
        fd, path_text = tempfile.mkstemp(prefix="algorithm_studio_clipboard_", suffix=".png")
        os.close(fd)
        output_path = Path(path_text)
        output_path.unlink(missing_ok=True)
        command = [
            "powershell.exe",
            "-NoProfile",
            "-STA",
            "-Command",
            "& { param([string]$OutPath) "
            "Add-Type -AssemblyName System.Windows.Forms; "
            "Add-Type -AssemblyName System.Drawing; "
            "$image = [System.Windows.Forms.Clipboard]::GetImage(); "
            "if ($null -eq $image) { exit 3 }; "
            "$image.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Png) }",
            str(output_path),
        ]
        completed = subprocess.run(
            command,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="strict",
            check=False,
        )
        if completed.returncode != 0:
            output_path.unlink(missing_ok=True)
            stderr = (completed.stderr or "").strip()
            stdout = (completed.stdout or "").strip()
            raise RuntimeError(f"Failed to read image data from the clipboard: {stderr or stdout or completed.returncode}")
        if not output_path.exists():
            raise AssertionError("Clipboard image export completed without creating a file.")
        return str(output_path)

    def _handle_chat_paste_event(self, event: tk.Event) -> str | None:
        file_paths = self._clipboard_file_paths()
        if file_paths:
            self._add_chat_attachments(file_paths, source="clipboard")
            return "break"
        if self._clipboard_has_image():
            image_path = self._save_clipboard_image_to_file()
            self._add_chat_attachments([image_path], source="clipboard")
            return "break"
        return None

    def _open_chat_attachment(self, path_text: str) -> None:
        path = Path(path_text)
        if not path.exists():
            raise FileNotFoundError(f"Attachment not found: {path}")
        os.startfile(str(path))

    def _resolve_chat_output_attachment(self, candidate: str) -> dict[str, str] | None:
        path_text = str(candidate).strip().strip("`").strip()
        if not path_text:
            return None
        if path_text.startswith("<") and path_text.endswith(">"):
            path_text = path_text[1:-1].strip()
        lowered = path_text.lower()
        if "://" in path_text and not lowered.startswith("file://"):
            return None
        if lowered.startswith("file://"):
            parsed = urlparse(path_text)
            parsed_path = unquote(parsed.path or "").strip()
            if parsed_path.startswith("/") and len(parsed_path) > 2 and parsed_path[2] == ":":
                parsed_path = parsed_path[1:]
            path_text = parsed_path
        path = Path(path_text)
        if not path.is_absolute():
            path = (PROJECT_ROOT / path_text).resolve()
        if not path.exists():
            return None
        return {
            "path": str(path),
            "name": path.name,
            "kind": self._chat_attachment_kind(path),
            "mime_type": mimetypes.guess_type(str(path))[0] or "application/octet-stream",
        }

    def _insert_chat_attachment_link(self, tag: str, attachment: dict[str, str], index: int) -> None:
        if not self.chat_history_text:
            raise AssertionError("Chat history box is not initialized.")
        line_tag = f"{tag}_attachment_{index}_{int(time.time() * 1000)}"
        label = f"[{attachment['kind']}] {attachment['name']}"
        start_index = self.chat_history_text.index(tk.END)
        self.chat_history_text.insert(tk.END, label + "\n", (tag,))
        end_index = self.chat_history_text.index(tk.END)
        self.chat_history_text.tag_add(line_tag, start_index, end_index)
        self.chat_history_text.tag_configure(
            line_tag,
            foreground=COLORS["accent"] if attachment["kind"] == "image" else COLORS["muted"],
            underline=True,
        )
        self.chat_history_text.tag_bind(line_tag, "<Button-1>", lambda _event, path=attachment["path"]: self._open_chat_attachment(path))

    def _insert_chat_attachment_preview(self, attachment: dict[str, str]) -> None:
        if not self.chat_history_text or attachment["kind"] != "image":
            return
        try:
            image = tk.PhotoImage(file=attachment["path"])
        except tk.TclError:
            return
        max_width = 280
        max_height = 180
        factor_x = max(1, (image.width() + max_width - 1) // max_width)
        factor_y = max(1, (image.height() + max_height - 1) // max_height)
        factor = max(factor_x, factor_y)
        if factor > 1:
            image = image.subsample(factor, factor)
        self.chat_rendered_images.append(image)
        self.chat_history_text.image_create(tk.END, image=image, padx=16, pady=4)
        self.chat_history_text.insert(tk.END, "\n")

    def _insert_chat_code_block(self, tag: str, language: str, code: str) -> None:
        if not self.chat_history_text:
            raise AssertionError("Chat history box is not initialized.")
        block_name = language.strip().lower() or "code"
        self.chat_history_text.insert(tk.END, f"[{block_name}]\n", (tag, "chat_code_label"))
        block_text = code.rstrip("\n")
        if block_text:
            self.chat_history_text.insert(tk.END, block_text + "\n", (tag, "chat_code"))

    def _render_chat_message_content(self, tag: str, content: str) -> None:
        if not self.chat_history_text:
            raise AssertionError("Chat history box is not initialized.")
        if not content:
            return
        code_block_pattern = re.compile(r"```([^\n`]*)\n(.*?)```", re.DOTALL)
        cursor = 0
        for match in code_block_pattern.finditer(content):
            plain_text = content[cursor:match.start()]
            if plain_text:
                self.chat_history_text.insert(tk.END, plain_text, (tag,))
            self._insert_chat_code_block(tag, match.group(1), match.group(2))
            cursor = match.end()
        tail = content[cursor:]
        if tail:
            self.chat_history_text.insert(tk.END, tail, (tag,))

    def _detect_output_attachments(self, content: str) -> list[dict[str, str]]:
        attachments: list[dict[str, str]] = []
        seen_paths: set[str] = set()

        def add_candidate(candidate: str) -> None:
            attachment = self._resolve_chat_output_attachment(candidate)
            if attachment is None:
                return
            normalized = attachment["path"]
            if normalized in seen_paths:
                return
            seen_paths.add(normalized)
            attachments.append(attachment)

        for match in re.finditer(r"!\[[^\]]*\]\(([^)]+)\)", content):
            add_candidate(match.group(1))
        for match in re.finditer(r"(?<!!)\[[^\]]+\]\(([^)]+)\)", content):
            add_candidate(match.group(1))
        for raw_line in content.splitlines():
            line = raw_line.strip().strip("`")
            if not line:
                continue
            lowered = line.lower()
            if Path(line).is_absolute() or lowered.startswith("file://") or line.startswith("./") or line.startswith(".\\") or line.startswith("../") or line.startswith("..\\"):
                add_candidate(line)
        return attachments

    def _append_chat_message(
        self,
        role: str,
        content: str,
        *,
        attachments: list[dict[str, str]] | None = None,
    ) -> None:
        message_attachments = [dict(item) for item in attachments or []]
        if role == "assistant" and not message_attachments:
            message_attachments = self._detect_output_attachments(content)
        message = {"role": role, "content": content, "attachments": message_attachments}
        self.chat_history.append(message)
        if not self.chat_history_text:
            return
        was_pinned = self._chat_history_is_pinned_to_bottom()
        self.chat_history_text.configure(state="normal")
        tag = role if role in {"user", "assistant", "system", "error"} else "system"
        rendered_content = content.rstrip()
        if rendered_content:
            self._render_chat_message_content(tag, rendered_content)
            self.chat_history_text.insert(tk.END, "\n", (tag,))
        for index, attachment in enumerate(message_attachments):
            self._insert_chat_attachment_link(tag, attachment, index)
            self._insert_chat_attachment_preview(attachment)
        self.chat_history_text.insert(tk.END, "\n", (tag,))
        self.chat_history_text.configure(state="disabled")
        self._restore_chat_history_bottom_pin(was_pinned)

    def _chat_history_transcript_line(self, item: dict[str, Any]) -> str:
        role = str(item.get("role") or "system").capitalize()
        content = str(item.get("content") or "").strip() or "(no text)"
        attachments = item.get("attachments") or []
        if attachments:
            names = ", ".join(str(entry.get("name") or Path(str(entry.get("path") or "")).name) for entry in attachments)
            return f"{role}: {content} [Attachments: {names}]"
        return f"{role}: {content}"

    def _chat_history_for_model(self) -> str:
        items = [item for item in self.chat_history if item["role"] in {"user", "assistant"}]
        if not items:
            return "(empty)"
        if len(items) <= CHAT_HISTORY_FULL_TURN_LIMIT:
            return "\n".join(self._compact_activity_text(self._chat_history_transcript_line(item), limit=220) for item in items)
        older = items[:-CHAT_HISTORY_RECENT_TURN_COUNT]
        recent = items[-CHAT_HISTORY_RECENT_TURN_COUNT:]
        older_preview = older[-CHAT_HISTORY_RECENT_TURN_COUNT:]
        older_lines = [self._compact_activity_text(self._chat_history_transcript_line(item), limit=140) for item in older_preview]
        older_lines = [line for line in older_lines if line]
        summary_lines = [
            f"Earlier compressed history: {len(older)} turn(s).",
            *older_lines,
        ]
        recent_lines = [self._compact_activity_text(self._chat_history_transcript_line(item), limit=220) for item in recent]
        recent_lines = [line for line in recent_lines if line]
        return "\n".join(summary_lines + ["Recent turns:"] + recent_lines)

    def _chat_history_is_pinned_to_bottom(self) -> bool:
        if not self.chat_history_text:
            return False
        _first, last = self.chat_history_text.yview()
        return last >= 0.999

    def _restore_chat_history_bottom_pin(self, was_pinned: bool) -> None:
        if was_pinned and self.chat_history_text:
            self.chat_history_text.see(tk.END)

    def _clear_execution_trace(self) -> None:
        self._cancel_execution_elapsed_timer()
        self.execution_started_at = None
        self.execution_summary_var.set("No execution trace yet.")
        self.execution_current_run = None

    def _cancel_execution_elapsed_timer(self) -> None:
        if self.execution_elapsed_after_id is None:
            return
        try:
            self.root.after_cancel(self.execution_elapsed_after_id)
        except tk.TclError:
            pass
        self.execution_elapsed_after_id = None

    def _schedule_execution_elapsed_timer(self) -> None:
        self._cancel_execution_elapsed_timer()
        if self.execution_current_run is None:
            return
        self.execution_elapsed_after_id = self.root.after(250, self._tick_execution_elapsed)

    def _tick_execution_elapsed(self) -> None:
        self.execution_elapsed_after_id = None
        run = self.execution_current_run
        if not run:
            return
        self._refresh_activity_block(run)
        self._schedule_execution_elapsed_timer()

    def _find_execution_run(self, block_id: str) -> dict[str, Any] | None:
        for run in self.execution_runs:
            if str(run.get("id")) == block_id:
                return run
        return None

    def _toggle_activity_block(self, block_id: str) -> None:
        if not self.chat_history_text:
            return
        run = self._find_execution_run(block_id)
        if not run:
            return
        self._set_activity_block_collapsed(run, not bool(run.get("collapsed", False)))

    def _set_activity_block_collapsed(self, run: dict[str, Any], collapsed: bool) -> None:
        if not self.chat_history_text:
            return
        was_pinned = self._chat_history_is_pinned_to_bottom()
        self.chat_history_text.configure(state="normal")
        self.chat_history_text.tag_configure(str(run["meta_tag"]), elide=collapsed)
        self.chat_history_text.tag_configure(str(run["body_tag"]), elide=collapsed)
        self.chat_history_text.configure(state="disabled")
        self._restore_chat_history_bottom_pin(was_pinned)
        run["collapsed"] = collapsed

    def _format_execution_elapsed(self, elapsed: float) -> str:
        return f"{max(0.0, elapsed):.1f}s"

    def _compact_activity_text(self, text: str, *, limit: int = 96) -> str:
        compact = " ".join(part.strip() for part in str(text).splitlines() if part.strip())
        if not compact:
            return ""
        if len(compact) > limit:
            return compact[: limit - 3].rstrip() + "..."
        return compact

    def _activity_label(self, title: str) -> str:
        normalized = self._compact_activity_text(title, limit=24).lower()
        if "chat" in normalized or "聊天" in normalized:
            return "聊天"
        if "function" in normalized or "函数" in normalized:
            return "函数"
        if "approval" in normalized or "审批" in normalized:
            return "审批"
        return self._compact_activity_text(title, limit=24) or "任务"

    def _replace_activity_line(self, tag: str, text: str) -> None:
        if not self.chat_history_text:
            raise AssertionError("Chat history box is not initialized.")
        ranges = self.chat_history_text.tag_ranges(tag)
        if len(ranges) != 2:
            raise AssertionError(f"Expected a single tagged range for {tag}.")
        start, end = ranges
        self.chat_history_text.delete(start, end)
        self.chat_history_text.insert(start, f"{text}\n", (tag,))

    def _activity_header_text(self, run: dict[str, Any], *, elapsed: float | None = None) -> str:
        if elapsed is None:
            started_at_perf = run.get("started_at_perf")
            elapsed = 0.0 if started_at_perf is None else time.perf_counter() - float(started_at_perf)
        state = str(run.get("state") or "running")
        if state == "running":
            prefix = f"执行中 {self._format_execution_elapsed(elapsed)}"
        elif state == "done":
            prefix = f"已完成 {self._format_execution_elapsed(elapsed)}"
        else:
            prefix = f"失败 {self._format_execution_elapsed(elapsed)}"
        last_reasoning = self._compact_activity_text(str(run.get("last_reasoning") or ""), limit=72)
        last_result = self._compact_activity_text(str(run.get("last_result") or ""), limit=72)
        last_error = self._compact_activity_text(str(run.get("last_error") or ""), limit=72)
        if state == "error" and last_error:
            summary = last_error
        elif last_reasoning:
            summary = last_reasoning
        elif state != "running" and last_result:
            summary = last_result
        else:
            summary = self._activity_label(str(run.get("title") or "task"))
        return self._compact_activity_text(f"{prefix} · {summary}", limit=140)

    def _activity_meta_text(self, run: dict[str, Any]) -> str:
        parts = [f"开始于 {run.get('started_at_text', '--:--:--')}"]
        parts.append("点击展开查看详情")
        return self._compact_activity_text(" · ".join(parts), limit=140)

    def _refresh_activity_block(self, run: dict[str, Any], *, elapsed: float | None = None) -> None:
        if not self.chat_history_text:
            raise AssertionError("Chat history box is not initialized.")
        header_text = self._activity_header_text(run, elapsed=elapsed)
        meta_text = self._activity_meta_text(run)
        was_pinned = self._chat_history_is_pinned_to_bottom()
        self.chat_history_text.configure(state="normal")
        self._replace_activity_line(str(run["header_tag"]), header_text)
        self._replace_activity_line(str(run["meta_tag"]), meta_text)
        self.chat_history_text.configure(state="disabled")
        self._restore_chat_history_bottom_pin(was_pinned)
        run["preview_lines"] = [header_text]
        self.execution_summary_var.set(header_text)

    def _set_activity_block_preview(self, run: dict[str, Any], lines: list[str]) -> None:
        preview_lines = [self._compact_activity_text(line, limit=96) for line in lines if self._compact_activity_text(line, limit=96)]
        if preview_lines:
            run["last_reasoning"] = preview_lines[-1]
        self._refresh_activity_block(run)

    def _promote_activity_preview(self, message: str, tag: str) -> None:
        run = self.execution_current_run
        if not run:
            return
        text = self._compact_activity_text(message, limit=96)
        if not text:
            return
        if tag == "reasoning":
            if text == str(run.get("last_reasoning") or ""):
                return
            run["last_reasoning"] = text
        elif tag == "error":
            if text == str(run.get("last_error") or ""):
                return
            run["last_error"] = text
        elif tag in {"result", "done"}:
            if text == str(run.get("last_result") or ""):
                return
            run["last_result"] = text
        self._refresh_activity_block(run)

    def _append_activity_block(
        self,
        title: str,
        detail_lines: list[str],
        *,
        collapsed: bool = False,
    ) -> dict[str, Any] | None:
        if not self.chat_history_text:
            return None
        block_id = f"{len(self.execution_runs) + 1}_{int(time.time() * 1000)}"
        header_tag = f"activity_header_{block_id}"
        meta_tag = f"activity_meta_{block_id}"
        body_tag = f"activity_body_{block_id}"
        lines = [self._compact_activity_text(line, limit=96) for line in detail_lines[:2]]
        lines = [line for line in lines if line]
        run = {
            "id": block_id,
            "title": title,
            "header_tag": header_tag,
            "meta_tag": meta_tag,
            "body_tag": body_tag,
            "collapsed": collapsed,
            "preview_lines": [],
            "state": "running",
            "started_at_perf": time.perf_counter(),
            "started_at_text": f"{datetime.now():%H:%M:%S}",
            "last_reasoning": lines[0] if lines else "",
            "last_result": "",
            "last_error": "",
            "last_body_entry": "",
        }
        header_text = self._activity_header_text(run, elapsed=0.0)
        meta_text = self._activity_meta_text(run)
        was_pinned = self._chat_history_is_pinned_to_bottom()
        self.chat_history_text.configure(state="normal")
        self.chat_history_text.insert(tk.END, f"{header_text}\n", (header_tag,))
        self.chat_history_text.insert(tk.END, f"{meta_text}\n", (meta_tag,))
        self.chat_history_text.tag_configure(
            header_tag,
            foreground=COLORS["muted"],
            justify="left",
            lmargin1=12,
            lmargin2=12,
            rmargin=120,
            spacing1=2,
            spacing3=1,
            font=("Segoe UI", 10),
        )
        self.chat_history_text.tag_configure(
            meta_tag,
            foreground=COLORS["muted"],
            justify="left",
            lmargin1=24,
            lmargin2=24,
            rmargin=120,
            spacing3=4,
            font=("Segoe UI", 8),
            elide=collapsed,
        )
        self.chat_history_text.tag_configure(
            body_tag,
            foreground=COLORS["muted"],
            justify="left",
            lmargin1=24,
            lmargin2=24,
            rmargin=120,
            spacing3=4,
            font=("Segoe UI", 8),
            elide=collapsed,
        )
        self.chat_history_text.configure(state="disabled")
        self._restore_chat_history_bottom_pin(was_pinned)
        self.chat_history_text.tag_bind(header_tag, "<Button-1>", lambda _event, current_block_id=block_id: self._toggle_activity_block(current_block_id))
        self.chat_history_text.tag_bind(meta_tag, "<Button-1>", lambda _event, current_block_id=block_id: self._toggle_activity_block(current_block_id))
        self.execution_runs.append(run)
        self.execution_current_run = run
        self._set_activity_block_collapsed(run, collapsed)
        return run

    def _start_execution_trace(self, title: str, detail: str | list[str]) -> None:
        if isinstance(detail, str):
            detail_lines = [line.strip() for line in detail.splitlines() if line.strip()]
        else:
            detail_lines = [str(line).strip() for line in detail if str(line).strip()]
        self._clear_execution_trace()
        self.execution_started_at = time.perf_counter()
        run = self._append_activity_block(title, detail_lines[:1], collapsed=True)
        if run:
            self._refresh_activity_block(run, elapsed=0.0)
            if detail_lines[:1]:
                self._append_execution_trace(detail_lines[0], "reasoning")
        self._schedule_execution_elapsed_timer()

    def _append_execution_trace(self, message: str, tag: str = "result") -> None:
        run = self.execution_current_run
        if not run or not self.chat_history_text:
            return
        if tag not in {"reasoning", "error", "result", "done"}:
            return
        body_tag = run["body_tag"]
        text = self._compact_activity_text(message, limit=120)
        if not text:
            return
        if tag == "error":
            prefix = "错误: "
        elif tag in {"result", "done"}:
            prefix = "结果: "
        else:
            prefix = ""
        body_entry = f"{prefix}{text}"
        if body_entry == str(run.get("last_body_entry") or ""):
            return
        run["last_body_entry"] = body_entry
        was_pinned = self._chat_history_is_pinned_to_bottom()
        self.chat_history_text.configure(state="normal")
        self.chat_history_text.insert(tk.END, f"{body_entry}\n", (body_tag,))
        self.chat_history_text.configure(state="disabled")
        self._restore_chat_history_bottom_pin(was_pinned)

    def _finish_execution_trace(self, title: str, ok: bool, detail: str | None = None) -> None:
        finished_at = datetime.now()
        elapsed = 0.0 if self.execution_started_at is None else time.perf_counter() - self.execution_started_at
        self._cancel_execution_elapsed_timer()
        self.execution_started_at = None
        run = self.execution_current_run
        if run and self.chat_history_text:
            run["state"] = "done" if ok else "error"
            run["finished_at_text"] = f"{finished_at:%H:%M:%S}"
            if detail:
                if ok:
                    run["last_result"] = self._compact_activity_text(detail, limit=96)
                else:
                    run["last_error"] = self._compact_activity_text(detail, limit=96)
                self._append_execution_trace(detail, "result" if ok else "error")
            self.chat_history_text.tag_configure(run["header_tag"], foreground=COLORS["bad"] if not ok else COLORS["muted"])
            self.chat_history_text.tag_configure(run["meta_tag"], foreground=COLORS["bad"] if not ok else COLORS["muted"])
            self._refresh_activity_block(run, elapsed=elapsed)
            self._set_activity_block_collapsed(run, True)
        self.execution_current_run = None

    def _handle_agent_event(self, event: dict[str, str]) -> None:
        event_type = str(event.get("type") or "").strip()
        if event_type == "activity.start":
            summary = str(event.get("summary") or "").strip()
            detail = str(event.get("detail") or "").strip()
            lines = [line for line in [summary, detail] if line]
            self._start_execution_trace(str(event.get("title") or "请求"), lines)
            return
        if event_type == "activity.update":
            detail = str(event.get("detail") or "").strip()
            if detail:
                tag = str(event.get("tag") or "tool_detail")
                if tag == "result":
                    return
                if tag not in {"reasoning", "error", "done"}:
                    return
                self._promote_activity_preview(detail, tag)
                self._append_execution_trace(detail, tag)
            return

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
        attachments = [dict(item) for item in self.chat_attachments]
        if not prompt and not attachments:
            raise AssertionError("Chat prompt is empty.")
        prompt_for_model = prompt or "Please inspect the attached content."
        self._sync_agent_client_settings()
        selection = self._selection_label()
        context = self._chat_selected_context()
        transcript = self._chat_history_for_model()
        final_prompt = "\n\n".join(
            [
                context,
                "Conversation history:",
                transcript,
                "User request:",
                prompt_for_model,
            ]
        ).strip()
        self.agent_prompt_var.set(final_prompt)
        try:
            approved = self._authorize_chat_request(selection, final_prompt)
        except Exception as exc:  # noqa: BLE001
            message = self._compact_activity_text(str(exc), limit=120) or "审批检查失败。"
            self._finish_execution_trace("聊天请求", False, f"审批失败：{message}")
            self._append_chat_message("error", message)
            self._log(f"Approval check failed: {message}")
            self.status_var.set("Approval check failed.")
            messagebox.showerror("Approval error", message)
            return
        if not approved:
            self._finish_execution_trace("聊天请求", False, "审批未通过。")
            return
        user_visible_content = prompt or "(attached content)"
        self._append_chat_message("user", user_visible_content, attachments=attachments)
        self.chat_input_text.delete("1.0", tk.END)
        self._clear_chat_attachments()
        self.chat_busy = True
        if self.chat_send_button:
            self.chat_send_button.configure(state="disabled")
        self.status_var.set(f"Sent to {self.agent_client.provider}; waiting for reply...")
        self._log(f"Chat request sent via {self.agent_client.provider}.")

        def emit_event(event: dict[str, str]) -> None:
            self.root.after(0, lambda event=event: self._handle_agent_event(event))

        def worker() -> None:
            try:
                response = self.agent_client.generate(
                    self.project,
                    selection,
                    final_prompt,
                    attachments=attachments,
                    event_callback=emit_event,
                )
            except Exception as exc:  # noqa: BLE001
                self.root.after(0, lambda exc=exc: self._finish_chat_request_error(exc))
                return
            self.root.after(0, lambda response=response: self._finish_chat_request_success(response))

        import threading

        thread = threading.Thread(target=worker, daemon=True)
        thread.start()

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
            self._log("Manual approval mode bypassed for chat request.")
            return True
        if mode == "rules":
            rule_set = load_access_rules(self.access_rules_path)
            decision = evaluate_access_rules(rule_set, approval_summary)
            if decision.outcome == "deny":
                self._log(f"Chat request denied by rules: {decision.reason}")
                self.status_var.set("Request denied by access rules.")
                messagebox.showwarning("Approval denied", decision.reason)
                return False
            if decision.outcome == "manual":
                self._log(f"Rule mode fell back to auto-approve: {decision.reason}")
                return True
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
        try:
            visible_response = self._consume_agent_tool_response(response)
        except Exception as exc:  # noqa: BLE001
            self._finish_chat_request_error(exc)
            return
        self._append_chat_message("assistant", visible_response)
        self.agent_output_var.set(visible_response)
        self._finish_execution_trace("聊天请求", True, f"已收到 {len(response.strip())} 个字符的回复。")
        self._log(f"Agent call completed via {self.agent_client.provider}.")
        self.status_var.set(f"Agent reply received from {self.agent_client.provider}.")

    def _finish_chat_request_error(self, exc: Exception) -> None:
        self.chat_busy = False
        if self.chat_send_button:
            self.chat_send_button.configure(state="normal")
        message = self._compact_activity_text(str(exc), limit=120) or "Agent 调用失败。"
        self._append_chat_message("error", message)
        self.agent_output_var.set(message)
        self._finish_execution_trace("聊天请求", False, f"执行失败：{message}")
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
        if self.selected_function_text_name:
            return f"functiontext:{self.selected_function_text_name}"
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
        self.selection_state = None
        container = self._find_container(name)
        group = self._find_container_group(name)
        reflector = self._find_reflector(name)
        res_node = self._find_res_node(name)
        function_frame = self._find_function_frame(name)
        function_text = self._find_function_text_item(name)
        stage = self._find_stage(name)
        rule = self._find_rule(name)
        self.selected_container_name = None
        self.selected_rule_name = None
        self.selected_container_group_name = None
        self.selected_reflector_name = None
        self.selected_res_node_name = None
        self.selected_function_name = None
        self.selected_function_text_name = None
        self.selected_stage_name = None
        if kind == "container" and container:
            self.selected_container_name = container.name
        elif kind == "decomposer" and rule:
            self.selected_rule_name = rule.name
        elif kind == "containerelement" and group:
            self.selected_container_group_name = group.name
        elif kind == "reflector" and reflector:
            self.selected_reflector_name = reflector.name
        elif kind == "resnode" and res_node:
            self.selected_res_node_name = res_node.name
        elif kind == "function" and function_frame:
            self.selected_function_name = function_frame.name
        elif kind == "functiontext" and function_text:
            self.selected_function_text_name = function_text.name
        elif kind == "interventioner" and stage:
            self.selected_stage_name = stage.name
        elif kind == "stage" and stage:
            self.selected_stage_name = stage.name
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
                    "scene_x": self._scene_point(event.x, event.y)[0],
                    "scene_y": self._scene_point(event.x, event.y)[1],
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
                        "scene_x0": self._scene_point(event.x, event.y)[0],
                        "scene_y0": self._scene_point(event.x, event.y)[1],
                        "scene_x1": self._scene_point(event.x, event.y)[0],
                        "scene_y1": self._scene_point(event.x, event.y)[1],
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
            elif kind in {"decomposer", "reflector", "resnode", "function", "functiontext", "interventioner", "stage"}:
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
            self.selection_state = None
            self.selected_container_name = None
            self.selected_rule_name = None
            self.selected_reflector_name = None
            self.selected_res_node_name = None
            self.selected_function_name = None
            self.selected_stage_name = None
            self.selected_function_text_name = None
            self.selected_container_group_name = None
            self._redraw_canvas()
            self.marquee_state = {
                "x0": event.x,
                "y0": event.y,
                "x1": event.x,
                "y1": event.y,
                "scene_x0": self._scene_point(event.x, event.y)[0],
                "scene_y0": self._scene_point(event.x, event.y)[1],
                "scene_x1": self._scene_point(event.x, event.y)[0],
                "scene_y1": self._scene_point(event.x, event.y)[1],
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
            scene_x1, scene_y1 = self._scene_point(event.x, event.y)
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
            min_width = 220.0
            min_height = 160.0
            new_width = max(min_width, float(self.container_group_resize_state["width"]) + self._scene_delta(event.x - float(self.container_group_resize_state["x"])))
            new_height = max(min_height, float(self.container_group_resize_state["height"]) + self._scene_delta(event.y - float(self.container_group_resize_state["y"])))
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
        elif kind == "interventioner" or kind == "stage":
            stage = self._find_stage(name)
            if stage:
                stage.x += dx
                stage.y += dy
        elif kind == "functiontext":
            item = self._find_function_text_item(name)
            if item:
                item.x += dx
                item.y += dy
        self._sync_project_manifest_cache()
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
                else:
                    variables, arrays = self._members_inside_rect(rect)
                if not variables and not arrays:
                    self._log("Marquee selection did not include any variables or arrays.")
                    self.selection_state = None
                    self._refresh_all()
                else:
                    self.selection_state = {
                        "variables": variables,
                        "arrays": arrays,
                        "rect": rect,
                        "scope_group": scope_group_name,
                        "suggested_name": self.project.next_container_group_name(),
                    }
                    self.selected_container_name = None
                    self.selected_rule_name = None
                    self.selected_reflector_name = None
                    self.selected_res_node_name = None
                    self.selected_stage_name = None
                    self.selected_container_group_name = None
                    self._refresh_all()
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
            self._pack_all_container_groups()
            self.node_drag_state = None
            self.container_group_drag_state = None
            self.container_group_resize_state = None
            self.toolnode_resize_state = None
            self._refresh_all()
            return
        self.node_drag_state = None

    def _on_canvas_double_click(self, event: tk.Event) -> None:
        if not self.canvas:
            return
        item_id = self._canvas_item_hit(event.x, event.y)
        if item_id is None:
            return
        tags = self.canvas.gettags(item_id)
        kind, node_name = self._node_info_from_tags(tags)
        if not kind or not node_name:
            return
        self._select_item_on_canvas(kind, node_name)
        if kind == "container":
            container = self._find_container(node_name)
            if not container:
                raise AssertionError(f"Missing container {node_name}")
            self._open_container_editor(container, focus_section=self._container_section_from_tags(tags))
            return
        if kind == "function":
            item = self._find_function_frame(node_name)
            if not item:
                raise AssertionError(f"Missing function {node_name}")
            self._open_function_editor(item)
            return
        if kind == "functiontext":
            item = self._find_function_text_item(node_name)
            if not item:
                raise AssertionError(f"Missing function text {node_name}")
            self._open_function_text_editor(item)
            return

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
        elif kind == "functiontext":
            menu.add_command(label="Delete", command=self._delete_selected_function_text)
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

    def _container_section_from_tags(self, tags: tuple[str, ...]) -> str | None:
        for tag in tags:
            if tag.startswith("container_section:"):
                section = tag.split(":", 1)[1].strip()
                if section in {"structure", "value"}:
                    return section
        return None

    def _container_structure_preview(self, container: ContainerItem, limit: int = 3) -> list[str]:
        items = [str(value).strip() for value in getattr(container, "structure", []) if str(value).strip()]
        if not items:
            return ["-"]
        if len(items) <= limit:
            return items
        return items[:limit] + [f"... ({len(items)} total)"]

    def _container_structure_signature(self, container: ContainerItem) -> str:
        items = [str(value).strip() for value in getattr(container, "structure", []) if str(value).strip()]
        if not items:
            return "-"
        signature = "".join(item[0] for item in items if item)
        return signature or "-"

    def _container_value_preview(self, container: ContainerItem, limit: int = 3) -> list[str]:
        if container.kind == "variable":
            value = container.value.strip()
            return [value or "-"]
        values = [str(value).strip() for value in container.values if str(value).strip()]
        if not values:
            return ["-"]
        start = min(max(getattr(container, "view_offset", 0), 0), max(0, len(values) - 1))
        preview = values[start : start + limit]
        if len(values) <= limit:
            return preview
        return [f"[{start + 1}:{start + len(preview)}] {', '.join(preview)}", f"... ({len(values)} total)"]

    def _container_text_to_lines(self, text_widget: tk.Text) -> list[str]:
        return [line.strip() for line in text_widget.get("1.0", tk.END).splitlines() if line.strip()]

    def _open_container_editor(self, container: ContainerItem, focus_section: str | None = None) -> None:
        dialog = tk.Toplevel(self.root)
        dialog.title(f"Container {container.name}")
        dialog.transient(self.root)
        dialog.grab_set()
        dialog.geometry("900x660")
        dialog.minsize(780, 560)

        body = ttk.Frame(dialog, padding=12)
        body.grid(row=0, column=0, sticky="nsew")
        dialog.columnconfigure(0, weight=1)
        dialog.rowconfigure(0, weight=1)
        body.columnconfigure(0, weight=1)
        body.columnconfigure(1, weight=1)
        body.rowconfigure(2, weight=1)

        header = ttk.Frame(body)
        header.grid(row=0, column=0, columnspan=2, sticky="ew")
        header.columnconfigure(1, weight=1)
        header.columnconfigure(3, weight=1)
        ttk.Label(header, text="Name").grid(row=0, column=0, sticky="w")
        ttk.Label(header, text=container.name, foreground=COLORS["accent"]).grid(row=0, column=1, sticky="w", padx=(6, 18))
        ttk.Label(header, text="Kind").grid(row=0, column=2, sticky="w")
        ttk.Label(header, text=container.kind, foreground=COLORS["accent"]).grid(row=0, column=3, sticky="w", padx=(6, 0))

        meta_row = ttk.Frame(body)
        meta_row.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(10, 0))
        meta_row.columnconfigure((1, 3, 5), weight=1)
        ttk.Label(meta_row, text="Count").grid(row=0, column=0, sticky="w")
        ttk.Label(meta_row, text=str(container.count), foreground=COLORS["muted"]).grid(row=0, column=1, sticky="w", padx=(6, 18))
        ttk.Label(meta_row, text="Stride").grid(row=0, column=2, sticky="w")
        ttk.Label(meta_row, text=str(container.stride), foreground=COLORS["muted"]).grid(row=0, column=3, sticky="w", padx=(6, 18))
        ttk.Label(meta_row, text="Structure items").grid(row=0, column=4, sticky="w")
        ttk.Label(meta_row, text=str(len(getattr(container, "structure", []))), foreground=COLORS["muted"]).grid(row=0, column=5, sticky="w", padx=(6, 0))

        structure_frame = ttk.LabelFrame(body, text="Structure", padding=8)
        structure_frame.grid(row=2, column=0, sticky="nsew", pady=(12, 0), padx=(0, 6))
        structure_frame.columnconfigure(0, weight=1)
        structure_frame.rowconfigure(0, weight=1)
        structure_text = tk.Text(
            structure_frame,
            wrap="word",
            height=12,
            bg=COLORS["panel_alt"],
            fg=COLORS["text"],
            insertbackground=COLORS["text"],
            relief="flat",
            borderwidth=0,
            highlightthickness=1,
            highlightbackground=COLORS["grid"],
            highlightcolor=COLORS["accent"],
        )
        structure_text.grid(row=0, column=0, sticky="nsew")
        structure_text.insert("1.0", "\n".join(self._container_structure_preview(container, limit=9999)))
        structure_scroll = ttk.Scrollbar(structure_frame, orient="vertical", command=structure_text.yview)
        structure_scroll.grid(row=0, column=1, sticky="ns")
        structure_text.configure(yscrollcommand=structure_scroll.set)

        value_label = "Value" if container.kind == "variable" else "Values"
        value_frame = ttk.LabelFrame(body, text=value_label, padding=8)
        value_frame.grid(row=2, column=1, sticky="nsew", pady=(12, 0), padx=(6, 0))
        value_frame.columnconfigure(0, weight=1)
        value_frame.rowconfigure(0, weight=1)
        value_text = tk.Text(
            value_frame,
            wrap="word",
            height=12 if container.kind == "array" else 6,
            bg=COLORS["canvas"],
            fg=COLORS["text"],
            insertbackground=COLORS["text"],
            relief="flat",
            borderwidth=0,
            highlightthickness=1,
            highlightbackground=COLORS["grid"],
            highlightcolor=COLORS["accent"],
        )
        value_text.grid(row=0, column=0, sticky="nsew")
        if container.kind == "variable":
            value_text.insert("1.0", container.value)
        else:
            value_text.insert("1.0", "\n".join(container.values))
        value_scroll = ttk.Scrollbar(value_frame, orient="vertical", command=value_text.yview)
        value_scroll.grid(row=0, column=1, sticky="ns")
        value_text.configure(yscrollcommand=value_scroll.set)

        button_row = ttk.Frame(body)
        button_row.grid(row=3, column=0, columnspan=2, sticky="ew", pady=(12, 0))
        button_row.columnconfigure(0, weight=1)
        button_row.columnconfigure(1, weight=1)

        def _apply_and_close() -> None:
            container.structure = self._container_text_to_lines(structure_text)
            if container.kind == "variable":
                container.value = value_text.get("1.0", tk.END).strip()
            else:
                container.values = self._container_text_to_lines(value_text)
            self._refresh_all()
            self._log(f"Updated container {container.name}.")
            dialog.destroy()

        apply_button = ttk.Button(button_row, text="Apply", command=_apply_and_close)
        apply_button.grid(row=0, column=0, sticky="ew", padx=(0, 6))
        cancel_button = ttk.Button(button_row, text="Cancel", command=dialog.destroy)
        cancel_button.grid(row=0, column=1, sticky="ew", padx=(6, 0))

        if focus_section == "structure":
            structure_text.focus_set()
        else:
            value_text.focus_set()

        dialog.bind("<Escape>", lambda _event: dialog.destroy())
        dialog.bind("<Control-Return>", lambda _event: _apply_and_close())

    def _ensure_function_text_item(self, function_name: str) -> FunctionTextItem:
        existing = self._find_function_text_for_function(function_name)
        if existing is not None:
            return existing
        function_item = self._find_function_frame(function_name)
        if function_item is None:
            raise AssertionError(f"Missing function {function_name} while creating text node.")
        item = FunctionTextItem(
            name=self.project.next_function_text_name(function_name),
            function_name=function_name,
            text="",
            x=function_item.x + 420.0,
            y=function_item.y,
            width=360.0,
            height=220.0,
        )
        self.project.function_text_items.append(item)
        return item

    def _rename_function_frame(self, item: FunctionFrameItem, new_name: str) -> None:
        target_name = new_name.strip() or item.name
        if target_name == item.name:
            return
        if self._find_function_frame(target_name):
            raise RuntimeError(f"Function name already exists: {target_name}")
        old_name = item.name
        item.name = target_name
        for connection in self.project.connections:
            if connection.source_kind == "function" and connection.source_name == old_name:
                connection.source_name = target_name
            if connection.target_kind == "function" and connection.target_name == old_name:
                connection.target_name = target_name
        for stage in self.project.intervention_stages:
            stage.functions = [target_name if value == old_name else value for value in stage.functions]
        for text_item in self.project.function_text_items:
            if text_item.function_name == old_name:
                text_item.function_name = target_name
                if text_item.name == f"{old_name}_text":
                    previous_name = text_item.name
                    text_item.name = self.project.next_function_text_name(target_name)
                    if self.selected_function_text_name == previous_name:
                        self.selected_function_text_name = text_item.name
        if self.selected_function_name == old_name:
            self.selected_function_name = target_name

    def _open_function_text_editor(self, item: FunctionTextItem) -> None:
        dialog = tk.Toplevel(self.root)
        dialog.title(f"Text {item.name}")
        dialog.transient(self.root)
        dialog.grab_set()
        dialog.geometry("720x520")
        dialog.minsize(640, 420)

        body = ttk.Frame(dialog, padding=12)
        body.grid(row=0, column=0, sticky="nsew")
        dialog.columnconfigure(0, weight=1)
        dialog.rowconfigure(0, weight=1)
        body.columnconfigure(0, weight=1)
        body.rowconfigure(1, weight=1)

        header = ttk.Frame(body)
        header.grid(row=0, column=0, sticky="ew")
        header.columnconfigure(1, weight=1)
        ttk.Label(header, text="Name").grid(row=0, column=0, sticky="w")
        name_entry = ttk.Entry(header)
        name_entry.grid(row=0, column=1, sticky="ew", padx=(6, 18))
        name_entry.insert(0, item.name)
        ttk.Label(header, text="Function").grid(row=0, column=2, sticky="w")
        ttk.Label(header, text=item.function_name or "-", foreground=COLORS["accent"]).grid(row=0, column=3, sticky="w", padx=(6, 0))

        text_frame = ttk.LabelFrame(body, text="Text", padding=8)
        text_frame.grid(row=1, column=0, sticky="nsew", pady=(12, 0))
        text_frame.columnconfigure(0, weight=1)
        text_frame.rowconfigure(0, weight=1)
        text_widget = tk.Text(
            text_frame,
            wrap="word",
            bg=COLORS["panel_alt"],
            fg=COLORS["text"],
            insertbackground=COLORS["text"],
            relief="flat",
            borderwidth=0,
            highlightthickness=1,
            highlightbackground=COLORS["grid"],
            highlightcolor=COLORS["accent"],
        )
        text_widget.grid(row=0, column=0, sticky="nsew")
        text_widget.insert("1.0", item.text)
        text_scroll = ttk.Scrollbar(text_frame, orient="vertical", command=text_widget.yview)
        text_scroll.grid(row=0, column=1, sticky="ns")
        text_widget.configure(yscrollcommand=text_scroll.set)

        button_row = ttk.Frame(body)
        button_row.grid(row=2, column=0, sticky="ew", pady=(12, 0))
        button_row.columnconfigure(0, weight=1)
        button_row.columnconfigure(1, weight=1)

        def save() -> None:
            try:
                new_name = name_entry.get().strip() or item.name
                if new_name != item.name and self._find_function_text_item(new_name):
                    raise RuntimeError(f"Text node name already exists: {new_name}")
                item.name = new_name
                item.text = text_widget.get("1.0", tk.END).strip()
                self.selected_function_text_name = item.name
                self._refresh_all()
                dialog.destroy()
            except Exception as exc:  # noqa: BLE001
                messagebox.showerror("Text node save error", str(exc))

        ttk.Button(button_row, text="Save", command=save).grid(row=0, column=0, sticky="ew", padx=(0, 6))
        ttk.Button(button_row, text="Close", command=dialog.destroy).grid(row=0, column=1, sticky="ew", padx=(6, 0))

    def _open_function_editor(self, item: FunctionFrameItem) -> None:
        dialog = tk.Toplevel(self.root)
        dialog.title(f"fun {item.name}")
        dialog.transient(self.root)
        dialog.grab_set()
        dialog.geometry("860x680")
        body = ttk.Frame(dialog, padding=12)
        body.grid(row=0, column=0, sticky="nsew")
        dialog.columnconfigure(0, weight=1)
        dialog.rowconfigure(0, weight=1)
        body.columnconfigure(0, weight=1)
        body.columnconfigure(1, weight=1)
        body.rowconfigure(2, weight=1)

        title_row = ttk.Frame(body)
        title_row.grid(row=0, column=0, columnspan=2, sticky="ew")
        title_row.columnconfigure(1, weight=1)
        title_row.columnconfigure(3, weight=1)
        title_row.columnconfigure(5, weight=1)
        ttk.Label(title_row, text="Name").grid(row=0, column=0, sticky="w")
        name_entry = ttk.Entry(title_row, width=18)
        name_entry.grid(row=0, column=1, sticky="ew", padx=(6, 18))
        name_entry.insert(0, item.name)
        ttk.Label(title_row, text="Input").grid(row=0, column=2, sticky="w")
        input_name_entry = ttk.Entry(title_row, width=18)
        input_name_entry.grid(row=0, column=3, sticky="ew", padx=(6, 18))
        input_name_entry.insert(0, item.input_name or "in")
        ttk.Label(title_row, text="Output").grid(row=0, column=4, sticky="w")
        output_name_entry = ttk.Entry(title_row, width=18)
        output_name_entry.grid(row=0, column=5, sticky="ew", padx=(6, 0))
        output_name_entry.insert(0, item.output_name or "out")

        expected_input_frame = ttk.LabelFrame(body, text="Expected Input", padding=8)
        expected_input_frame.grid(row=1, column=0, sticky="nsew", pady=(12, 0), padx=(0, 6))
        expected_input_frame.columnconfigure(0, weight=1)
        expected_input_frame.rowconfigure(0, weight=1)
        expected_input_text = tk.Text(
            expected_input_frame,
            wrap="word",
            height=10,
            bg=COLORS["canvas"],
            fg=COLORS["text"],
            insertbackground=COLORS["text"],
            relief="flat",
            borderwidth=0,
            highlightthickness=1,
            highlightbackground=COLORS["grid"],
            highlightcolor=COLORS["accent"],
        )
        expected_input_text.grid(row=0, column=0, sticky="nsew")
        expected_input_text.insert("1.0", item.expected_input)
        expected_input_scroll = ttk.Scrollbar(expected_input_frame, orient="vertical", command=expected_input_text.yview)
        expected_input_scroll.grid(row=0, column=1, sticky="ns")
        expected_input_text.configure(yscrollcommand=expected_input_scroll.set)

        expected_output_frame = ttk.LabelFrame(body, text="Expected Output", padding=8)
        expected_output_frame.grid(row=1, column=1, sticky="nsew", pady=(12, 0), padx=(6, 0))
        expected_output_frame.columnconfigure(0, weight=1)
        expected_output_frame.rowconfigure(0, weight=1)
        expected_output_text = tk.Text(
            expected_output_frame,
            wrap="word",
            height=10,
            bg=COLORS["canvas"],
            fg=COLORS["text"],
            insertbackground=COLORS["text"],
            relief="flat",
            borderwidth=0,
            highlightthickness=1,
            highlightbackground=COLORS["grid"],
            highlightcolor=COLORS["accent"],
        )
        expected_output_text.grid(row=0, column=0, sticky="nsew")
        expected_output_text.insert("1.0", item.expected_output)
        expected_output_scroll = ttk.Scrollbar(expected_output_frame, orient="vertical", command=expected_output_text.yview)
        expected_output_scroll.grid(row=0, column=1, sticky="ns")
        expected_output_text.configure(yscrollcommand=expected_output_scroll.set)

        script_frame = ttk.LabelFrame(body, text="Real Function / Shader", padding=8)
        script_frame.grid(row=2, column=0, columnspan=2, sticky="nsew", pady=(12, 0))
        script_frame.columnconfigure(0, weight=1)
        script_frame.rowconfigure(0, weight=1)
        script_text = tk.Text(
            script_frame,
            wrap="word",
            height=14,
            bg=COLORS["panel_alt"],
            fg=COLORS["text"],
            insertbackground=COLORS["text"],
            relief="flat",
            borderwidth=0,
            highlightthickness=1,
            highlightbackground=COLORS["grid"],
            highlightcolor=COLORS["accent"],
        )
        script_text.grid(row=0, column=0, sticky="nsew")
        script_text.insert("1.0", item.script)
        script_scroll = ttk.Scrollbar(script_frame, orient="vertical", command=script_text.yview)
        script_scroll.grid(row=0, column=1, sticky="ns")
        script_text.configure(yscrollcommand=script_scroll.set)

        button_row = ttk.Frame(body)
        button_row.grid(row=3, column=0, columnspan=2, sticky="ew", pady=(12, 0))
        button_row.columnconfigure(0, weight=1)
        button_row.columnconfigure(1, weight=1)
        button_row.columnconfigure(2, weight=1)
        button_row.columnconfigure(3, weight=1)

        def apply_fields() -> None:
            new_name = name_entry.get().strip()
            if not new_name:
                raise RuntimeError("Function name is required.")
            self._rename_function_frame(item, new_name)
            dialog.title(f"fun {item.name}")
            item.input_name = input_name_entry.get().strip() or "in"
            item.output_name = output_name_entry.get().strip() or "out"
            item.expected_input = expected_input_text.get("1.0", tk.END).strip()
            item.expected_output = expected_output_text.get("1.0", tk.END).strip()
            item.script = script_text.get("1.0", tk.END).strip()

        def save() -> None:
            try:
                apply_fields()
            except Exception as exc:  # noqa: BLE001
                messagebox.showerror("Function save error", str(exc))
                return
            self._refresh_all()
            dialog.destroy()

        def request(mode: str) -> None:
            self._sync_agent_client_settings()
            try:
                apply_fields()
            except Exception as exc:  # noqa: BLE001
                messagebox.showerror("Function request error", str(exc))
                return
            current_script = script_text.get("1.0", tk.END).strip()
            current_text_item = self._find_function_text_for_function(item.name)
            current_solution_text = "" if current_text_item is None else current_text_item.text.strip()
            task_line = (
                "Return a concise natural-language solution only. Focus on algorithm steps, data flow, C++ responsibilities, and shader responsibilities. Do not output code."
                if mode == "plan"
                else "Return concrete C++ and shader code as plain text. If both are needed, separate them with short headings."
            )
            prompt = "\n".join(
                [
                    "You are helping complete a single fun node inside Algorithm Studio.",
                    "Return plain text only.",
                    "Do not emit algorithm-studio-tool blocks.",
                    "",
                    f"Function name: {item.name}",
                    f"Input port: {item.input_name}",
                    f"Output port: {item.output_name}",
                    "",
                    "Expected input:",
                    item.expected_input or "-",
                    "",
                    "Expected output:",
                    item.expected_output or "-",
                    "",
                    "Current function body:",
                    current_script or "-",
                    "",
                    "Current solution text:",
                    current_solution_text or "-",
                    "",
                    self._agent_document_context(),
                    "",
                    "Task:",
                    task_line,
                ]
            ).strip()
            selection = f"function:{item.name}"
            try:
                approved = self._authorize_chat_request(selection, prompt)
            except Exception as exc:  # noqa: BLE001
                message = self._compact_activity_text(str(exc), limit=120) or "审批检查失败。"
                self._finish_execution_trace("函数请求", False, f"审批失败：{message}")
                self._append_chat_message("error", message)
                messagebox.showerror("Approval error", message)
                return
            if not approved:
                self._finish_execution_trace("函数请求", False, "审批未通过。")
                return

            button_plan.configure(state="disabled")
            button_code.configure(state="disabled")
            save_button.configure(state="disabled")
            close_button.configure(state="disabled")
            self.status_var.set(f"Sending function request for {item.name}...")

            def emit_event(event: dict[str, str]) -> None:
                mapped = dict(event)
                mapped["title"] = "函数请求"
                self.root.after(0, lambda event=mapped: self._handle_agent_event(event))

            def finish_success(response: str) -> None:
                tool_calls, visible_response = self._extract_agent_tool_calls(response)
                if tool_calls:
                    raise RuntimeError("Function assistant must return plain text, not tool calls.")
                content = visible_response.strip()
                if not content:
                    raise RuntimeError("Function assistant returned empty content.")
                apply_fields()
                if mode == "plan":
                    text_item = self._ensure_function_text_item(item.name)
                    text_item.text = content
                    self.selected_function_name = None
                    self.selected_function_text_name = text_item.name
                else:
                    script_text.delete("1.0", tk.END)
                    script_text.insert("1.0", content)
                    item.script = content
                    self.selected_function_name = item.name
                    self.selected_function_text_name = None
                self._refresh_all()
                self._append_chat_message("assistant", content)
                detail = "已更新方案文本节点。" if mode == "plan" else "已更新真实函数 / shader。"
                self._finish_execution_trace("函数请求", True, detail)
                self.status_var.set(f"Function assistant finished for {item.name}.")
                button_plan.configure(state="normal")
                button_code.configure(state="normal")
                save_button.configure(state="normal")
                close_button.configure(state="normal")

            def finish_error(exc: Exception) -> None:
                message = self._compact_activity_text(str(exc), limit=120) or "Function assistant failed."
                self._append_chat_message("error", message)
                messagebox.showerror("Function assistant error", message)
                self._finish_execution_trace("函数请求", False, f"执行失败：{message}")
                self.status_var.set("Function assistant failed.")
                button_plan.configure(state="normal")
                button_code.configure(state="normal")
                save_button.configure(state="normal")
                close_button.configure(state="normal")

            def worker() -> None:
                try:
                    response = self.agent_client.generate(
                        self.project,
                        selection,
                        prompt,
                        event_callback=emit_event,
                    )
                except Exception as exc:  # noqa: BLE001
                    self.root.after(0, lambda exc=exc: finish_error(exc))
                    return
                def on_success() -> None:
                    try:
                        finish_success(response)
                    except Exception as exc:  # noqa: BLE001
                        finish_error(exc)
                self.root.after(0, on_success)

            import threading

            threading.Thread(target=worker, daemon=True).start()

        button_plan = ttk.Button(button_row, text="尝试给出解决方案", command=lambda: request("plan"))
        button_plan.grid(row=0, column=0, sticky="ew", padx=(0, 6))
        button_code = ttk.Button(button_row, text="尝试给出真实函数/shader", command=lambda: request("code"))
        button_code.grid(row=0, column=1, sticky="ew", padx=6)
        save_button = ttk.Button(button_row, text="Save", command=save)
        save_button.grid(row=0, column=2, sticky="ew", padx=6)
        close_button = ttk.Button(button_row, text="Close", command=dialog.destroy)
        close_button.grid(row=0, column=3, sticky="ew", padx=(6, 0))

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
        node.width = max(320.0, base_width + self._scene_delta(x - start_x))
        node.height = max(180.0, base_height + self._scene_delta(y - start_y))
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

    def _canvas_zoom_factor(self) -> float:
        return self.canvas_zoom if self.canvas_zoom > 0.0 else 1.0

    def _scene_delta(self, value: float) -> float:
        return value / self._canvas_zoom_factor()

    def _scene_point(self, x: float, y: float) -> tuple[float, float]:
        zoom = self._canvas_zoom_factor()
        return x / zoom, y / zoom

    def _port_canvas_position_screen(self, kind: str, name: str, direction: str, port: str) -> tuple[float, float]:
        x, y = self._port_canvas_position(kind, name, direction, port)
        zoom = self._canvas_zoom_factor()
        return x * zoom, y * zoom

    def _scale_canvas_rendering(self, canvas: tk.Canvas, zoom: float) -> None:
        if zoom == 1.0:
            return
        for item_id in canvas.find_all():
            item_type = canvas.type(item_id)
            if item_type == "text":
                font_spec = canvas.itemcget(item_id, "font")
                if font_spec:
                    font = tkfont.Font(font=font_spec)
                    size = int(font.cget("size"))
                    if size < 0:
                        size = -size
                    font.configure(size=max(1, int(round(size * zoom))))
                    canvas.itemconfigure(item_id, font=font)
                wrap_width = canvas.itemcget(item_id, "width")
                if wrap_width:
                    canvas.itemconfigure(item_id, width=max(1, int(round(float(wrap_width) * zoom))))
                continue
            width_value = canvas.itemcget(item_id, "width")
            if width_value:
                canvas.itemconfigure(item_id, width=max(1, int(round(float(width_value) * zoom))))

    def _on_canvas_mouse_wheel(self, event: tk.Event) -> str | None:
        if not self.canvas:
            return None
        steps = 0
        if getattr(event, "num", None) == 4:
            steps = 1
        elif getattr(event, "num", None) == 5:
            steps = -1
        elif getattr(event, "delta", 0):
            steps = 1 if event.delta > 0 else -1
        if steps == 0:
            return "break"
        item_id = self._canvas_item_hit(event.x, event.y)
        if item_id is not None:
            tags = self.canvas.gettags(item_id)
            kind, node_name = self._node_info_from_tags(tags)
            if kind == "container" and node_name:
                container = self._find_container(node_name)
                if container and container.kind == "array":
                    container.view_offset = max(0, container.view_offset - steps)
                    self._refresh_all()
                    return "break"
        zoom = self.canvas_zoom
        if steps > 0:
            zoom *= 1.12
        else:
            zoom /= 1.12
        zoom = min(max(zoom, 0.5), 2.5)
        if zoom == self.canvas_zoom:
            return "break"
        self.canvas_zoom = zoom
        self._refresh_all()
        return "break"

    def _on_palette_mouse_wheel(self, event: tk.Event) -> str | None:
        if not self.palette_canvas:
            return None
        steps = 0
        if getattr(event, "num", None) == 4:
            steps = 1
        elif getattr(event, "num", None) == 5:
            steps = -1
        elif getattr(event, "delta", 0):
            steps = 1 if event.delta > 0 else -1
        if steps == 0:
            return "break"
        self.palette_canvas.yview_scroll(-steps * 3, "units")
        return "break"

    def _move_scene(self, dx: float, dy: float) -> None:
        dx = self._scene_delta(dx)
        dy = self._scene_delta(dy)
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
        start_x, start_y = self._port_canvas_position_screen(kind, name, direction, port)
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
        start_x, start_y = self._port_canvas_position_screen(start_kind, start_name, start_direction, start_port)
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
        start_x, start_y = self._port_canvas_position_screen(start_kind, start_name, start_direction, start_port)
        end_x, end_y = self._port_canvas_position_screen(end_kind, end_name, end_direction, end_port)
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
            outputs = self._resource_output_ports(item.resource_kind or "mesh", item.outputs)
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
        for item in self.project.function_text_items:
            item_id = self._draw_function_text_node(canvas, item)
            self.canvas_nodes[item.name] = item_id
            self.canvas_item_to_name[item_id] = item.name
        for stage in self.project.intervention_stages:
            item_id = self._draw_stage_node(canvas, stage)
            self.canvas_nodes[stage.name] = item_id
            self.canvas_item_to_name[item_id] = stage.name
        self._draw_connections(canvas)
        if self.connection_drag_state:
            self._draw_connection_drag_preview(canvas)
        if self.canvas_zoom != 1.0:
            canvas.scale("all", 0.0, 0.0, self.canvas_zoom, self.canvas_zoom)
            self._scale_canvas_rendering(canvas, self.canvas_zoom)
        canvas.configure(scrollregion=canvas.bbox("all"))

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
        end_x, end_y = self._scene_point(float(self.connection_drag_state["x"]), float(self.connection_drag_state["y"]))
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
            text="container" if group.name == "container" else f"container {group.name}",
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

        canvas.create_text(24, 18, anchor="w", fill=COLORS["muted"], text="drag node headers, left-drag blank space to box-select, right-drag blank space to pan", font=("Segoe UI", 10))
        canvas.create_text(width - 24, 18, anchor="e", fill=COLORS["muted"], text="right click a node to delete or duplicate", font=("Segoe UI", 10))

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
        header_fill = "#06080d"
        title = container.name
        parent_group_name = self._container_parent_group_name(container.name)
        compact = parent_group_name is not None
        width = 132.0 if compact else float(NODE_WIDTH)
        value_lines = self._container_value_preview(container, 2 if compact else 3)
        content_rows = max(len(value_lines), 1)
        height = max(92.0 if compact else 104.0, 62.0 + content_rows * 18.0)
        node_tag = f"node:container:{container.name}"
        outline = COLORS["accent"] if self.selected_container_name == container.name else fill
        item_id = canvas.create_rectangle(
            x,
            y,
            x + width,
            y + height,
            fill=COLORS["panel_alt"],
            outline=outline,
            width=2,
            tags=(node_tag, "draggable"),
        )
        header_height = 24
        canvas.create_rectangle(
            x,
            y,
            x + width,
            y + header_height,
            fill=header_fill,
            outline=outline,
            width=2,
            tags=(node_tag, "node_header", "draggable"),
        )
        canvas.create_text(
            x + width / 2,
            y + 5,
            anchor="n",
            fill="#ffffff",
            text=title,
            font=("Segoe UI", 11, "bold"),
            tags=(f"text_of_{item_id}", node_tag, "node_header", "draggable"),
        )

        body_top = y + header_height
        port_y = y + height - 24
        content_bottom = port_y - 10
        canvas.create_rectangle(
            x + 1,
            body_top + 1,
            x + width - 1,
            content_bottom,
            fill="#111827",
            outline=COLORS["grid"],
            width=1,
            tags=(node_tag, "node_body", "container_section:value"),
        )

        value_section_label = "VALUE" if container.kind == "variable" else "VALUES"
        canvas.create_text(
            x + 10,
            body_top + 4,
            anchor="nw",
            fill=COLORS["muted"],
            text=value_section_label,
            font=("Segoe UI", 9, "bold"),
            tags=(f"text_of_{item_id}", node_tag, "container_section:value"),
        )
        canvas.create_text(
            x + 10,
            body_top + 20,
            anchor="nw",
            fill=COLORS["good"],
            text="\n".join(value_lines),
            font=("Segoe UI", 9),
            width=width - 20,
            justify="left",
            tags=(f"text_of_{item_id}", node_tag, "container_section:value"),
        )

        input_tag = f"port:container:{container.name}:in:in"
        input_x = x + width * 0.22
        input_y = port_y
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
            input_x,
            input_y + 10,
            anchor="n",
            fill=COLORS["muted"],
            text="in",
            font=("Segoe UI", 9, "bold"),
            tags=(f"node:container:{container.name}", input_tag),
        )
        self._register_port("container", container.name, "in", "in", input_x, input_y)
        port_tag = f"port:container:{container.name}:out:out"
        port_x = x + width * 0.78
        port_y = input_y
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
            port_x,
            port_y + 10,
            anchor="n",
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
        structured_mesh = self.project.decomposer_res.get("mesh") if isinstance(self.project.decomposer_res, dict) else None
        if isinstance(structured_mesh, dict):
            resource_box_left = middle_left + 10
            resource_box_right = middle_right - 10
            resource_box_top = split_y + 24
            resource_box_bottom = middle_bottom - 14
            canvas.create_rectangle(
                resource_box_left,
                resource_box_top,
                resource_box_right,
                resource_box_bottom,
                fill="#141a22",
                outline=COLORS["grid"],
                width=1,
                tags=(node_tag, "node_body"),
            )
            canvas.create_text(
                resource_box_left + 10,
                resource_box_top + 8,
                anchor="nw",
                fill=COLORS["muted"],
                text="mesh",
                font=("Segoe UI", 10, "bold"),
                tags=(f"text_of_{item_id}", node_tag, "node_body"),
            )
            block_left = resource_box_left + 16
            block_right = resource_box_right - 16
            block_width = max(block_right - block_left, 1)
            block_height = 18
            block_gap = 8
            for index, label in enumerate(["vertex", "edge", "normal"]):
                block_top = resource_box_top + 30 + index * (block_height + block_gap)
                canvas.create_rectangle(
                    block_left,
                    block_top,
                    block_left + block_width,
                    block_top + block_height,
                    fill=COLORS["agent"],
                    outline=COLORS["agent"],
                    width=1,
                    tags=(node_tag, "node_body"),
                )
                canvas.create_text(
                    block_left + 10,
                    block_top + 2,
                    anchor="nw",
                    fill=COLORS["window"],
                    text=label,
                    font=("Segoe UI", 10, "bold"),
                    tags=(f"text_of_{item_id}", node_tag, "node_body"),
                )
        else:
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
        width = max(float(getattr(item, "width", BLUEPRINT_NODE_WIDTH)), 220.0)
        height = max(float(getattr(item, "height", BLUEPRINT_NODE_MIN_HEIGHT)), 104.0)
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
            text="meshNode" if item.name == "meshNode" else f"meshNode {item.name}",
            font=("Segoe UI", 12, "bold"),
            tags=(f"text_of_{item_id}", node_tag, "node_header", "draggable"),
        )

        body_top = y + header_height
        body_bottom = y + height
        canvas.create_rectangle(
            x + 1,
            body_top + 1,
            x + width - 1,
            body_bottom - 1,
            fill="#141a22",
            outline=COLORS["grid"],
            width=1,
            tags=(node_tag, "node_body"),
        )

        mesh_x0 = x + 18
        mesh_x1 = x + width - 18
        mesh_y0 = body_top + 30
        mesh_y1 = body_top + 72
        canvas.create_rectangle(
            mesh_x0,
            mesh_y0,
            mesh_x1,
            mesh_y1,
            fill=COLORS["agent"],
            outline=COLORS["agent"],
            width=1,
            tags=(node_tag, "node_body"),
        )
        canvas.create_text(
            (mesh_x0 + mesh_x1) / 2,
            (mesh_y0 + mesh_y1) / 2 - 1,
            anchor="center",
            fill=COLORS["window"],
            text="[mesh]",
            font=("Segoe UI", 12, "bold"),
            tags=(f"text_of_{item_id}", node_tag, "node_body"),
        )

        input_y = body_top + 86
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
            text="",
            font=("Segoe UI", 10),
            width=80,
            justify="left",
            tags=(f"text_of_{item_id}", node_tag, input_tag),
        )
        self._register_port("resnode", item.name, "in", "in", x + 16, input_y + 6)

        output_tag = f"port:resnode:{item.name}:out:mesh"
        out_x = x + width - 16
        canvas.create_oval(
            out_x - 12,
            input_y,
            out_x,
            input_y + 12,
            fill=COLORS["agent"],
            outline=COLORS["agent"],
            tags=(node_tag, output_tag),
        )
        canvas.create_text(
            out_x - 16,
            input_y - 1,
            anchor="e",
            fill=COLORS["text"],
            text="",
            font=("Segoe UI", 10),
            width=80,
            justify="right",
            tags=(f"text_of_{item_id}", node_tag, output_tag),
        )
        self._register_port("resnode", item.name, "out", "mesh", out_x - 6, input_y + 6)

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
        script_lines = [line for line in (item.script or "agent writes text and code here").splitlines() if line.strip()]
        if not script_lines:
            script_lines = ["agent writes text and code here"]
        return self._draw_blueprint_node(
            canvas,
            "function",
            item.name,
            x,
            y,
            "fun" if item.name == "fun" else f"fun {item.name}",
            inputs,
            outputs,
            script_lines,
            float(getattr(item, "width", BLUEPRINT_NODE_WIDTH)),
            float(getattr(item, "height", BLUEPRINT_NODE_MIN_HEIGHT)),
            COLORS["good"],
            self.selected_function_name == item.name,
        )

    def _draw_function_text_node(self, canvas: tk.Canvas, item: FunctionTextItem) -> int:
        x = item.x or CANVAS_PADDING + 440
        y = item.y or CANVAS_PADDING + 520
        width = max(float(getattr(item, "width", BLUEPRINT_NODE_WIDTH)), 320.0)
        height = max(float(getattr(item, "height", BLUEPRINT_NODE_MIN_HEIGHT)), 180.0)
        outline = COLORS["accent"] if self.selected_function_text_name == item.name else COLORS["accent_2"]
        node_tag = f"node:functiontext:{item.name}"
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
            fill=COLORS["accent_2"],
            outline=outline,
            width=2,
            tags=(node_tag, "node_header", "draggable"),
        )
        canvas.create_text(
            x + 12,
            y + 7,
            anchor="nw",
            fill=COLORS["window"],
            text=item.name,
            font=("Segoe UI", 12, "bold"),
            tags=(f"text_of_{item_id}", node_tag, "node_header", "draggable"),
        )

        function_label = f"fun: {item.function_name or '-'}"
        canvas.create_text(
            x + 12,
            y + header_height + 10,
            anchor="nw",
            fill=COLORS["muted"],
            text=function_label,
            font=("Segoe UI", 10),
            tags=(f"text_of_{item_id}", node_tag, "node_body"),
        )

        preview_text = item.text.strip() or "双击编辑方案文本"
        canvas.create_text(
            x + 12,
            y + header_height + 34,
            anchor="nw",
            fill=COLORS["text"],
            text=preview_text,
            font=("Segoe UI", 10),
            width=max(width - 24, 120.0),
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
            tags=(node_tag, "node_resize_handle", f"resize_handle:functiontext:{item.name}"),
        )
        canvas.tag_raise(handle_id)
        return item_id

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
