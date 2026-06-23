#!/usr/bin/env python3

from __future__ import annotations

import copy
import ctypes
import json
import math
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
LEGACY_ALGORITHM_NAME_PREFIX = "vxax"
RESOURCE_ROOT_GROUP_NAME = "resourceRoot"
FUNCTION_SCRIPT_PLACEHOLDER = "Describe the function logic here."
FUNCTION_SCRIPT_LANGUAGE_DEFAULT = "pseudocode"

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
    from .agents import MockAgentClient, generate_cpp_skeleton
    from .interface4agents import execute_interface4agents_command, extract_interface4agents_script
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
    from agents import MockAgentClient, generate_cpp_skeleton
    from interface4agents import execute_interface4agents_command, extract_interface4agents_script

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
    from .app_container_transaction_mixin import AlgorithmStudioContainerTransactionMixin
    from .app_chat_mixin import AlgorithmStudioChatMixin
    from .app_chat_request_mixin import AlgorithmStudioChatRequestMixin
    from .app_canvas_overlay_mixin import AlgorithmStudioCanvasOverlayMixin
    from .app_canvas_interaction_mixin import AlgorithmStudioCanvasInteractionMixin
    from .app_editor_dialog_mixin import AlgorithmStudioEditorDialogMixin
    from .app_identity_mixin import AlgorithmStudioIdentityMixin
    from .app_palette_mixin import AlgorithmStudioPaletteMixin
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
    from app_container_transaction_mixin import AlgorithmStudioContainerTransactionMixin
    from app_chat_mixin import AlgorithmStudioChatMixin
    from app_chat_request_mixin import AlgorithmStudioChatRequestMixin
    from app_canvas_overlay_mixin import AlgorithmStudioCanvasOverlayMixin
    from app_canvas_interaction_mixin import AlgorithmStudioCanvasInteractionMixin
    from app_editor_dialog_mixin import AlgorithmStudioEditorDialogMixin
    from app_identity_mixin import AlgorithmStudioIdentityMixin
    from app_palette_mixin import AlgorithmStudioPaletteMixin


class AlgorithmStudioApp(
    AlgorithmStudioContainerTransactionMixin,
    AlgorithmStudioChatMixin,
    AlgorithmStudioChatRequestMixin,
    AlgorithmStudioCanvasOverlayMixin,
    AlgorithmStudioCanvasInteractionMixin,
    AlgorithmStudioEditorDialogMixin,
    AlgorithmStudioIdentityMixin,
    AlgorithmStudioPaletteMixin,
):
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("Algorithm Studio")
        self.root.geometry("1640x980")
        self.root.configure(bg=COLORS["window"])

        self.project = ProjectState()
        self.algorithm_name_prefix = ""
        self._normalize_project_algorithm_identity(self.project)
        self.project_manifest_text_cache = self.project.rebuild_manifest_text()
        self.project_manifest_revision = 1
        self.document_editor_dirty = False
        self.document_editor_applying = False
        self.document_apply_after_id: str | None = None
        self.document_last_error: str | None = None
        self.geometry_manifest_after_id: str | None = None
        self.agent_client = MockAgentClient()
        self.selected_container_name: str | None = None
        self.selected_rule_name: str | None = None
        self.selected_reflector_name: str | None = None
        self.selected_res_node_name: str | None = None
        self.selected_function_name: str | None = None
        self.selected_function_text_name: str | None = None
        self.selected_stage_name: str | None = None
        self.selected_container_group_name: str | None = None
        self.selected: list[dict[str, str]] = []
        self.selection_state: dict[str, Any] | None = None
        self.selection_clipboard: dict[str, Any] | None = None
        self.connection_drag_state: dict[str, Any] | None = None
        self.marquee_state: dict[str, Any] | None = None
        self.canvas_pan_state: dict[str, Any] | None = None
        self.container_group_drag_state: dict[str, Any] | None = None
        self.container_group_resize_state: dict[str, Any] | None = None
        self.toolnode_resize_state: dict[str, Any] | None = None
        self.container_copy_drag_state: dict[str, Any] | None = None
        self.canvas_nodes: dict[str, int] = {}
        self.canvas_container_group_nodes: dict[str, int] = {}
        self.canvas_port_positions: dict[str, tuple[float, float]] = {}
        self.canvas_connection_item_to_index: dict[int, int] = {}
        self.canvas_zoom: float = 1.0
        self.canvas_double_click_suppress_until: float = 0.0
        self.palette_shell: ttk.Frame | None = None
        self.palette_canvas: tk.Canvas | None = None
        self.palette_inner_frame: ttk.Frame | None = None
        self.interface4agents_highlight_targets: dict[str, list[tuple[tk.Widget, dict[str, Any]]]] = {}
        self.interface4agents_highlight_after_id: str | None = None
        self.interface4agents_highlight_restore: list[tuple[tk.Widget, dict[str, Any]]] = []
        self.drag_palette_body_frame: ttk.Frame | None = None
        self.drag_palette_toggle_button: ttk.Button | None = None
        self.node_drag_state: dict[str, Any] | None = None
        self.palette_drag_state: dict[str, Any] | None = None
        self.canvas_item_to_name: dict[int, str] = {}
        self.log_lines: list[str] = []
        self.algorithm_identity_syncing = False
        self.canvas_view_mode = "graph"
        self.scene_tab_buttons: dict[str, tk.Widget] = {}
        self.scene_tabs_canvas: tk.Canvas | None = None

        self.project_name_var = tk.StringVar(value=self.project.algorithm_name)
        self.package_name_var = tk.StringVar(value=self.project.package_name)
        self.algorithm_prefix_var = tk.StringVar(value=self._algorithm_count_prefix())
        self.algorithm_suffix_var = tk.StringVar(value=self._algorithm_suffix_from_full_name(self.project.algorithm_name))
        self.scene_tab_var = tk.StringVar(value="main")
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
        self.drag_palette_panel_collapsed_var = tk.BooleanVar(value=False)
        self.sidebar_collapsed_var = tk.BooleanVar(value=False)
        self.agent_ready_var = tk.BooleanVar(value=False)
        self.chat_history: list[dict[str, Any]] = []
        self.chat_message_serial = 0
        self.chat_embedded_widgets: list[tk.Widget] = []
        self.chat_message_controls: dict[int, dict[str, Any]] = {}
        self.pending_chat_request_message_id: int | None = None
        self.chat_controls_hide_after_ids: dict[int, str] = {}
        self.chat_history_compaction_summary = ""
        self.chat_history_compacted_item_count = 0
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
        self.selection_name_entry: ttk.Entry | None = None
        self.selection_copy_button: ttk.Button | None = None
        self.selection_merge_button: ttk.Button | None = None
        self.selection_arrange_button: ttk.Button | None = None
        self.selection_delete_button: ttk.Button | None = None
        self.selection_paste_button: ttk.Button | None = None
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
        self.detail_panel_state: dict[str, Any] | None = None
        self.access_rules_path = resolve_access_rules_path()
        self._apply_saved_settings()
        self._configure_style()
        self._build_ui()
        self._install_settings_persistence()
        self._apply_drag_palette_panel_layout()
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

    def _build_canvas_panel(self, parent: ttk.Frame) -> None:
        frame = ttk.Frame(parent)
        frame.grid(row=0, column=1, sticky="nsew")
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(0, weight=3)
        frame.rowconfigure(1, weight=2)

        canvas_frame = ttk.LabelFrame(frame, text="Scene", padding=8)
        canvas_frame.grid(row=0, column=0, sticky="nsew")
        canvas_frame.rowconfigure(1, weight=1)
        canvas_frame.columnconfigure(0, weight=1)

        scene_tabs_shell = ttk.Frame(canvas_frame)
        scene_tabs_shell.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        scene_tabs_shell.columnconfigure(0, weight=1)
        scene_tabs_canvas = tk.Canvas(
            scene_tabs_shell,
            bg=COLORS["panel"],
            highlightthickness=0,
            height=44,
        )
        scene_tabs_canvas.grid(row=0, column=0, sticky="ew")
        self.scene_tabs_canvas = scene_tabs_canvas

        scene_tabs = tk.Frame(scene_tabs_canvas, bg=COLORS["panel"])
        scene_tabs_window = scene_tabs_canvas.create_window((0, 0), window=scene_tabs, anchor="nw")

        def _sync_scene_tabs_scrollregion(_event: tk.Event | None = None) -> None:
            if not self.scene_tabs_canvas:
                return
            self.scene_tabs_canvas.configure(scrollregion=self.scene_tabs_canvas.bbox("all"))

        def _sync_scene_tabs_window_width(event: tk.Event) -> None:
            if not self.scene_tabs_canvas:
                return
            requested_width = max(scene_tabs.winfo_reqwidth(), event.width)
            self.scene_tabs_canvas.itemconfigure(scene_tabs_window, width=requested_width)

        scene_tabs.bind("<Configure>", _sync_scene_tabs_scrollregion)
        scene_tabs_canvas.bind("<Configure>", _sync_scene_tabs_window_width)
        scene_tabs_canvas.bind("<MouseWheel>", self._on_scene_tabs_mouse_wheel)
        scene_tabs_canvas.bind("<Button-4>", self._on_scene_tabs_mouse_wheel)
        scene_tabs_canvas.bind("<Button-5>", self._on_scene_tabs_mouse_wheel)
        self.scene_tab_buttons = {}
        scene_specs = (
            ("algorithmScene", "main", "graph"),
            ("containerScene", "container", "container_overview"),
            ("decomposerScene", "decomposer", "decomposer_overview"),
            ("reflectorScene", "reflector", "reflector_overview"),
            ("interventionerScene", "interventioner", "interventioner_overview"),
            ("d2cScene", "decomposer2container", "decomposer2container_overview"),
            ("allInOne", "all_in_one", "all_in_one"),
        )
        for index, (label, tab_key, view_mode) in enumerate(scene_specs):
            button = tk.Label(
                scene_tabs,
                text=label,
                bg=COLORS["panel_alt"],
                fg=COLORS["muted"],
                padx=12,
                pady=6,
                relief="flat",
                bd=1,
                cursor="hand2",
            )
            padx = (0, 6) if index == 0 else (0, 6)
            button.pack(side="left", padx=padx)
            button.bind("<Button-1>", lambda _event, mode=view_mode: self._set_canvas_view_mode(mode))
            button.bind("<MouseWheel>", self._on_scene_tabs_mouse_wheel)
            button.bind("<Button-4>", self._on_scene_tabs_mouse_wheel)
            button.bind("<Button-5>", self._on_scene_tabs_mouse_wheel)
            self.scene_tab_buttons[tab_key] = button
            self.interface4agents_highlight_targets[f"scene:{tab_key}"] = [
                (
                    button,
                    {
                        "bg": COLORS["accent"],
                        "fg": COLORS["window"],
                        "highlightbackground": COLORS["accent"],
                        "highlightcolor": COLORS["accent"],
                        "highlightthickness": 2,
                    },
                )
            ]
        self._refresh_scene_tabs()

        self.canvas = tk.Canvas(
            canvas_frame,
            bg=COLORS["canvas"],
            highlightthickness=0,
            width=1080,
            height=820,
            relief="flat",
        )
        self.canvas.grid(row=1, column=0, sticky="nsew")
        self.interface4agents_highlight_targets["canvas"] = [
            (
                self.canvas,
                {
                    "highlightbackground": COLORS["accent"],
                    "highlightcolor": COLORS["accent"],
                    "highlightthickness": 2,
                },
            )
        ]
        self.canvas.bind("<Button-1>", self._on_canvas_click)
        self.canvas.bind("<ButtonPress-3>", self._on_canvas_right_press)
        self.canvas.bind("<B3-Motion>", self._on_canvas_right_drag)
        self.canvas.bind("<ButtonRelease-3>", self._on_canvas_right_release)
        self.canvas.bind("<B1-Motion>", self._on_canvas_drag)
        self.canvas.bind("<ButtonRelease-1>", self._on_canvas_release)
        self.root.bind_all("<B1-Motion>", self._on_canvas_drag, add="+")
        self.root.bind_all("<ButtonRelease-1>", self._on_canvas_release, add="+")
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
        body.rowconfigure(0, weight=1)
        self.sidebar_body = body

        history_frame = ttk.LabelFrame(body, text="Conversation", padding=8)
        history_frame.grid(row=0, column=0, sticky="nsew")
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

    def _toggle_drag_palette_panel(self) -> None:
        self.drag_palette_panel_collapsed_var.set(not self.drag_palette_panel_collapsed_var.get())
        self._apply_drag_palette_panel_layout()

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

    def _apply_drag_palette_panel_layout_legacy_unused(self) -> None:
        if not self.drag_palette_body_frame or not self.drag_palette_toggle_button:
            return
        if self.drag_palette_panel_collapsed_var.get():
            self.drag_palette_body_frame.grid_remove()
            self.drag_palette_toggle_button.configure(text="唤起")
        else:
            if not self.drag_palette_body_frame.winfo_ismapped():
                self.drag_palette_body_frame.grid()
            self.drag_palette_toggle_button.configure(text="收起")

    def _apply_selection_panel_layout_legacy_unused(self) -> None:
        if not self.selection_body_frame or not self.selection_toggle_button:
            return
        if self.selection_panel_collapsed_var.get():
            self.selection_body_frame.grid_remove()
            self.selection_toggle_button.configure(text="唤起")
        else:
            if not self.selection_body_frame.winfo_ismapped():
                self.selection_body_frame.grid()
            self.selection_toggle_button.configure(text="收起")

    def _apply_execution_panel_layout_legacy_unused(self) -> None:
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

    def _apply_drag_palette_panel_layout(self) -> None:
        if not self.drag_palette_body_frame or not self.drag_palette_toggle_button:
            return
        if self.drag_palette_panel_collapsed_var.get():
            self.drag_palette_body_frame.grid_remove()
            self.drag_palette_toggle_button.configure(text="Show")
        else:
            if not self.drag_palette_body_frame.winfo_ismapped():
                self.drag_palette_body_frame.grid()
            self.drag_palette_toggle_button.configure(text="Hide")

    def _apply_selection_panel_layout(self) -> None:
        if not self.selection_body_frame or not self.selection_toggle_button:
            return
        if self.selection_panel_collapsed_var.get():
            self.selection_body_frame.grid_remove()
            self.selection_toggle_button.configure(text="Show")
        else:
            if not self.selection_body_frame.winfo_ismapped():
                self.selection_body_frame.grid()
            self.selection_toggle_button.configure(text="Hide")

    def _apply_execution_panel_layout(self) -> None:
        if not self.execution_body_frame or not self.execution_toggle_button:
            return
        if self.execution_panel_collapsed_var.get():
            self.execution_body_frame.grid_remove()
            self.execution_toggle_button.configure(text="Show")
        else:
            if not self.execution_body_frame.winfo_ismapped():
                self.execution_body_frame.grid()
            self.execution_toggle_button.configure(text="Hide")

    def _clear_marquee_state(self) -> None:
        if not self.marquee_state:
            return
        item_id = self.marquee_state.get("item_id")
        if self.canvas and item_id:
            try:
                self.canvas.delete(item_id)
            except tk.TclError:
                pass
        self.marquee_state = None

    def _reset_canvas_interaction_states(self) -> None:
        self._clear_marquee_state()
        if self.connection_drag_state and self.canvas:
            preview_line_id = self.connection_drag_state.get("line_id")
            if preview_line_id:
                try:
                    self.canvas.delete(preview_line_id)
                except tk.TclError:
                    pass
        self.connection_drag_state = None
        self.node_drag_state = None
        self.canvas_pan_state = None
        self.container_group_drag_state = None
        self.container_group_resize_state = None
        self.toolnode_resize_state = None
        self.container_copy_drag_state = None

    def _on_model_selection_changed(self, _event: tk.Event | None = None) -> None:
        self._sync_agent_client_settings()
        self._log(f"Switched model to {self.model_var.get().strip() or 'gpt-5.4-mini'}.")

    def _on_approval_mode_selection_changed(self, _event: tk.Event | None = None) -> None:
        self._sync_agent_client_settings()
        mode = self.approval_mode_var.get().strip() or "manual"
        self._log(f"Switched approval mode to {mode}.")

    def _sync_project_to_vars(self) -> None:
        self._normalize_project_algorithm_identity(self.project)
        suffix = self._algorithm_suffix_from_full_name(self.project.algorithm_name)
        prefix = self._algorithm_count_prefix()
        self.algorithm_name_prefix = prefix
        self.algorithm_identity_syncing = True
        try:
            self.algorithm_prefix_var.set(prefix)
            self.algorithm_suffix_var.set(suffix)
            self.project_name_var.set(self.project.algorithm_name)
            self.package_name_var.set(self.project.package_name)
        finally:
            self.algorithm_identity_syncing = False
        self.cpu_var.set(self.project.cpu_available)
        self.gpu_var.set(self.project.gpu_available)

    def _new_skeleton_project(self) -> ProjectState:
        project = ProjectState()
        self._ensure_singleton_container_group(project)
        self._normalize_project_algorithm_identity(project)
        return project

    def _apply_project_vars(self) -> None:
        self.project.algorithm_name = self.project_name_var.get().strip() or self._compose_algorithm_name("new_algorithm")
        self.project.package_name = self.package_name_var.get().strip() or self.project.algorithm_name
        self._normalize_project_algorithm_identity(self.project)
        self._sync_project_to_vars()
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
        if algorithm_name == self._algorithm_count_prefix():
            raise RuntimeError("Algorithm name suffix is required before build.")
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
        return PROJECT_ROOT / "algorithmLib" / "algorithmSrc" / algorithm_name

    def _existing_algorithm_catalog_path(self) -> Path:
        return PROJECT_ROOT / "algorithmLib" / "algorithmSrc" / "algorithm_catalog.json"

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

    def _function_script_assets(self) -> list[dict[str, Any]]:
        scripts: list[dict[str, Any]] = []
        function_text_lookup = {
            item.function_name: item.text.strip()
            for item in self.project.function_text_items
            if item.function_name.strip()
        }
        for item in self.project.function_frames:
            text = item.script.strip()
            if not text or text == FUNCTION_SCRIPT_PLACEHOLDER:
                continue
            scripts.append(
                {
                    "name": item.name,
                    "script_language": item.script_language.strip() or FUNCTION_SCRIPT_LANGUAGE_DEFAULT,
                    "input_name": item.input_name.strip() or "in",
                    "output_name": item.output_name.strip() or "out",
                    "expected_input": item.expected_input.strip(),
                    "expected_output": item.expected_output.strip(),
                    "script": text,
                    "notes": function_text_lookup.get(item.name, ""),
                }
            )
        return scripts

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
        script_assets = self._function_script_assets()

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

        plugin_path.write_text(self._generate_cpp_skeleton().rstrip() + "\n", encoding="utf-8")
        scripts_path = bundle_dir / f"{algorithm_name}_function_scripts.json"
        scripts_payload = {
            "algorithm_name": algorithm_name,
            "generated_at": datetime.now().isoformat(timespec="seconds"),
            "functions": script_assets,
        }
        scripts_path.write_text(json.dumps(scripts_payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

        self._update_algorithm_catalog_entry(algorithm_name, expected_manifest_name)
        return algorithm_name, bundle_dir

    def _summarize_build_output(self, stdout_text: str, stderr_text: str) -> str:
        lines = [line.strip() for line in (stdout_text + "\n" + stderr_text).splitlines() if line.strip()]
        if not lines:
            return "Build failed without output."
        preview = lines[-6:]
        return " | ".join(self._compact_activity_text(line, limit=72) for line in preview)

    def _run_build_command(self, algorithm_name: str) -> str:
        build_script = PROJECT_ROOT / "build_algorithm.bat"
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
        command_calls, response_without_commands = extract_interface4agents_script(response)
        tool_calls, visible_text = self._extract_agent_tool_calls(response_without_commands)
        summaries: list[str] = []
        failures: list[str] = []
        for tokens in command_calls:
            command_text = " ".join(tokens).strip()
            try:
                summaries.append(execute_interface4agents_command(self, tokens))
            except Exception as exc:  # noqa: BLE001
                failure_text = self._compact_activity_text(str(exc), limit=120) or "未知原因"
                failures.append(f"命令 {command_text} 执行失败：{failure_text}")
                self._log(f"interface4agents failed: {command_text} -> {failure_text}")
                continue
        for payload in tool_calls:
            tool_name = str(payload.get("tool") or "").strip()
            try:
                if tool_name in {"ui_add_node", "ui_update_node", "ui_delete_node", "ui_add_rule"}:
                    summaries.append(self._apply_agent_ui_tool(payload))
                    continue
                if tool_name == "update_document":
                    summaries.append(self._apply_agent_update_document_tool(payload))
                    continue
                raise RuntimeError(f"Unsupported agent tool: {tool_name or '(empty)'}")
            except Exception as exc:  # noqa: BLE001
                failure_text = self._compact_activity_text(str(exc), limit=120) or "未知原因"
                failures.append(f"工具 {tool_name or '(empty)'} 执行失败：{failure_text}")
                self._log(f"agent tool failed: {tool_name or '(empty)'} -> {failure_text}")
                continue
        parts: list[str] = []
        if visible_text:
            parts.append(visible_text)
        summary_text = "\n".join(part for part in summaries if part)
        if summary_text:
            parts.append(summary_text)
        if failures:
            failure_block = "\n".join(f"- {item}" for item in failures)
            parts.append("执行失败，已继续尝试其他可用方法：\n" + failure_block + "\n请确认是否需要改用别的命令或补充参数。")
        if parts:
            return "\n\n".join(parts)
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
                return f"Reused containerElement {name}."
            group = ContainerGroupItem(name=name, x=self._agent_tool_float(payload, "x", x_default), y=self._agent_tool_float(payload, "y", y_default), width=self._agent_tool_float(payload, "width", 360.0), height=self._agent_tool_float(payload, "height", 220.0))
            self.project.validate_container_group(group)
            self.project.container_groups.append(group)
            self.selected_container_group_name = name
            self._refresh_all()
            return f"Added containerElement {name}."
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
                if "script_language" in payload:
                    existing_function.script_language = str(payload.get("script_language") or FUNCTION_SCRIPT_LANGUAGE_DEFAULT)
                self.selected_function_name = name
                self._refresh_all()
                return f"Reused fun {name}."
            item = FunctionFrameItem(
                name=name,
                script=str(payload.get("script") or FUNCTION_SCRIPT_PLACEHOLDER),
                script_language=str(payload.get("script_language") or FUNCTION_SCRIPT_LANGUAGE_DEFAULT),
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
            return f"Updated containerElement {name}."
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
            if "script_language" in payload:
                item.script_language = str(payload.get("script_language") or FUNCTION_SCRIPT_LANGUAGE_DEFAULT)
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
            if name == "container":
                raise RuntimeError("Root container cannot be deleted.")
            self.selected_container_group_name = name
            self._delete_selected_container_group()
            return f"Deleted containerElement {name}."
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

    def _interface4agents_create_cos_node(self, name: str) -> str:
        payload = {
            "tool": "ui_add_node",
            "kind": "containerelement",
            "name": str(name).strip(),
        }
        return self._apply_agent_ui_add_node(payload)

    def _interface4agents_hang(self, child_name: str, parent_name: str) -> str:
        child = str(child_name).strip()
        parent = str(parent_name).strip()
        if self._find_container(child) is not None:
            self._add_container_to_group(child, parent)
        elif self._find_container_group(child) is not None:
            self._add_group_to_parent_group(child, parent)
        else:
            raise RuntimeError(f"hang child not found: {child}")
        self._interface4agents_integrate_child(parent)
        return f"Attached {child} into {parent}."

    def _interface4agents_integrate_child(self, group_name: str) -> str:
        group = self._find_container_group(str(group_name).strip())
        if not group:
            raise RuntimeError(f"containerElement not found: {group_name}")
        pack_names: list[str] = [group.name]
        parent_name = self._group_parent_group_name(group.name)
        while parent_name:
            pack_names.append(parent_name)
            parent_name = self._group_parent_group_name(parent_name)
        for pack_name in pack_names:
            pack_group = self._find_container_group(pack_name)
            if not pack_group:
                raise RuntimeError(f"containerElement not found: {pack_name}")
            self._pack_container_group_contents(pack_group)
        self._refresh_all()
        return f"Integrated child layout for {group.name}."

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
        self._ensure_singleton_container_group(project)
        self._normalize_project_algorithm_identity(project)
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
        self.container_copy_drag_state = None
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
        self._cancel_geometry_manifest_refresh()
        self._sync_all_container_groups()
        self._sync_selected_nodes()
        self._sync_project_to_vars()
        self._sync_project_manifest_cache()
        self._refresh_scene_tabs()
        self._refresh_container_list()
        self._refresh_rule_list()
        self._refresh_reflector_list()
        self._refresh_stage_list()
        self._refresh_preview()
        self._redraw_canvas()
        self._refresh_canvas_detail_panel()
        self._refresh_inspector()
        self._refresh_selection_panel()

    def _cancel_geometry_manifest_refresh(self) -> None:
        if self.geometry_manifest_after_id is None:
            return
        self.root.after_cancel(self.geometry_manifest_after_id)
        self.geometry_manifest_after_id = None

    def _flush_geometry_manifest_refresh(self) -> None:
        self.geometry_manifest_after_id = None
        self._sync_project_manifest_cache()
        self._refresh_preview()

    def _schedule_geometry_manifest_refresh(self, delay_ms: int = 180) -> None:
        self._cancel_geometry_manifest_refresh()
        self.geometry_manifest_after_id = self.root.after(delay_ms, self._flush_geometry_manifest_refresh)

    def _refresh_canvas_interaction_state(self, *, schedule_manifest_refresh: bool = True) -> None:
        self._sync_all_container_groups()
        self._sync_selected_nodes()
        self._sync_project_to_vars()
        self._refresh_scene_tabs()
        self._redraw_canvas()
        self._refresh_canvas_detail_panel()
        self._refresh_inspector()
        self._refresh_selection_panel()
        if schedule_manifest_refresh:
            self._schedule_geometry_manifest_refresh()

    def _refresh_container_list(self) -> None:
        if not self.container_list:
            return
        self.container_list.delete(0, tk.END)
        for container in self.project.containers:
            if self._is_resource_container_name(container.name):
                continue
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
        default_selection_name = self._next_group_name_for_zone(self._selection_sync_zone(self.selection_state))
        if self.selection_state:
            self.selection_name_var.set(str(self.selection_state.get("suggested_name") or default_selection_name))
        elif not self.selection_name_var.get().strip():
            self.selection_name_var.set(default_selection_name)
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
            groups = ", ".join(self.selection_state.get("groups", [])) or "-"
            resnodes = ", ".join(self.selection_state.get("resnodes", [])) or "-"
            return "\n".join(
                [
                    "Batch selection",
                    f"groups: {groups}",
                    f"variables: {variables}",
                    f"arrays: {arrays}",
                    f"resnodes: {resnodes}",
                    f"count: {len(self.selection_state.get('groups', [])) + len(self.selection_state.get('variables', [])) + len(self.selection_state.get('arrays', [])) + len(self.selection_state.get('resnodes', []))}",
                    "",
                    "Actions:",
                    "- Copy: store the current batch",
                    "- Merge: build a custom variable from the batch",
                    "- Arrange: stack the current batch from top to bottom",
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
                        "width": container.width,
                        "height": container.height,
                        "expand": container.expand,
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
        zone = self._selection_sync_zone(selection)
        default_group_name = self._next_group_name_for_zone(zone)
        name = self.selection_name_var.get().strip() or default_group_name
        if self._find_container_group(name):
            raise AssertionError(f"ContainerElement {name} already exists.")
        default_parent_group_name = self._sync_zone_root_group_name(zone)
        parent_group_name = str(selection.get("scope_group") or default_parent_group_name).strip() or default_parent_group_name
        if self._find_container_group(parent_group_name) is None:
            raise AssertionError(f"Parent containerElement {parent_group_name} does not exist.")
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
        for container_name in list(group.variables) + list(group.arrays):
            self._remove_container_from_parent_group(container_name)
        self._pack_container_group_contents(group)
        self.project.container_groups.append(group)
        self._add_group_to_parent_group(group.name, parent_group_name)
        parent_group = self._find_container_group(parent_group_name)
        if not parent_group:
            raise AssertionError(f"Parent containerElement {parent_group_name} does not exist.")
        self._pack_container_group_contents(parent_group)
        self.project.validate_container_group(group)
        self.selection_state = None
        self.selected_container_group_name = group.name
        self._refresh_all()
        self._log(f"Merged batch selection into custom variable {group.name}.")

    def _delete_current_selection(self) -> None:
        selection = self._current_batch_selection()
        if not selection:
            self._log("No batch selection to delete.")
            return
        names = list(selection.get("variables", [])) + list(selection.get("arrays", []))
        self.selection_state = None
        self._delete_containers_by_name(names)

    def _arrangement_domain(self, kind: str, name: str) -> str:
        zone = self._sync_zone_for_node(kind, name)
        return "decomposer" if zone == "resource" else "container"

    def _arrange_current_selection(self) -> None:
        selection = self._current_batch_selection()
        if not selection:
            self._log("No batch selection to arrange.")
            return
        selected_groups = [str(name).strip() for name in selection.get("groups", []) if str(name).strip()]
        selected_variables = [str(name).strip() for name in selection.get("variables", []) if str(name).strip()]
        selected_arrays = [str(name).strip() for name in selection.get("arrays", []) if str(name).strip()]
        selected_resnodes = [str(name).strip() for name in selection.get("resnodes", []) if str(name).strip()]

        arranged_groups: list[str] = []
        excluded_group_names: set[str] = set()
        excluded_container_names: set[str] = set()
        for group_name in sorted(selected_groups, key=lambda value: self._group_bounds(self._find_container_group(value))[1] if self._find_container_group(value) else float("inf")):
            if group_name in excluded_group_names:
                continue
            group = self._find_container_group(group_name)
            if not group:
                raise AssertionError(f"Missing containerElement {group_name}")
            arranged_groups.append(group_name)
            if self._is_node_expanded(group):
                excluded_group_names.update(self._group_descendant_names(group_name))
                excluded_container_names.update(self._container_names_in_group_tree(group_name))

        arranged_variables = [name for name in selected_variables if name not in excluded_container_names]
        arranged_arrays = [name for name in selected_arrays if name not in excluded_container_names]

        items_by_domain: dict[str, list[tuple[str, str]]] = {}
        for group_name in arranged_groups:
            items_by_domain.setdefault(self._arrangement_domain("containerelement", group_name), []).append(("containerelement", group_name))
        for container_name in arranged_variables:
            items_by_domain.setdefault(self._arrangement_domain("container", container_name), []).append(("container", container_name))
        for container_name in arranged_arrays:
            items_by_domain.setdefault(self._arrangement_domain("container", container_name), []).append(("container", container_name))
        for resnode_name in selected_resnodes:
            items_by_domain.setdefault(self._arrangement_domain("resnode", resnode_name), []).append(("resnode", resnode_name))

        arranged_count = 0
        for domain, items in items_by_domain.items():
            if not items:
                continue
            items.sort(key=lambda item: (self._node_bounds(item[0], item[1])[1], self._node_bounds(item[0], item[1])[0]))
            top = min(self._node_bounds(kind, name)[1] for kind, name in items)
            center_x = sum(self._node_center(kind, name)[0] for kind, name in items) / len(items)
            cursor_y = top
            gap = 16.0
            for kind, name in items:
                left, current_top, right, bottom = self._node_bounds(kind, name)
                width = right - left
                height = bottom - current_top
                target_left = center_x - width / 2
                dx = target_left - left
                dy = cursor_y - current_top
                if kind == "containerelement":
                    group = self._find_container_group(name)
                    if not group:
                        raise AssertionError(f"Missing containerElement {name}")
                    self._move_container_group_and_members(group, dx, dy)
                elif kind == "container":
                    container = self._find_container(name)
                    if not container:
                        raise AssertionError(f"Missing container {name}")
                    container.x += dx
                    container.y += dy
                elif kind == "resnode":
                    resnode = self._find_res_node(name)
                    if not resnode:
                        raise AssertionError(f"Missing resNode {name}")
                    resnode.x += dx
                    resnode.y += dy
                else:
                    raise AssertionError(f"Unsupported arrange kind: {kind}")
                cursor_y += height + gap
                arranged_count += 1
        self._refresh_canvas_interaction_state(schedule_manifest_refresh=False)
        self._schedule_geometry_manifest_refresh()
        self._log(f"Arranged {arranged_count} selected item(s).")

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
            source_name = str(entry.get("name", "")).strip()
            name = self.project.next_resource_container_name(kind) if self._is_resource_container_name(source_name) else self.project.next_container_name(kind)
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
                width=float(entry.get("width", 0.0)),
                height=float(entry.get("height", 0.0)),
                expand=bool(entry.get("expand", False)),
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

    def _collapsed_port_node_size(
        self,
        summary_lines: list[str],
        input_ports: list[str],
        output_ports: list[str],
        width: float,
    ) -> tuple[float, float]:
        visible_lines = [line.strip() for line in summary_lines if str(line).strip()] or ["-"]
        node_width = max(180.0, min(float(width), 260.0))
        header_height = 26.0
        port_rows = max(len(input_ports), len(output_ports), 1)
        summary_rows = min(len(visible_lines), 3)
        height = max(92.0, header_height + 14.0 + max(summary_rows, port_rows) * 18.0 + 18.0)
        return node_width, height

    def _create_rounded_rect(
        self,
        canvas: tk.Canvas,
        x0: float,
        y0: float,
        x1: float,
        y1: float,
        radius: float,
        **kwargs: Any,
    ) -> int:
        safe_radius = max(0.0, min(float(radius), abs(x1 - x0) * 0.5, abs(y1 - y0) * 0.5))
        points = [
            x0 + safe_radius,
            y0,
            x1 - safe_radius,
            y0,
            x1,
            y0,
            x1,
            y0 + safe_radius,
            x1,
            y1 - safe_radius,
            x1,
            y1,
            x1 - safe_radius,
            y1,
            x0 + safe_radius,
            y1,
            x0,
            y1,
            x0,
            y1 - safe_radius,
            x0,
            y0 + safe_radius,
            x0,
            y0,
        ]
        return canvas.create_polygon(points, smooth=True, splinesteps=24, **kwargs)

    def _draw_top_rounded_header(
        self,
        canvas: tk.Canvas,
        x0: float,
        y0: float,
        x1: float,
        y1: float,
        radius: float,
        fill: str,
        tags: tuple[str, ...],
    ) -> None:
        safe_radius = max(0.0, min(float(radius), abs(x1 - x0) * 0.5, abs(y1 - y0)))
        canvas.create_rectangle(
            x0 + safe_radius,
            y0,
            x1 - safe_radius,
            y1,
            fill=fill,
            outline="",
            tags=tags,
        )
        canvas.create_rectangle(
            x0,
            y0 + safe_radius,
            x1,
            y1,
            fill=fill,
            outline="",
            tags=tags,
        )
        canvas.create_arc(
            x0,
            y0,
            x0 + safe_radius * 2.0,
            y0 + safe_radius * 2.0,
            start=90,
            extent=90,
            style=tk.PIESLICE,
            fill=fill,
            outline="",
            tags=tags,
        )
        canvas.create_arc(
            x1 - safe_radius * 2.0,
            y0,
            x1,
            y0 + safe_radius * 2.0,
            start=0,
            extent=90,
            style=tk.PIESLICE,
            fill=fill,
            outline="",
            tags=tags,
        )

    def _container_group_min_size(self, group: ContainerGroupItem) -> tuple[float, float]:
        parent_name = self._group_parent_group_name(group.name)
        if parent_name is not None and not self._is_hidden_root_group_name(parent_name):
            return 132.0, 120.0
        return 220.0, 160.0

    def _container_min_render_size(self, container: ContainerItem) -> tuple[float, float]:
        parent_group_name = self._container_parent_group_name(container.name)
        compact = parent_group_name is not None and not self._is_hidden_root_group_name(parent_group_name)
        width = 132.0 if compact else float(NODE_WIDTH)
        value_lines = self._container_value_preview(container, 2 if compact else 3)
        content_rows = max(len(value_lines), 1)
        height = max(92.0 if compact else 104.0, 62.0 + content_rows * 18.0)
        return width, height

    def _container_render_size(self, container: ContainerItem) -> tuple[float, float]:
        if not self._is_node_expanded(container, default=False):
            return self._collapsed_port_node_size(self._container_value_preview(container, 2), ["in"], ["out"], 196.0)
        min_width, min_height = self._container_min_render_size(container)
        width = float(getattr(container, "width", 0.0) or 0.0)
        height = float(getattr(container, "height", 0.0) or 0.0)
        return max(width, min_width), max(height, min_height)

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

    def _add_container(self, kind: str) -> None:
        name = self._next_container_name_for_zone(kind, self._primary_sync_zone_for_view())
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
        default_name = self._next_container_name_for_zone(kind, self._primary_sync_zone_for_view())
        name = self.container_name_var.get().strip() or default_name
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
        duplicate = self._duplicate_container_with_reuse(container)
        self.selected_container_name = duplicate.name
        self._refresh_all()
        self._log(f"Duplicated container to {duplicate.name}.")

    def _container_reuse_chain_next_index(self, chain_id: str) -> int:
        return max(
            (item.reuse_chain_index for item in self.project.containers if item.reuse_chain_id == chain_id),
            default=-1,
        ) + 1

    def _duplicate_container_with_reuse(
        self,
        container: ContainerItem,
        *,
        x: float | None = None,
        y: float | None = None,
    ) -> ContainerItem:
        source_chain_id = container.reuse_chain_id or self.project.next_container_reuse_chain_id()
        if not container.reuse_chain_id:
            container.reuse_chain_id = source_chain_id
            container.reuse_chain_index = 0
        duplicate = copy.deepcopy(container)
        duplicate.name = self.project.next_resource_container_name(container.kind) if self._is_resource_container_name(container.name) else self.project.next_container_name(container.kind)
        duplicate.reuse_chain_id = source_chain_id
        duplicate.reuse_chain_index = self._container_reuse_chain_next_index(source_chain_id)
        duplicate.x = container.x + 36 if x is None else x
        duplicate.y = container.y + 36 if y is None else y
        self.project.containers.append(duplicate)
        return duplicate

    def _duplicate_canvas_node(
        self,
        kind: str,
        name: str,
        *,
        x: float | None = None,
        y: float | None = None,
    ) -> tuple[str, str]:
        normalized_kind = self._normalize_selected_kind(kind)
        if normalized_kind == "container":
            source = self._find_container(name)
            if source is None:
                raise AssertionError(f"Missing container {name}")
            duplicate = self._duplicate_container_with_reuse(source, x=x, y=y)
            return "container", duplicate.name
        if normalized_kind == "decomposer":
            source = self._find_rule(name)
            if source is None:
                raise AssertionError(f"Missing decomposer {name}")
            duplicate = copy.deepcopy(source)
            duplicate.name = self.project.next_decomposer_name()
            duplicate.x = source.x + 36.0 if x is None else x
            duplicate.y = source.y + 36.0 if y is None else y
            self.project.decomposer_rules.append(duplicate)
            return "decomposer", duplicate.name
        if normalized_kind == "reflector":
            source = self._find_reflector(name)
            if source is None:
                raise AssertionError(f"Missing reflector {name}")
            duplicate = copy.deepcopy(source)
            duplicate.name = self.project.next_reflector_name()
            duplicate.x = source.x + 36.0 if x is None else x
            duplicate.y = source.y + 36.0 if y is None else y
            self.project.reflector_items.append(duplicate)
            return "reflector", duplicate.name
        if normalized_kind == "resnode":
            source = self._find_res_node(name)
            if source is None:
                raise AssertionError(f"Missing resnode {name}")
            duplicate = copy.deepcopy(source)
            duplicate.name = self.project.next_res_name()
            duplicate.x = source.x + 36.0 if x is None else x
            duplicate.y = source.y + 36.0 if y is None else y
            self.project.res_nodes.append(duplicate)
            return "resnode", duplicate.name
        if normalized_kind == "function":
            source = self._find_function_frame(name)
            if source is None:
                raise AssertionError(f"Missing function {name}")
            duplicate = copy.deepcopy(source)
            duplicate.name = self.project.next_function_name()
            duplicate.x = source.x + 36.0 if x is None else x
            duplicate.y = source.y + 36.0 if y is None else y
            self.project.function_frames.append(duplicate)
            return "function", duplicate.name
        if normalized_kind == "functiontext":
            source = self._find_function_text_item(name)
            if source is None:
                raise AssertionError(f"Missing functiontext {name}")
            duplicate = copy.deepcopy(source)
            duplicate.name = self.project.next_function_text_name(source.function_name or source.name)
            duplicate.x = source.x + 36.0 if x is None else x
            duplicate.y = source.y + 36.0 if y is None else y
            self.project.function_text_items.append(duplicate)
            return "functiontext", duplicate.name
        if normalized_kind == "interventioner":
            source = self._find_stage(name)
            if source is None:
                raise AssertionError(f"Missing stage {name}")
            duplicate = copy.deepcopy(source)
            duplicate.name = self.project.next_stage_name()
            duplicate.x = source.x + 36.0 if x is None else x
            duplicate.y = source.y + 36.0 if y is None else y
            self.project.intervention_stages.append(duplicate)
            return "interventioner", duplicate.name
        raise AssertionError(f"Unsupported node duplicate kind: {kind}")

    def _set_canvas_node_position(self, kind: str, name: str, x: float, y: float) -> None:
        node = self._node_by_kind_name(self._normalize_selected_kind(kind), name)
        if node is None:
            raise AssertionError(f"Missing node while updating position: {kind}:{name}")
        if not hasattr(node, "x") or not hasattr(node, "y"):
            raise AssertionError(f"Node does not expose canvas coordinates: {kind}:{name}")
        node.x = x
        node.y = y

    def _duplicate_canvas_node_and_refresh(self, kind: str, name: str) -> None:
        duplicate_kind, duplicate_name = self._duplicate_canvas_node(kind, name)
        self._select_item_on_canvas(duplicate_kind, duplicate_name)
        self._refresh_all()
        self._log(f"Duplicated {duplicate_kind} to {duplicate_name}.")

    def _delete_selected_container(self) -> None:
        container = self._current_container()
        if not container:
            return
        self._delete_container_by_name(container.name)

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
        if group.name == "container":
            raise AssertionError("Root container cannot be deleted.")
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
        self.chat_history_compaction_summary = ""
        self.chat_history_compacted_item_count = 0
        self.agent_client.reset_session_state()
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

    def _chat_user_assistant_items(self) -> list[dict[str, Any]]:
        return [item for item in self.chat_history if item["role"] in {"user", "assistant"}]

    def _chat_request_count(self) -> int:
        return sum(1 for item in self.chat_history if item["role"] == "user")

    def _chat_history_for_compaction(self) -> str:
        items = self._chat_user_assistant_items()
        pending_items = items[self.chat_history_compacted_item_count :]
        sections: list[str] = []
        if self.chat_history_compaction_summary:
            sections.append("Existing compacted conversation summary:\n" + self.chat_history_compaction_summary.strip())
        if pending_items:
            lines = [self._compact_activity_text(self._chat_history_transcript_line(item), limit=400) for item in pending_items]
            lines = [line for line in lines if line]
            if lines:
                sections.append("Conversation turns to merge:\n" + "\n".join(lines))
        if not sections:
            raise AssertionError("No chat history is available for compaction.")
        return "\n\n".join(sections)

    def _chat_history_for_model(self) -> str:
        mode = self.agent_client.conversation_history_mode()
        if mode == "server_managed":
            return ""
        items = self._chat_user_assistant_items()
        if not items:
            return "(empty)"
        if mode == "checkpoint_summary":
            pending_items = items[self.chat_history_compacted_item_count :]
            sections: list[str] = []
            if self.chat_history_compaction_summary:
                sections.append("Compacted conversation summary:\n" + self.chat_history_compaction_summary.strip())
            if pending_items:
                lines = [self._compact_activity_text(self._chat_history_transcript_line(item), limit=220) for item in pending_items]
                lines = [line for line in lines if line]
                if lines:
                    sections.append("Recent turns:\n" + "\n".join(lines))
            return "\n\n".join(section for section in sections if section) or "(empty)"
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
        self._interface4agents_hot_review()

    def _canvas_node_item_id(self, kind: str, name: str) -> int | None:
        if kind == "containerelement":
            return self.canvas_container_group_nodes.get(name)
        return self.canvas_nodes.get(name)

    def _interface4agents_flash_canvas_item(self, item_id: int) -> None:
        if not self.canvas:
            return
        bbox = self.canvas.bbox(item_id)
        if bbox is None:
            return
        left, top, right, bottom = bbox
        overlays = [
            (left - 6, top - 6, right + 6, bottom + 6, "#6b0000", 3),
            (left - 3, top - 3, right + 3, bottom + 3, "#c81e1e", 2),
            (left, bottom - 4, right, bottom + 4, "#ff6b6b", 3),
        ]
        flash_ids: list[int] = []
        for x0, y0, x1, y1, color, width in overlays:
            flash_ids.append(
                self.canvas.create_rectangle(
                    x0,
                    y0,
                    x1,
                    y1,
                    outline=color,
                    width=width,
                    tags=("interface4agents_flash",),
                )
            )
        self.canvas.tag_raise("interface4agents_flash")

        def _clear_flash() -> None:
            if not self.canvas:
                return
            for flash_id in flash_ids:
                try:
                    self.canvas.delete(flash_id)
                except tk.TclError:
                    pass

        self.canvas.after(900, _clear_flash)

    def _interface4agents_highlight_node(self, kind: str, name: str) -> str:
        normalized_kind = str(kind).strip().lower()
        normalized_name = str(name).strip()
        item_id = self._canvas_node_item_id(normalized_kind, normalized_name)
        if item_id is None:
            raise RuntimeError(f"Node not visible on canvas: {normalized_kind}:{normalized_name}")
        self._interface4agents_flash_canvas_item(item_id)
        return f"Highlight: {normalized_kind}:{normalized_name}"

    def _interface4agents_highlight_named_node(self, name: str) -> str:
        normalized_name = str(name).strip()
        for kind, node_name, _item in self.project.iter_nodes():
            if node_name != normalized_name:
                continue
            try:
                return self._interface4agents_highlight_node(kind, node_name)
            except RuntimeError:
                continue
        raise RuntimeError(f"Node not visible on canvas: {normalized_name}")

    def _interface4agents_hot_review(self) -> str:
        if not self.canvas:
            return "Canvas is not ready."
        kind = None
        name = None
        if self.selected_container_group_name:
            kind = "containerelement"
            name = self.selected_container_group_name
        elif self.selected_container_name:
            kind = "container"
            name = self.selected_container_name
        elif self.selected_rule_name:
            kind = "decomposer"
            name = self.selected_rule_name
        elif self.selected_reflector_name:
            kind = "reflector"
            name = self.selected_reflector_name
        elif self.selected_res_node_name:
            kind = "resnode"
            name = self.selected_res_node_name
        elif self.selected_function_name:
            kind = "function"
            name = self.selected_function_name
        elif self.selected_function_text_name:
            kind = "functiontext"
            name = self.selected_function_text_name
        elif self.selected_stage_name:
            kind = "interventioner"
            name = self.selected_stage_name
        if kind and name:
            item_id = self._canvas_node_item_id(kind, name)
            if item_id is not None:
                bbox = self.canvas.bbox(item_id)
                if bbox is not None:
                    left, top, right, bottom = bbox
                    self._restore_canvas_anchor(
                        ((left + right) / 2) / self._canvas_zoom_factor(),
                        ((top + bottom) / 2) / self._canvas_zoom_factor(),
                        float(self.canvas.winfo_width()) / 2,
                        float(self.canvas.winfo_height()) / 2,
                    )
                    self._redraw_canvas()
                self._log(f"Hot reviewed {kind}:{name}.")
                return f"Hot reviewed {kind}:{name}."
        for kind, node_name, _item in self.project.iter_nodes():
            if not self._is_node_visible_in_current_view(kind, node_name):
                continue
            item_id = self._canvas_node_item_id(kind, node_name)
            if item_id is None:
                continue
            bbox = self.canvas.bbox(item_id)
            if bbox is None:
                continue
            left, top, right, bottom = bbox
            self._restore_canvas_anchor(
                ((left + right) / 2) / self._canvas_zoom_factor(),
                ((top + bottom) / 2) / self._canvas_zoom_factor(),
                float(self.canvas.winfo_width()) / 2,
                float(self.canvas.winfo_height()) / 2,
            )
            self._redraw_canvas()
            self._log(f"Hot reviewed {kind}:{node_name}.")
            return f"Hot reviewed {kind}:{node_name}."
        return "No visible nodes to review."

    def _drag_delete_warning_target(self) -> tuple[str, str] | None:
        if self.node_drag_state and bool(self.node_drag_state.get("waste_area")):
            return str(self.node_drag_state.get("kind") or ""), str(self.node_drag_state.get("name") or "")
        if self.palette_drag_state and bool(self.palette_drag_state.get("waste_area")) and bool(self.palette_drag_state.get("active")):
            return str(self.palette_drag_state.get("kind") or ""), str(self.palette_drag_state.get("name") or "")
        return None

    def _draw_drag_delete_warning(self, canvas: tk.Canvas) -> None:
        warning = self._drag_delete_warning_target()
        if not warning:
            return
        kind, name = warning
        item_id = self._canvas_node_item_id(kind, name)
        if item_id is None:
            return
        bbox = canvas.bbox(item_id)
        if bbox is None:
            return
        left, top, right, bottom = bbox
        overlays = [
            (left - 8, top - 8, right + 8, bottom + 8, "#7f1d1d", 4),
            (left - 4, top - 4, right + 4, bottom + 4, "#ef4444", 2),
            (left - 2, bottom - 6, right + 2, bottom + 2, "#fca5a5", 2),
        ]
        for x0, y0, x1, y1, color, width in overlays:
            canvas.create_rectangle(x0, y0, x1, y1, outline=color, width=width, tags=("drag_delete_warning",))
        canvas.tag_raise("drag_delete_warning")

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
        self._sync_selected_nodes()
        self._redraw_canvas()
        self._refresh_inspector()
        self._refresh_selection_panel()

    def _normalize_selected_kind(self, kind: str) -> str:
        if kind == "stage":
            return "interventioner"
        return kind

    def _selected_node_exists(self, kind: str, name: str) -> bool:
        normalized_kind = self._normalize_selected_kind(kind)
        if normalized_kind == "decomposer":
            return self._find_rule(name) is not None
        if normalized_kind == "reflector":
            return self._find_reflector(name) is not None
        if normalized_kind == "resnode":
            return self._find_res_node(name) is not None
        if normalized_kind == "function":
            return self._find_function_frame(name) is not None
        if normalized_kind == "functiontext":
            return self._find_function_text_item(name) is not None
        if normalized_kind == "interventioner":
            return self._find_stage(name) is not None
        if normalized_kind == "containerelement":
            return self._find_container_group(name) is not None
        if normalized_kind == "container":
            return self._find_container(name) is not None
        return False

    def _selected_nodes_from_state(self) -> list[dict[str, str]]:
        selected_nodes: list[dict[str, str]] = []
        seen: set[tuple[str, str]] = set()

        def add(kind: str, name: str) -> None:
            normalized_kind = self._normalize_selected_kind(kind)
            key = (normalized_kind, name)
            if key in seen:
                return
            if not self._selected_node_exists(normalized_kind, name):
                return
            seen.add(key)
            selected_nodes.append({"kind": normalized_kind, "name": name})

        if self.selection_state:
            for group_name in self.selection_state.get("groups", []):
                add("containerelement", str(group_name))
            for container_name in self.selection_state.get("variables", []):
                add("container", str(container_name))
            for container_name in self.selection_state.get("arrays", []):
                add("container", str(container_name))
            for res_name in self.selection_state.get("resnodes", []):
                add("resnode", str(res_name))
            return selected_nodes

        if self.selected_container_group_name:
            add("containerelement", self.selected_container_group_name)
        if self.selected_container_name:
            add("container", self.selected_container_name)
        if self.selected_rule_name:
            add("decomposer", self.selected_rule_name)
        if self.selected_reflector_name:
            add("reflector", self.selected_reflector_name)
        if self.selected_res_node_name:
            add("resnode", self.selected_res_node_name)
        if self.selected_function_name:
            add("function", self.selected_function_name)
        if self.selected_function_text_name:
            add("functiontext", self.selected_function_text_name)
        if self.selected_stage_name:
            add("interventioner", self.selected_stage_name)
        return selected_nodes

    def _sync_selected_nodes(self) -> None:
        self.selected = self._selected_nodes_from_state()

    def _is_canvas_node_selected(self, kind: str, name: str) -> bool:
        normalized_kind = self._normalize_selected_kind(kind)
        return any(
            str(item.get("kind") or "") == normalized_kind and str(item.get("name") or "") == name
            for item in self.selected
        )

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

    def _detach_container_from_group(self, container_name: str, group_name: str) -> bool:
        group = self._find_container_group(group_name)
        if not group:
            raise AssertionError(f"Missing containerElement {group_name}")
        removed = False
        if container_name in group.variables:
            group.variables = [name for name in group.variables if name != container_name]
            removed = True
        if container_name in group.arrays:
            group.arrays = [name for name in group.arrays if name != container_name]
            removed = True
        return removed

    def _detach_group_from_parent(self, group_name: str, parent_name: str) -> bool:
        parent_group = self._find_container_group(parent_name)
        if not parent_group:
            raise AssertionError(f"Missing containerElement {parent_name}")
        if group_name not in parent_group.groups:
            return False
        parent_group.groups = [name for name in parent_group.groups if name != group_name]
        return True

    def _attach_dragged_node_to_group_if_inside(self, drag_state: dict[str, Any]) -> bool:
        kind = str(drag_state.get("kind") or "").strip()
        name = str(drag_state.get("name") or "").strip()
        if kind not in {"container", "containerelement"}:
            return False
        rect = self._node_bounds(kind, name)
        exclude_group_names: set[str] = set()
        if kind == "containerelement":
            exclude_group_names.add(name)
            exclude_group_names.update(self._group_descendant_names(name))
        parent_name = self._smallest_group_containing_rect(rect, exclude_group_names=exclude_group_names)
        if not parent_name:
            return False
        if kind == "container":
            current_parent = self._container_parent_group_name(name)
            if current_parent == parent_name:
                return True
            self._add_container_to_group(name, parent_name)
            self._log(f"Attached {name} into {parent_name}.")
            return True
        current_parent = self._group_parent_group_name(name)
        if current_parent == parent_name:
            return True
        self._add_group_to_parent_group(name, parent_name)
        self._log(f"Attached {name} into {parent_name}.")
        return True

    def _detach_dragged_node_if_outside_parent(self, drag_state: dict[str, Any]) -> None:
        kind = str(drag_state.get("kind") or "").strip()
        name = str(drag_state.get("name") or "").strip()
        original_parent_group = str(drag_state.get("original_parent_group") or "").strip()
        if not original_parent_group:
            return
        parent_group = self._find_container_group(original_parent_group)
        if not parent_group:
            raise AssertionError(f"Missing original parent containerElement {original_parent_group}")
        if kind == "container":
            container = self._find_container(name)
            if not container:
                raise AssertionError(f"Missing container {name}")
            if self.project._rect_contains_rect(*self._group_bounds(parent_group), *self._container_rect(container)):
                return
            if self._detach_container_from_group(name, original_parent_group):
                self._log(f"Detached {name} from {original_parent_group}.")
            return
        if kind == "containerelement":
            group = self._find_container_group(name)
            if not group:
                raise AssertionError(f"Missing containerElement {name}")
            if self.project._rect_contains_rect(*self._group_bounds(parent_group), *self._group_bounds(group)):
                return
            if self._detach_group_from_parent(name, original_parent_group):
                self._log(f"Detached {name} from {original_parent_group}.")

    def _node_bounds(self, kind: str, name: str) -> tuple[float, float, float, float]:
        if kind == "container":
            container = self._find_container(name)
            if not container:
                raise AssertionError(f"Missing container {name}")
            return self._container_rect(container)
        if kind == "containerelement":
            group = self._find_container_group(name)
            if not group:
                raise AssertionError(f"Missing containerElement {name}")
            return self._group_bounds(group)
        node = self._node_by_kind_name(kind, name)
        if node is None:
            raise AssertionError(f"Missing node {kind}:{name}")
        width = float(getattr(node, "width", BLUEPRINT_NODE_WIDTH))
        height = float(getattr(node, "height", BLUEPRINT_NODE_MIN_HEIGHT))
        x = float(getattr(node, "x", 0.0))
        y = float(getattr(node, "y", 0.0))
        return x, y, x + width, y + height

    def _move_top_level_node_by(self, kind: str, name: str, dy: float) -> None:
        if dy <= 0.0:
            return
        if kind == "container":
            container = self._find_container(name)
            if not container:
                raise AssertionError(f"Missing container {name}")
            container.y += dy
            return
        if kind == "containerelement":
            group = self._find_container_group(name)
            if not group:
                raise AssertionError(f"Missing containerElement {name}")
            self._move_container_group_and_members(group, 0.0, dy)
            return
        node = self._node_by_kind_name(kind, name)
        if node is None:
            raise AssertionError(f"Missing node {kind}:{name}")
        node.y = float(getattr(node, "y", 0.0)) + dy

    def _draw_child_clip_masks(self, canvas: tk.Canvas) -> None:
        for kind, name, _item in self.project.iter_nodes():
            if kind not in {"container", "containerelement"}:
                continue
            if not self._is_node_visible_in_current_view(kind, name):
                continue
            parent_name = self._nearest_parent_group_name(kind, name)
            if not parent_name:
                continue
            if parent_name in {"container", RESOURCE_ROOT_GROUP_NAME}:
                continue
            parent_group = self._find_container_group(parent_name)
            if not parent_group:
                raise AssertionError(f"Missing parent containerElement {parent_name}")
            if not self._is_node_expanded(parent_group):
                continue
            child_left, child_top, child_right, child_bottom = self._node_bounds(kind, name)
            clip_left, clip_top, clip_right, clip_bottom = self._group_body_bounds(parent_group)
            if child_left < clip_left:
                self._create_scene_clip_mask(canvas, child_left, child_top, clip_left, child_bottom)
            if child_right > clip_right:
                self._create_scene_clip_mask(canvas, clip_right, child_top, child_right, child_bottom)
            if child_top < clip_top:
                self._create_scene_clip_mask(
                    canvas,
                    max(child_left, clip_left),
                    child_top,
                    min(child_right, clip_right),
                    clip_top,
                )
            if child_bottom > clip_bottom:
                self._create_scene_clip_mask(
                    canvas,
                    max(child_left, clip_left),
                    clip_bottom,
                    min(child_right, clip_right),
                    child_bottom,
                )

    def _clip_mask_fill_for_span(self, left: float, right: float) -> str:
        if self.canvas_view_mode != "decomposer2container_overview":
            return COLORS["canvas"]
        divider_x = self._d2c_divider_x()
        midpoint = (left + right) * 0.5
        return "#12161f" if midpoint <= divider_x else "#141a22"

    def _d2c_divider_x(self) -> float:
        scene_width = 1440.0
        if self.canvas:
            scene_width = max(float(self.canvas.winfo_width()) / self._canvas_zoom_factor(), scene_width)
        return scene_width * 0.5

    def _create_scene_clip_mask(
        self,
        canvas: tk.Canvas,
        left: float,
        top: float,
        right: float,
        bottom: float,
    ) -> None:
        if right <= left or bottom <= top:
            return
        if self.canvas_view_mode == "decomposer2container_overview":
            divider_x = self._d2c_divider_x()
            if left < divider_x < right:
                self._create_scene_clip_mask(canvas, left, top, divider_x, bottom)
                self._create_scene_clip_mask(canvas, divider_x, top, right, bottom)
                return
        fill = self._clip_mask_fill_for_span(left, right)
        canvas.create_rectangle(left, top, right, bottom, fill=fill, outline=fill, tags=("clip_mask",))

    def _draw_group_clip_overlays(self, canvas: tk.Canvas) -> None:
        for group in sorted(self.project.container_groups, key=lambda item: item.width * item.height):
            if not self._is_node_visible_in_current_view("containerelement", group.name):
                continue
            if not self._is_node_expanded(group):
                continue
            if not group.variables and not group.arrays and not group.groups:
                continue
            raw_x = group.x or CANVAS_PADDING + 20
            raw_y = group.y or CANVAS_PADDING + 20
            x, y = self._container_display_origin("containerelement", group.name, raw_x, raw_y)
            width, height = self._container_group_render_size(group)
            header_height = 30.0
            outline = COLORS["accent"] if self.selected_container_group_name == group.name else COLORS["good"]
            node_tag = f"group_clip_overlay:{group.name}"
            canvas.create_rectangle(
                x,
                y,
                x + width,
                y + height,
                fill="",
                outline=outline,
                width=2,
                dash=(4, 3),
                tags=("group_clip_overlay", node_tag),
            )
            canvas.create_rectangle(
                x,
                y,
                x + width,
                y + header_height,
                fill=COLORS["good"],
                outline=outline,
                width=2,
                tags=("group_clip_overlay", node_tag),
            )
            canvas.create_text(
                x + 12,
                y + 7,
                anchor="nw",
                fill=COLORS["window"],
                text="container" if group.name == "container" else group.name,
                font=("Segoe UI", 12, "bold"),
                tags=("group_clip_overlay", node_tag),
            )

    def _resolve_expanded_container_group_overlaps(self) -> None:
        if self.canvas_view_mode != "container_overview":
            return
        margin = 24.0
        top_level_items: list[tuple[str, str]] = []
        for group in self.project.container_groups:
            if group.name == "container":
                continue
            if self._group_parent_group_name(group.name) is None:
                top_level_items.append(("containerelement", group.name))
        for container in self.project.containers:
            if self._container_parent_group_name(container.name) is None:
                top_level_items.append(("container", container.name))
        for kind, name, _item in self.project.iter_nodes():
            if kind in {"containerelement", "container"}:
                continue
            top_level_items.append((kind, name))
        for group in sorted(self.project.container_groups, key=lambda item: item.y):
            if group.name == "container":
                continue
            if not self._is_node_expanded(group):
                continue
            group_left, group_top, group_right, group_bottom = self._group_bounds(group)
            related_groups = {group.name} | self._group_descendant_names(group.name) | self._group_ancestor_names(group.name)
            related_containers = self._container_names_in_group_tree(group.name)
            for candidate_kind, candidate_name in top_level_items:
                if candidate_kind == "containerelement" and candidate_name in related_groups:
                    continue
                if candidate_kind == "container" and candidate_name in related_containers:
                    continue
                left, top, right, bottom = self._node_bounds(candidate_kind, candidate_name)
                if not self._rects_intersect(group_left, group_top, group_right, group_bottom, left, top, right, bottom):
                    continue
                target_top = group_bottom + margin
                dy = target_top - top
                self._move_top_level_node_by(candidate_kind, candidate_name, dy)

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
            left, top, right, bottom = self._container_rect(container)
            return (left + right) / 2, (top + bottom) / 2
        if kind == "containerelement":
            group = self._find_container_group(name)
            if not group:
                raise ValueError(f"Missing containerElement {name}")
            left, top, right, bottom = self._group_bounds(group)
            return (left + right) / 2, (top + bottom) / 2
        if kind == "resnode":
            left, top, right, bottom = self._node_bounds(kind, name)
            return (left + right) / 2, (top + bottom) / 2
        raise ValueError(f"Unsupported center lookup kind: {kind}")

    def _members_inside_rect(self, rect: tuple[float, float, float, float]) -> tuple[list[str], list[str]]:
        variables: list[str] = []
        arrays: list[str] = []
        for container in self.project.containers:
            if not self._is_node_visible_in_current_view("container", container.name):
                continue
            c_left, c_top, c_right, c_bottom = self._container_rect(container)
            if self._rects_intersect(rect[0], rect[1], rect[2], rect[3], c_left, c_top, c_right, c_bottom):
                if container.kind == "variable":
                    variables.append(container.name)
                elif container.kind == "array":
                    arrays.append(container.name)
                else:
                    raise AssertionError(f"Unsupported container kind: {container.kind}")
        return variables, arrays

    def _groups_inside_rect(self, rect: tuple[float, float, float, float]) -> list[str]:
        groups: list[str] = []
        for group in self.project.container_groups:
            if not self._is_node_visible_in_current_view("containerelement", group.name):
                continue
            g_left, g_top, g_right, g_bottom = self._group_bounds(group)
            if self._rects_intersect(rect[0], rect[1], rect[2], rect[3], g_left, g_top, g_right, g_bottom):
                groups.append(group.name)
        return groups

    def _resnodes_inside_rect(self, rect: tuple[float, float, float, float]) -> list[str]:
        names: list[str] = []
        for item in self.project.res_nodes:
            if not self._is_node_visible_in_current_view("resnode", item.name):
                continue
            left = float(item.x)
            top = float(item.y)
            width = float(getattr(item, "width", BLUEPRINT_NODE_WIDTH))
            height = float(getattr(item, "height", BLUEPRINT_NODE_MIN_HEIGHT))
            right = left + width
            bottom = top + height
            if self._rects_intersect(rect[0], rect[1], rect[2], rect[3], left, top, right, bottom):
                names.append(item.name)
        return names

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
        member_names = self._container_names_in_group_tree(group.name)
        for container in self.project.containers:
            if not self._is_node_visible_in_current_view("container", container.name):
                continue
            if container.name not in member_names:
                continue
            c_left, c_top, c_right, c_bottom = self._container_rect(container)
            if not self._rects_intersect(left, top, right, bottom, c_left, c_top, c_right, c_bottom):
                continue
            if container.kind == "variable":
                variables.append(container.name)
            elif container.kind == "array":
                arrays.append(container.name)
            else:
                raise AssertionError(f"Unsupported container kind: {container.kind}")
        return variables, arrays

    def _groups_inside_group_rect(
        self,
        group: ContainerGroupItem,
        rect: tuple[float, float, float, float],
    ) -> list[str]:
        names: list[str] = []
        group_left, group_top, group_right, group_bottom = self._group_bounds(group)
        left, top, right, bottom = self._normalize_rect(
            max(rect[0], group_left),
            max(rect[1], group_top),
            min(rect[2], group_right),
            min(rect[3], group_bottom),
        )
        if right < left or bottom < top:
            return names
        group_names = self._group_descendant_names(group.name)
        for candidate in self.project.container_groups:
            if candidate.name not in group_names:
                continue
            if not self._is_node_visible_in_current_view("containerelement", candidate.name):
                continue
            c_left, c_top, c_right, c_bottom = self._group_bounds(candidate)
            if self._rects_intersect(left, top, right, bottom, c_left, c_top, c_right, c_bottom):
                names.append(candidate.name)
        return names

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
            "Selected container" if group.name == "container" else "Selected custom variable",
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
                "- drag the corner handle to resize without changing membership",
                "- drag a child node out of the box to detach it",
                "- edit the document text for direct membership changes",
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
            self.project.validate_container_group(group)

    def _canvas_item_at(self, x: int, y: int) -> int | None:
        if not self.canvas:
            return None
        canvas_x = self.canvas.canvasx(x)
        canvas_y = self.canvas.canvasy(y)
        items = self.canvas.find_overlapping(canvas_x - 2, canvas_y - 2, canvas_x + 2, canvas_y + 2)
        if items:
            for item_id in reversed(items):
                tags = self.canvas.gettags(item_id)
                if not tags:
                    continue
                if "connection_preview" in tags:
                    continue
                if "marquee" in tags:
                    continue
                if "grid" in tags or "clip_mask" in tags or "group_clip_overlay" in tags or any(tag.startswith("grid_") for tag in tags):
                    continue
                return item_id
        closest = self.canvas.find_closest(canvas_x, canvas_y)
        if not closest:
            return None
        item_id = closest[0]
        tags = self.canvas.gettags(item_id)
        if not tags or "connection_preview" in tags or "marquee" in tags or "grid" in tags or "clip_mask" in tags or "group_clip_overlay" in tags or any(tag.startswith("grid_") for tag in tags):
            return None
        return item_id

    def _canvas_item_hit(self, x: int, y: int) -> int | None:
        if not self.canvas:
            return None
        canvas_x = self.canvas.canvasx(x)
        canvas_y = self.canvas.canvasy(y)
        items = self.canvas.find_overlapping(canvas_x - 2, canvas_y - 2, canvas_x + 2, canvas_y + 2)
        if not items:
            return None
        for item_id in reversed(items):
            tags = self.canvas.gettags(item_id)
            if not tags or "connection_preview" in tags or "marquee" in tags or "grid" in tags or "clip_mask" in tags or "group_clip_overlay" in tags or any(tag.startswith("grid_") for tag in tags):
                continue
            return item_id
        return None

    def _canvas_item_hit_nearby(self, x: int, y: int, radius: int = 12) -> int | None:
        if not self.canvas:
            return None
        canvas_x = self.canvas.canvasx(x)
        canvas_y = self.canvas.canvasy(y)
        items = self.canvas.find_overlapping(canvas_x - radius, canvas_y - radius, canvas_x + radius, canvas_y + radius)
        if not items:
            return None
        for item_id in reversed(items):
            tags = self.canvas.gettags(item_id)
            if not tags or "connection_preview" in tags or "marquee" in tags or "grid" in tags or "clip_mask" in tags or "group_clip_overlay" in tags or any(tag.startswith("grid_") for tag in tags):
                continue
            if any(tag.startswith("node:") for tag in tags):
                return item_id
        return None

    def _split_port_names(self, raw_value: str, default_name: str) -> list[str]:
        values = [part.strip() for part in str(raw_value or "").split(",") if part.strip()]
        return values or [default_name]

    def _canvas_zoom_factor(self) -> float:
        zoom = float(self.canvas_zoom)
        if zoom <= 0.0:
            raise AssertionError(f"Canvas zoom must stay positive, got {zoom}.")
        return zoom

    def _scene_delta(self, value: float) -> float:
        return value / self._canvas_zoom_factor()

    def _scene_point(self, x: float, y: float) -> tuple[float, float]:
        zoom = self._canvas_zoom_factor()
        if not self.canvas:
            return x / zoom, y / zoom
        return self.canvas.canvasx(x) / zoom, self.canvas.canvasy(y) / zoom

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

    def _mouse_wheel_steps(self, event: tk.Event) -> int:
        if getattr(event, "num", None) == 4:
            return 1
        if getattr(event, "num", None) == 5:
            return -1
        if getattr(event, "delta", 0):
            return 1 if event.delta > 0 else -1
        return 0

    def _on_scene_tabs_mouse_wheel(self, event: tk.Event) -> str | None:
        if not self.scene_tabs_canvas:
            return None
        steps = self._mouse_wheel_steps(event)
        if steps == 0:
            return "break"
        self.scene_tabs_canvas.xview_scroll(-steps * 3, "units")
        return "break"

    def _on_canvas_mouse_wheel(self, event: tk.Event) -> str | None:
        if not self.canvas:
            return None
        steps = self._mouse_wheel_steps(event)
        if steps == 0:
            return "break"
        state = int(getattr(event, "state", 0) or 0)
        item_id = self._canvas_item_hit(event.x, event.y)
        if item_id is not None:
            tags = self.canvas.gettags(item_id)
            kind, node_name = self._node_info_from_tags(tags)
            if kind == "container" and node_name and (state & 0x0008):
                container = self._find_container(node_name)
                if container and container.kind == "array":
                    container.view_offset = max(0, container.view_offset - steps)
                    self._refresh_all()
                    return "break"
        if state & 0x0001:
            self.canvas.xview_scroll(-steps * 4, "units")
        else:
            self.canvas.yview_scroll(-steps * 4, "units")
        self._redraw_canvas()
        return "break"

    def _on_palette_mouse_wheel(self, event: tk.Event) -> str | None:
        if not self.palette_canvas:
            return None
        steps = self._mouse_wheel_steps(event)
        if steps == 0:
            return "break"
        self.palette_canvas.yview_scroll(-steps * 3, "units")
        return "break"

    def _drag_canvas_pan(self, x: int, y: int) -> None:
        if not self.canvas_pan_state or not self.canvas:
            raise AssertionError("Canvas pan drag requested without active state.")
        self.canvas_pan_state["x"] = x
        self.canvas_pan_state["y"] = y
        self.canvas.scan_dragto(x, y, gain=1)
        self._redraw_canvas()

    def _finish_canvas_pan(self) -> None:
        if not self.canvas_pan_state:
            return
        self.canvas_pan_state = None
        self._redraw_canvas()

    def _expanded_scrollregion(self, canvas: tk.Canvas) -> tuple[float, float, float, float]:
        bbox = canvas.bbox("all")
        width = max(float(canvas.winfo_width()), 1.0)
        height = max(float(canvas.winfo_height()), 1.0)
        margin = max(width, height, 4000.0)
        if not bbox:
            return -margin, -margin, margin, margin
        left, top, right, bottom = [float(value) for value in bbox]
        return left - margin, top - margin, right + margin, bottom + margin

    def _current_canvas_scrollregion(self) -> tuple[float, float, float, float]:
        if not self.canvas:
            raise AssertionError("Canvas scrollregion requested before canvas initialization.")
        raw_value = str(self.canvas.cget("scrollregion")).strip()
        if not raw_value:
            return self._expanded_scrollregion(self.canvas)
        parts = raw_value.split()
        if len(parts) != 4:
            raise AssertionError(f"Invalid canvas scrollregion: {raw_value!r}")
        return tuple(float(value) for value in parts)

    def _restore_canvas_anchor(self, scene_x: float, scene_y: float, screen_x: float, screen_y: float) -> None:
        if not self.canvas:
            raise AssertionError("Canvas anchor restore requested before canvas initialization.")
        left, top, right, bottom = self._current_canvas_scrollregion()
        viewport_width = max(float(self.canvas.winfo_width()), 1.0)
        viewport_height = max(float(self.canvas.winfo_height()), 1.0)
        zoom = self._canvas_zoom_factor()
        target_canvas_x = scene_x * zoom
        target_canvas_y = scene_y * zoom
        desired_left = target_canvas_x - float(screen_x)
        desired_top = target_canvas_y - float(screen_y)
        total_width = right - left
        total_height = bottom - top
        if total_width <= viewport_width:
            self.canvas.xview_moveto(0.0)
        else:
            x_fraction = (desired_left - left) / (total_width - viewport_width)
            self.canvas.xview_moveto(max(0.0, min(1.0, x_fraction)))
        if total_height <= viewport_height:
            self.canvas.yview_moveto(0.0)
        else:
            y_fraction = (desired_top - top) / (total_height - viewport_height)
            self.canvas.yview_moveto(max(0.0, min(1.0, y_fraction)))

    def _zoom_canvas_at(self, screen_x: float, screen_y: float, steps: int) -> None:
        if not self.canvas:
            raise AssertionError("Canvas zoom requested before canvas initialization.")
        if steps == 0:
            return
        scene_x, scene_y = self._scene_point(screen_x, screen_y)
        old_zoom = self._canvas_zoom_factor()
        new_zoom = max(0.4, min(2.5, old_zoom * (1.1 ** steps)))
        if abs(new_zoom - old_zoom) < 1e-6:
            return
        self.canvas_zoom = new_zoom
        self._redraw_canvas()
        self._restore_canvas_anchor(scene_x, scene_y, screen_x, screen_y)
        self._redraw_canvas()

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
            end_x = self.canvas.canvasx(event.x)
            end_y = self.canvas.canvasy(event.y)
            line_id = self.canvas.create_line(
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
        end_x = self.canvas.canvasx(x)
        end_y = self.canvas.canvasy(y)
        line_id = self.connection_drag_state.get("line_id")
        if line_id is not None:
            try:
                self.canvas.coords(line_id, start_x, start_y, end_x, end_y)
                return
            except tk.TclError:
                pass
        line_id = self.canvas.create_line(
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
        self.canvas.tag_raise("connection_preview")

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

    def _draw_container_reuse_links(self, canvas: tk.Canvas) -> None:
        chains: dict[str, list[ContainerItem]] = {}
        for container in self.project.containers:
            chain_id = container.reuse_chain_id.strip()
            if not chain_id:
                continue
            if not self._is_node_visible_in_current_view("container", container.name):
                continue
            chains.setdefault(chain_id, []).append(container)
        for chain in chains.values():
            if len(chain) < 2:
                continue
            chain.sort(key=lambda item: item.reuse_chain_index)
            for prev, curr in zip(chain, chain[1:]):
                x0, y0 = self._container_center(prev)
                x1, y1 = self._container_center(curr)
                canvas.create_line(
                    x0,
                    y0,
                    x1,
                    y1,
                    fill=COLORS["edge"],
                    width=2,
                    dash=(8, 5),
                    tags=("reuse_connection",),
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
        self._draw_container_reuse_links(canvas)
        self._draw_partial_shared_links(canvas)
        self.canvas_container_group_nodes.clear()
        self.canvas_nodes.clear()
        self.canvas_item_to_name.clear()
        self.canvas_port_positions.clear()
        self.canvas_connection_item_to_index.clear()
        for group in sorted(self.project.container_groups, key=lambda item: item.width * item.height, reverse=True):
            if not self._is_node_visible_in_current_view("containerelement", group.name):
                continue
            item_id = self._draw_container_group_node(canvas, group)
            self.canvas_container_group_nodes[group.name] = item_id
            self.canvas_item_to_name[item_id] = group.name
        for container in self.project.containers:
            if not self._is_node_visible_in_current_view("container", container.name):
                continue
            item_id = self._draw_container_node(canvas, container)
            self.canvas_nodes[container.name] = item_id
            self.canvas_item_to_name[item_id] = container.name
        for item in self.project.res_nodes:
            if not self._is_node_visible_in_current_view("resnode", item.name):
                continue
            item_id = self._draw_res_node(canvas, item)
            self.canvas_nodes[item.name] = item_id
            self.canvas_item_to_name[item_id] = item.name
        for item in self.project.reflector_items:
            if not self._is_node_visible_in_current_view("reflector", item.name):
                continue
            item_id = self._draw_reflector_node(canvas, item)
            self.canvas_nodes[item.name] = item_id
            self.canvas_item_to_name[item_id] = item.name
        for item in self.project.function_frames:
            if not self._is_node_visible_in_current_view("function", item.name):
                continue
            item_id = self._draw_function_frame_node(canvas, item)
            self.canvas_nodes[item.name] = item_id
            self.canvas_item_to_name[item_id] = item.name
        for item in self.project.function_text_items:
            if not self._is_node_visible_in_current_view("functiontext", item.name):
                continue
            item_id = self._draw_function_text_node(canvas, item)
            self.canvas_nodes[item.name] = item_id
            self.canvas_item_to_name[item_id] = item.name
        for stage in self.project.intervention_stages:
            if not self._is_node_visible_in_current_view("interventioner", stage.name):
                continue
            item_id = self._draw_stage_node(canvas, stage)
            self.canvas_nodes[stage.name] = item_id
            self.canvas_item_to_name[item_id] = stage.name
        self._draw_child_clip_masks(canvas)
        canvas.tag_raise("clip_mask")
        self._draw_group_clip_overlays(canvas)
        canvas.tag_raise("group_clip_overlay")
        self._draw_connections(canvas)
        if self.connection_drag_state:
            self._draw_connection_drag_preview(canvas)
        zoom = self._canvas_zoom_factor()
        if abs(zoom - 1.0) >= 1e-6:
            canvas.scale("all", 0.0, 0.0, zoom, zoom)
            self._scale_canvas_rendering(canvas, zoom)
        canvas.tag_raise("connection")
        canvas.tag_raise("connection_preview")
        self._draw_drag_delete_warning(canvas)
        self._draw_selected_node_highlights(canvas)
        left, top, right, bottom = self._expanded_scrollregion(canvas)
        canvas.configure(scrollregion=(left, top, right, bottom))

    def _draw_selected_node_highlights(self, canvas: tk.Canvas) -> None:
        if not self.selected:
            return
        for entry in self.selected:
            kind = str(entry.get("kind") or "").strip()
            name = str(entry.get("name") or "").strip()
            if not kind or not name:
                continue
            item_id = self._canvas_node_item_id(kind, name)
            if item_id is None:
                continue
            bbox = canvas.bbox(item_id)
            if bbox is None:
                continue
            left, top, right, bottom = bbox
            radius = 16.0
            overlays = [
                (left - 8, top - 8, right + 8, bottom + 8, COLORS["accent"], 3),
                (left - 4, top - 4, right + 4, bottom + 4, COLORS["accent_2"], 2),
            ]
            for x0, y0, x1, y1, color, width in overlays:
                self._create_rounded_rect(
                    canvas,
                    x0,
                    y0,
                    x1,
                    y1,
                    radius,
                    fill="",
                    outline=color,
                    width=width,
                    tags=("selected_highlight",),
                )
        canvas.tag_raise("selected_highlight")

    def _draw_connections(self, canvas: tk.Canvas) -> None:
        for index, connection in enumerate(self.project.connections):
            if not self._is_node_visible_in_current_view(connection.source_kind, connection.source_name):
                continue
            if not self._is_node_visible_in_current_view(connection.target_kind, connection.target_name):
                continue
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
            canvas.tag_raise("connection")

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
        canvas.tag_raise("connection_preview")

    def _draw_container_group_node(self, canvas: tk.Canvas, group: ContainerGroupItem) -> int:
        raw_x = group.x or CANVAS_PADDING + 20
        raw_y = group.y or CANVAS_PADDING + 20
        x, y = self._container_display_origin("containerelement", group.name, raw_x, raw_y)
        if not self._is_node_expanded(group):
            width, _height = self._container_group_render_size(group)
            return self._draw_collapsed_port_node(
                canvas,
                "containerelement",
                group.name,
                "container" if group.name == "container" else group.name,
                [
                    f"v: {len(group.variables)}",
                    f"a: {len(group.arrays)}",
                    f"g: {len(group.groups)}",
                ],
                ["in"],
                ["out"],
                COLORS["good"],
                self.selected_container_group_name == group.name,
                x,
                y,
                width,
            )
        width, height = self._container_group_render_size(group)
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
            text="container" if group.name == "container" else group.name,
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
        zoom = self._canvas_zoom_factor()
        viewport_left = canvas.canvasx(0) / zoom
        viewport_top = canvas.canvasy(0) / zoom
        viewport_right = canvas.canvasx(width) / zoom
        viewport_bottom = canvas.canvasy(height) / zoom
        viewport_width = max(viewport_right - viewport_left, 1.0)
        viewport_height = max(viewport_bottom - viewport_top, 1.0)
        margin = max(viewport_width, viewport_height, 2400.0)
        grid_left = math.floor((viewport_left - margin) / step) * step
        grid_top = math.floor((viewport_top - margin) / step) * step
        grid_right = math.ceil((viewport_right + margin) / step) * step
        grid_bottom = math.ceil((viewport_bottom + margin) / step) * step
        for x in range(int(grid_left), int(grid_right) + step, step):
            canvas.create_line(x, grid_top, x, grid_bottom, fill=COLORS["grid"], width=1, tags=("grid",))
        for y in range(int(grid_top), int(grid_bottom) + step, step):
            canvas.create_line(grid_left, y, grid_right, y, fill=COLORS["grid"], width=1, tags=("grid",))

        if self.canvas_view_mode == "decomposer2container_overview":
            divider_x = self._d2c_divider_x()
            canvas.create_rectangle(grid_left, grid_top, divider_x, grid_bottom, fill="#12161f", outline="", tags=("grid_overlay_bg",))
            canvas.create_rectangle(divider_x, grid_top, grid_right, grid_bottom, fill="#141a22", outline="", tags=("grid_overlay_bg",))
            canvas.create_line(divider_x, grid_top, divider_x, grid_bottom, fill=COLORS["accent"], width=2, dash=(10, 10), tags=("grid_overlay_divider",))
            canvas.tag_lower("grid_overlay_bg")
            right_half_width = max(grid_right - divider_x, 1.0)
            canvas.create_text(divider_x * 0.5, viewport_top + 18, anchor="center", fill=COLORS["good"], text="decomposer side", font=("Segoe UI", 10, "bold"), tags=("grid_overlay_label",))
            canvas.create_text(divider_x + right_half_width * 0.5, viewport_top + 18, anchor="center", fill=COLORS["container"], text="container side", font=("Segoe UI", 10, "bold"), tags=("grid_overlay_label",))
            left_hint = "d2cScene"
        elif self.canvas_view_mode == "container_overview":
            left_hint = "containerScene"
        elif self.canvas_view_mode == "decomposer_overview":
            left_hint = "decomposerScene"
        elif self.canvas_view_mode == "reflector_overview":
            left_hint = "reflectorScene"
        elif self.canvas_view_mode == "interventioner_overview":
            left_hint = "interventionerScene"
        elif self.canvas_view_mode == "all_in_one":
            left_hint = "allInOne"
        else:
            left_hint = "algorithmScene"
        canvas.create_text(viewport_left + 24, viewport_top + 18, anchor="w", fill=COLORS["muted"], text=left_hint, font=("Segoe UI", 10), tags=("grid_overlay_label",))
        canvas.create_text(viewport_right - 24, viewport_top + 18, anchor="e", fill=COLORS["muted"], text="right click: delete / duplicate", font=("Segoe UI", 10), tags=("grid_overlay_label",))

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

    def _is_node_expanded(self, item: Any, default: bool = True) -> bool:
        return bool(getattr(item, "expand", default))

    def _draw_collapsed_port_node(
        self,
        canvas: tk.Canvas,
        kind: str,
        name: str,
        title: str,
        summary_lines: list[str],
        input_ports: list[str],
        output_ports: list[str],
        fill: str,
        selected: bool,
        x: float,
        y: float,
        width: float,
    ) -> int:
        visible_lines = [line.strip() for line in summary_lines if str(line).strip()] or ["-"]
        summary_text = "  |  ".join(visible_lines[:3])
        width = max(200.0, min(float(width), 280.0))
        header_height = 32.0
        left_w = 62.0 if input_ports else 18.0
        right_w = 76.0 if output_ports else 18.0
        port_rows = max(len(input_ports), len(output_ports), 1)
        height = max(84.0, header_height + 16.0 + port_rows * 22.0 + 18.0)
        outline = COLORS["accent"] if selected else fill
        node_tag = f"node:{kind}:{name}"
        body_fill = "#05070c"
        radius = 10.0

        item_id = self._create_rounded_rect(
            canvas,
            x,
            y,
            x + width,
            y + height,
            radius,
            fill=body_fill,
            outline=outline,
            width=2,
            tags=(node_tag, "node_body"),
        )
        self._draw_top_rounded_header(
            canvas,
            x + 2,
            y + 2,
            x + width - 2,
            y + header_height,
            max(radius - 2.0, 4.0),
            fill=fill,
            tags=(node_tag, "node_header", "draggable"),
        )
        canvas.create_line(
            x + 3,
            y + header_height,
            x + width - 3,
            y + header_height,
            fill="#d5def3",
            width=1,
            tags=(node_tag, "node_header"),
        )
        canvas.create_text(
            x + width / 2,
            y + 8,
            anchor="n",
            fill="#ffffff",
            text=title,
            font=("Segoe UI", 11, "bold"),
            tags=(f"text_of_{item_id}", node_tag, "node_header", "draggable"),
        )

        body_top = y + header_height
        body_bottom = y + height
        center_left = x + left_w
        center_right = x + width - right_w
        center_width = max(center_right - center_left - 20.0, 80.0)
        canvas.create_text(
            center_left + center_width * 0.5 + 10.0,
            body_top + (body_bottom - body_top) * 0.5,
            anchor="center",
            fill="#ffffff",
            text=summary_text,
            font=("Segoe UI", 11),
            width=center_width,
            justify="center",
            tags=(f"text_of_{item_id}", node_tag, "node_body"),
        )

        if input_ports:
            for index, port_name in enumerate(input_ports):
                port_y = body_top + 14 + index * 20
                port_tag = f"port:{kind}:{name}:in:{port_name}"
                canvas.create_oval(
                    x + 8,
                    port_y,
                    x + 20,
                    port_y + 12,
                    fill=fill,
                    outline=fill,
                    tags=(node_tag, port_tag),
                )
                canvas.create_text(
                    x + 24,
                    port_y - 1,
                    anchor="nw",
                    fill="#ffffff",
                    text=port_name,
                    font=("Segoe UI", 9, "bold"),
                    width=max(left_w - 28.0, 24.0),
                    justify="left",
                    tags=(f"text_of_{item_id}", node_tag, port_tag),
                )
                self._register_port(kind, name, "in", port_name, x + 13, port_y + 5)

        if output_ports:
            for index, port_name in enumerate(output_ports):
                port_y = body_top + 14 + index * 20
                port_tag = f"port:{kind}:{name}:out:{port_name}"
                port_center_x = x + width - 13
                canvas.create_oval(
                    port_center_x - 6,
                    port_y,
                    port_center_x + 6,
                    port_y + 12,
                    fill=fill,
                    outline=fill,
                    tags=(node_tag, port_tag),
                )
                canvas.create_text(
                    port_center_x - 9,
                    port_y - 2,
                    anchor="ne",
                    fill="#ffffff",
                    text=port_name,
                    font=("Segoe UI", 9, "bold"),
                    width=max(right_w - 24.0, 24.0),
                    justify="right",
                    tags=(f"text_of_{item_id}", node_tag, port_tag),
                )
                self._register_port(kind, name, "out", port_name, port_center_x, port_y + 5)
        return item_id

    def _draw_container_node(self, canvas: tk.Canvas, container: ContainerItem) -> int:
        raw_x = container.x or CANVAS_PADDING + 40
        raw_y = container.y or CANVAS_PADDING + 40
        x, y = self._container_display_origin("container", container.name, raw_x, raw_y)
        fill = COLORS["container"] if container.kind == "variable" else COLORS["container_array"]
        if not self._is_node_expanded(container, default=False):
            width, _height = self._container_render_size(container)
            return self._draw_collapsed_port_node(
                canvas,
                "container",
                container.name,
                container.name,
                self._container_value_preview(container, 2),
                ["in"],
                ["out"],
                fill,
                self.selected_container_name == container.name,
                x,
                y,
                width,
            )
        header_fill = "#06080d"
        title = container.name
        parent_group_name = self._container_parent_group_name(container.name)
        compact = parent_group_name is not None
        width, height = self._container_render_size(container)
        value_lines = self._container_value_preview(container, 2 if compact else 3)
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
        if not self._is_node_expanded(rule):
            return self._draw_collapsed_port_node(
                canvas,
                "decomposer",
                rule.name,
                f"Decomposer {rule.name}",
                [
                    f"map: {rule.map_kind or 'v2v'}",
                    f"{rule.source or '-'} -> {rule.target or '-'}",
                ],
                inputs,
                outputs,
                COLORS["accent"],
                self.selected_rule_name == rule.name,
                x,
                y,
                float(getattr(rule, "width", BLUEPRINT_NODE_WIDTH)),
            )
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
        split_x = middle_left + middle_width * 0.5

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
            split_x - 1,
            middle_bottom,
            fill=COLORS["canvas"],
            outline=COLORS["grid"],
            width=1,
            tags=(node_tag, "node_body"),
        )
        canvas.create_rectangle(
            split_x + 1,
            middle_top,
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
            split_x,
            middle_top,
            split_x,
            middle_bottom,
            fill=COLORS["grid"],
            width=1,
            tags=(node_tag, "node_body"),
        )

        canvas.create_text(x + 14, body_top + 8, anchor="nw", fill=COLORS["muted"], text="IN", font=("Segoe UI", 10, "bold"), tags=(node_tag,))
        canvas.create_text(x + width - 14, body_top + 8, anchor="ne", fill=COLORS["muted"], text="OUT", font=("Segoe UI", 10, "bold"), tags=(node_tag,))
        canvas.create_text(middle_left + 8, middle_top + 8, anchor="nw", fill=COLORS["descriptor"], text="DESCRIPTOR", font=("Segoe UI", 10, "bold"), tags=(node_tag,))
        canvas.create_text(split_x + 8, middle_top + 8, anchor="nw", fill=COLORS["resource"], text="RESOURCE", font=("Segoe UI", 10, "bold"), tags=(node_tag,))

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
            fill=COLORS["descriptor"],
            text=descriptor_text,
            font=("Segoe UI", 10),
            width=max(split_x - middle_left - 20, 80),
            justify="left",
            tags=(f"text_of_{item_id}", node_tag, "node_body"),
        )
        canvas.create_text(
            split_x + 10,
            middle_top + 30,
            anchor="nw",
            fill=COLORS["resource"],
            text=resource_text,
            font=("Segoe UI", 10),
            width=max(middle_right - split_x - 20, 80),
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
        if not self._is_node_expanded(item):
            return self._draw_collapsed_port_node(
                canvas,
                "reflector",
                item.name,
                f"Reflector {item.name}",
                [
                    f"filter: {item.reflect_fun or 'direct'}",
                    f"out: {', '.join(outputs) or '-'}",
                ],
                inputs,
                outputs,
                COLORS["good"],
                self.selected_reflector_name == item.name,
                x,
                y,
                float(getattr(item, "width", BLUEPRINT_NODE_WIDTH)),
            )
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
        if not self._is_node_expanded(item):
            return self._draw_collapsed_port_node(
                canvas,
                "resnode",
                item.name,
                "meshNode" if item.name == "meshNode" else f"meshNode {item.name}",
                [f"[{item.resource_kind or 'mesh'}]"],
                ["in"],
                self._resource_output_ports(item.resource_kind or "mesh", item.outputs),
                COLORS["agent"],
                self.selected_res_node_name == item.name,
                x,
                y,
                float(getattr(item, "width", BLUEPRINT_NODE_WIDTH)),
            )
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
        inputs = self._split_port_names(item.input_name, "in")
        outputs = self._split_port_names(item.output_name, "out")
        script_lines = [line for line in (item.script or FUNCTION_SCRIPT_PLACEHOLDER).splitlines() if line.strip()]
        if not script_lines:
            script_lines = [FUNCTION_SCRIPT_PLACEHOLDER]
        script_lines = [f"lang: {item.script_language or FUNCTION_SCRIPT_LANGUAGE_DEFAULT}"] + script_lines
        if not self._is_node_expanded(item):
            return self._draw_collapsed_port_node(
                canvas,
                "function",
                item.name,
                "fun" if item.name == "fun" else f"fun {item.name}",
                script_lines[:2],
                inputs,
                outputs,
                COLORS["good"],
                self.selected_function_name == item.name,
                x,
                y,
                max(float(getattr(item, "width", BLUEPRINT_NODE_WIDTH)), 420.0),
            )
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
            max(float(getattr(item, "width", BLUEPRINT_NODE_WIDTH)), 460.0),
            float(getattr(item, "height", BLUEPRINT_NODE_MIN_HEIGHT)),
            COLORS["good"],
            self.selected_function_name == item.name,
        )

    def _draw_function_text_node(self, canvas: tk.Canvas, item: FunctionTextItem) -> int:
        x = item.x or CANVAS_PADDING + 440
        y = item.y or CANVAS_PADDING + 520
        if not self._is_node_expanded(item):
            return self._draw_collapsed_port_node(
                canvas,
                "functiontext",
                item.name,
                item.name,
                [f"fun: {item.function_name or '-'}", self._compact_activity_text(item.text, limit=48) or "-"],
                [],
                [],
                COLORS["accent_2"],
                self.selected_function_text_name == item.name,
                x,
                y,
                float(getattr(item, "width", BLUEPRINT_NODE_WIDTH)),
            )
        width = max(float(getattr(item, "width", BLUEPRINT_NODE_WIDTH)), 320.0)
        height = max(float(getattr(item, "height", BLUEPRINT_NODE_MIN_HEIGHT)), 180.0)
        outline = COLORS["accent"] if self.selected_function_text_name == item.name else COLORS["accent_2"]
        node_tag = f"node:functiontext:{item.name}"
        item_id = canvas.create_rectangle(
            x,
            y,
            x + width,
            y + height,
            fill="#05070b",
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
            fill="#05070b",
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
            fill=COLORS["text"],
            text=function_label,
            font=("Segoe UI", 10, "bold"),
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
        if not self._is_node_expanded(stage):
            return self._draw_collapsed_port_node(
                canvas,
                "interventioner",
                stage.name,
                f"Interventioner {stage.name}",
                [
                    f"kind: {stage.kind}",
                    self._compact_activity_text(stage.shader_vertex or stage.shader_fragment, limit=48) or "shader",
                ],
                inputs,
                outputs,
                COLORS["accent_2"],
                self.selected_stage_name == stage.name,
                x,
                y,
                float(getattr(stage, "width", BLUEPRINT_NODE_WIDTH)),
            )
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
        if kind == "function":
            width = max(width, 460.0)
        input_count = max(len(inputs), 1)
        output_count = max(len(outputs), 1)
        script_count = max(len(script_lines), 1)
        port_rows = max(input_count, output_count)
        height = max(float(height), BLUEPRINT_NODE_MIN_HEIGHT, 106 + max(port_rows, script_count) * 28)
        outline = COLORS["accent"] if selected else fill
        node_tag = f"node:{kind}:{name}"
        body_fill = "#05070c"
        radius = 12.0

        item_id = self._create_rounded_rect(
            canvas,
            x,
            y,
            x + width,
            y + height,
            radius,
            fill=body_fill,
            outline=outline,
            width=2,
            tags=(node_tag, "node_body"),
        )

        header_height = 36
        self._draw_top_rounded_header(
            canvas,
            x + 2,
            y + 2,
            x + width - 2,
            y + header_height,
            max(radius - 2.0, 4.0),
            fill=fill,
            tags=(node_tag, "node_header", "draggable"),
        )
        canvas.create_line(
            x + 3,
            y + header_height,
            x + width - 3,
            y + header_height,
            fill="#d5def3",
            width=1,
            tags=(node_tag, "node_header"),
        )
        canvas.create_text(
            x + 12,
            y + 9,
            anchor="nw",
            fill="#ffffff",
            text=title,
            font=("Segoe UI", 13, "bold"),
            tags=(f"text_of_{item_id}", node_tag, "node_header", "draggable"),
        )

        body_top = y + header_height
        body_bottom = y + height
        left_w = 106
        right_w = 124
        if kind == "function":
            left_w = 94
            right_w = 104
        center_x = x + left_w
        right_x = x + width - right_w

        script_text = "\n".join(script_lines) if script_lines else "-"

        canvas.create_text(
            x + 10,
            body_top + 10,
            anchor="nw",
            fill="#dbe6ff",
            text="IN",
            font=("Segoe UI", 11, "bold"),
            tags=(node_tag,),
        )
        input_ports = inputs or ["in"]
        for index, port_name in enumerate(input_ports):
            port_y = body_top + 38 + index * 26
            port_tag = f"port:{kind}:{name}:in:{port_name}"
            canvas.create_oval(
                x + 10,
                port_y,
                x + 24,
                port_y + 14,
                fill=fill,
                outline=fill,
                tags=(node_tag, port_tag),
            )
            canvas.create_text(
                x + 30,
                port_y - 1,
                anchor="nw",
                fill="#ffffff",
                text=port_name,
                font=("Segoe UI", 11),
                width=left_w - 34,
                justify="left",
                tags=(f"text_of_{item_id}", node_tag, port_tag),
            )
            self._register_port(kind, name, "in", port_name, x + 16, port_y + 6)

        canvas.create_text(
            center_x + 10,
            body_top + 10,
            anchor="nw",
            fill="#dbe6ff",
            text="SCRIPT",
            font=("Segoe UI", 11, "bold"),
            tags=(node_tag,),
        )
        canvas.create_text(
            center_x + 10,
            body_top + 36,
            anchor="nw",
            fill="#ffffff",
            text=script_text,
            font=("Segoe UI", 11),
            width=width - left_w - right_w - 20,
            justify="left",
            tags=(f"text_of_{item_id}", node_tag),
        )

        canvas.create_text(
            x + width - 10,
            body_top + 10,
            anchor="ne",
            fill="#dbe6ff",
            text="OUT",
            font=("Segoe UI", 11, "bold"),
            tags=(node_tag,),
        )
        output_ports = outputs or ["out"]
        for index, output in enumerate(output_ports):
            output_y = body_top + 38 + index * 26
            port_tag = f"port:{kind}:{name}:out:{output}"
            canvas.create_oval(
                right_x + 10,
                output_y,
                right_x + 24,
                output_y + 14,
                fill=fill,
                outline=fill,
                tags=(node_tag, port_tag),
            )
            canvas.create_text(
                right_x + 30,
                output_y - 1,
                anchor="nw",
                fill="#ffffff",
                text=output,
                font=("Segoe UI", 11),
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
