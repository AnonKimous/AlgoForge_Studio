from __future__ import annotations

from typing import Any
import tkinter as tk
from tkinter import ttk

try:
    from .backend import (
        ContainerGroupItem,
        ContainerItem,
        DecomposerRule,
        FunctionFrameItem,
        InterventionStage,
        ReflectorItem,
        ResourceNodeItem,
    )
    from .shared import CANVAS_PADDING, COLORS
except ImportError:
    from backend import (
        ContainerGroupItem,
        ContainerItem,
        DecomposerRule,
        FunctionFrameItem,
        InterventionStage,
        ReflectorItem,
        ResourceNodeItem,
    )
    from shared import CANVAS_PADDING, COLORS


class AlgorithmStudioPaletteMixin:
    def _build_palette_panel(self, parent: ttk.Frame) -> None:
        palette_shell = ttk.Frame(parent, width=320, padding=(0, 0, 12, 0))
        palette_shell.grid(row=0, column=0, sticky="ns")
        palette_shell.grid_propagate(False)
        palette_shell.columnconfigure(0, weight=1)
        palette_shell.rowconfigure(2, weight=1)
        self.palette_shell = palette_shell
        self.root.bind_all("<ButtonRelease-1>", self._finish_palette_drag, add="+")

        selection_shell = ttk.Frame(palette_shell)
        selection_shell.grid(row=0, column=0, sticky="ew", pady=(0, 12))
        selection_shell.columnconfigure(0, weight=1)
        self.selection_frame = selection_shell
        self._bind_palette_wheel(selection_shell)
        self.root.bind_all("<B1-Motion>", self._palette_drag_motion, add="+")
        self.root.bind_all("<ButtonPress-1>", self._interface4agents_handle_global_click, add="+")
        self.root.bind_all("<ButtonPress-3>", self._interface4agents_handle_global_click, add="+")

        selection_header = ttk.Frame(selection_shell)
        selection_header.grid(row=0, column=0, sticky="ew")
        selection_header.columnconfigure(0, weight=1)
        ttk.Label(selection_header, text="Selection").grid(row=0, column=0, sticky="w")
        selection_mode_toggle = ttk.Button(selection_header, text="Operation Stack", command=self._toggle_selection_view_mode)
        selection_mode_toggle.grid(row=0, column=1, sticky="e", padx=(0, 6))
        self.selection_mode_toggle_button = selection_mode_toggle
        operation_stack_auto_read_button = ttk.Button(selection_header, command=self._toggle_operation_stack_auto_read)
        operation_stack_auto_read_button.grid(row=0, column=2, sticky="e", padx=(0, 6))
        self.operation_stack_auto_read_button = operation_stack_auto_read_button
        selection_toggle = ttk.Button(selection_header, text="Hide", width=8, command=self._toggle_selection_panel)
        selection_toggle.grid(row=0, column=3, sticky="e")
        self.selection_toggle_button = selection_toggle
        self._bind_palette_wheel(selection_header)
        self._bind_palette_wheel(selection_mode_toggle)
        self._bind_palette_wheel(operation_stack_auto_read_button)
        self._refresh_operation_stack_auto_read_button()

        selection_body = ttk.Frame(selection_shell, padding=8)
        selection_body.grid(row=1, column=0, sticky="ew")
        selection_body.columnconfigure(0, weight=1)
        selection_body.columnconfigure(1, weight=1)
        selection_body.columnconfigure(2, weight=1)
        self.selection_body_frame = selection_body
        self._bind_palette_wheel(selection_body)

        selection_summary = tk.Text(
            selection_body,
            wrap="word",
            height=4,
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
        self._bind_palette_wheel(selection_summary)

        selection_name_row = ttk.Frame(selection_body)
        selection_name_row.grid(row=1, column=0, columnspan=3, sticky="ew", pady=(8, 0))
        selection_name_row.columnconfigure(1, weight=1)
        ttk.Label(selection_name_row, text="Name").grid(row=0, column=0, sticky="w", padx=(0, 8))
        selection_name_entry = ttk.Entry(selection_name_row, textvariable=self.selection_name_var)
        selection_name_entry.grid(row=0, column=1, sticky="ew")
        selection_name_entry.bind("<Return>", self._handle_selection_name_submit)
        self.selection_name_entry = selection_name_entry
        self._bind_palette_wheel(selection_name_row)
        self._bind_palette_wheel(selection_name_entry)
        self.interface4agents_highlight_targets["renamenode"] = [
            (
                selection_name_entry,
                {
                    "highlightbackground": COLORS["accent"],
                    "highlightcolor": COLORS["accent"],
                    "highlightthickness": 3,
                },
            )
        ]

        selection_buttons = ttk.Frame(selection_body)
        selection_buttons.grid(row=2, column=0, columnspan=3, sticky="ew", pady=(8, 0))
        selection_buttons.columnconfigure(0, weight=1)
        selection_buttons.columnconfigure(1, weight=1)
        ttk.Button(selection_buttons, text="Merge", command=self._merge_current_selection).grid(row=0, column=0, sticky="ew", padx=(0, 6), pady=(0, 6))
        ttk.Button(selection_buttons, text="整理", command=self._arrange_current_selection).grid(row=0, column=1, sticky="ew", padx=(6, 0), pady=(0, 6))
        self._bind_palette_wheel(selection_buttons)
        self.interface4agents_highlight_targets["createcosnode"] = [
            (
                selection_summary,
                {
                    "highlightbackground": COLORS["accent"],
                    "highlightcolor": COLORS["accent"],
                    "highlightthickness": 3,
                },
            )
        ]
        self.interface4agents_highlight_targets["integratechild"] = [
            (
                selection_summary,
                {
                    "highlightbackground": COLORS["accent"],
                    "highlightcolor": COLORS["accent"],
                    "highlightthickness": 3,
                },
            )
        ]

        operation_stack_body = ttk.Frame(selection_shell, padding=8)
        operation_stack_body.grid(row=1, column=0, sticky="ew")
        operation_stack_body.columnconfigure(0, weight=1)
        operation_stack_body.rowconfigure(1, weight=1)
        self.operation_stack_body_frame = operation_stack_body
        self._bind_palette_wheel(operation_stack_body)

        operation_hint = ttk.Label(
            operation_stack_body,
            text="User and agent actions are recorded here. Chat and highlight steps stay out of the stack. Toggle Read Stack to decide whether agent requests automatically include this history.",
            foreground=COLORS["muted"],
            wraplength=280,
        )
        operation_hint.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        self._bind_palette_wheel(operation_hint)

        operation_scroll_frame = ttk.Frame(operation_stack_body)
        operation_scroll_frame.grid(row=1, column=0, sticky="nsew")
        operation_scroll_frame.columnconfigure(0, weight=1)
        operation_scroll_frame.rowconfigure(0, weight=1)

        operation_scrollbar = ttk.Scrollbar(operation_scroll_frame, orient="vertical")
        operation_scrollbar.grid(row=0, column=1, sticky="ns")
        operation_stack_text = tk.Text(
            operation_scroll_frame,
            wrap="word",
            height=12,
            bg=COLORS["canvas"],
            fg=COLORS["text"],
            insertbackground=COLORS["text"],
            relief="flat",
            borderwidth=0,
            highlightthickness=1,
            highlightbackground=COLORS["grid"],
            highlightcolor=COLORS["accent"],
            yscrollcommand=operation_scrollbar.set,
        )
        operation_stack_text.grid(row=0, column=0, sticky="nsew")
        operation_scrollbar.config(command=operation_stack_text.yview)
        operation_stack_text.tag_configure("operation_header", foreground=COLORS["muted"], font=("Segoe UI", 8, "bold"))
        operation_stack_text.tag_configure("operation_user", foreground=COLORS["text"])
        operation_stack_text.tag_configure("operation_agent", foreground=COLORS["accent"])
        operation_stack_text.tag_configure("empty", foreground=COLORS["muted"])
        operation_stack_text.configure(state="disabled")
        self.operation_stack_text = operation_stack_text
        self._bind_palette_wheel(operation_stack_text)

        drag_header = ttk.Frame(palette_shell)
        drag_header.grid(row=1, column=0, sticky="ew", pady=(0, 8))
        drag_header.columnconfigure(0, weight=1)
        ttk.Label(drag_header, text="Drag Palette").grid(row=0, column=0, sticky="w")
        drag_toggle = ttk.Button(drag_header, text="Hide", width=8, command=self._toggle_drag_palette_panel)
        drag_toggle.grid(row=0, column=1, sticky="e")
        self.drag_palette_toggle_button = drag_toggle
        self._bind_palette_wheel(drag_header)

        drag_body = ttk.Frame(palette_shell)
        drag_body.grid(row=2, column=0, sticky="nsew")
        drag_body.columnconfigure(0, weight=1)
        drag_body.rowconfigure(2, weight=1)
        self.drag_palette_body_frame = drag_body
        self._bind_palette_wheel(drag_body)

        hint = ttk.Label(drag_body, text="Drag blueprint nodes into the canvas", foreground=COLORS["muted"])
        hint.grid(row=0, column=0, sticky="w", pady=(0, 8))
        self.drag_palette_hint_label = hint
        self._bind_palette_wheel(hint)

        mode_row = ttk.Frame(drag_body)
        mode_row.grid(row=1, column=0, sticky="ew", pady=(0, 8))
        mode_row.columnconfigure(0, weight=1)
        mode_row.columnconfigure(1, weight=1)
        for column, (mode_key, mode_label) in enumerate((("blueprint", "Blueprint"), ("container_tree", "Container Tree"))):
            button = tk.Label(
                mode_row,
                text=mode_label,
                bg=COLORS["panel_alt"],
                fg=COLORS["muted"],
                padx=12,
                pady=6,
                relief="flat",
                bd=1,
                cursor="hand2",
            )
            button.grid(row=0, column=column, sticky="ew", padx=(0, 6) if column == 0 else (6, 0))
            button.bind("<Button-1>", lambda _event, value=mode_key: self._set_drag_palette_mode(value))
            self._bind_palette_wheel(button)
            self.drag_palette_mode_buttons[mode_key] = button
        self._bind_palette_wheel(mode_row)

        palette_scroll_frame = ttk.Frame(drag_body)
        palette_scroll_frame.grid(row=2, column=0, sticky="nsew")
        palette_scroll_frame.columnconfigure(0, weight=1)
        palette_scroll_frame.rowconfigure(0, weight=1)

        palette_canvas = tk.Canvas(
            palette_scroll_frame,
            bg=COLORS["window"],
            highlightthickness=0,
            width=286,
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

        blueprint_frame = ttk.Frame(inner_frame)
        blueprint_frame.grid(row=0, column=0, sticky="ew")
        blueprint_frame.columnconfigure(0, weight=1)
        self.palette_blueprint_frame = blueprint_frame

        tree_frame = ttk.Frame(inner_frame)
        tree_frame.grid(row=0, column=0, sticky="ew")
        tree_frame.columnconfigure(0, weight=1)
        self.palette_tree_frame = tree_frame

        self._create_palette_group(
            blueprint_frame,
            0,
            "Container",
            [
                ("variable", "v", "Variable", "drag to canvas"),
                ("array", "a", "Array", "drag to canvas"),
                ("micronode", "m", "MicroNode", "drag into v layout"),
            ],
        )
        self._create_palette_group(
            blueprint_frame,
            1,
            "ToolNodes",
            [
                ("function", "ƒ", "Function", "drag to canvas"),
            ],
        )

        self.drag_palette_mode_var.set("blueprint" if self.canvas_view_mode == "container_overview" else "container_tree")
        self._refresh_container_tree_palette()
        self._refresh_operation_stack_panel()
        _sync_palette_scrollregion()
        self._apply_selection_panel_layout()
        self._apply_drag_palette_panel_layout()
        self._apply_drag_palette_mode_layout()

    def _create_palette_group(
        self,
        parent: ttk.Frame,
        row: int,
        title: str,
        items: list[tuple[str, str, str, str]],
        *,
        draggable: bool = True,
    ) -> None:
        group = ttk.LabelFrame(parent, text=title, padding=8)
        group.grid(row=row, column=0, sticky="ew", pady=(0, 12))
        group.columnconfigure(0, weight=1)
        self._bind_palette_wheel(group)

        for index, (kind, icon, label, subtext) in enumerate(items):
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

            sub_label = tk.Label(tile, text=subtext, bg=COLORS["panel_alt"], fg=COLORS["muted"], anchor="w")
            sub_label.grid(row=1, column=1, sticky="ew", pady=(0, 8))
            self.interface4agents_highlight_targets[kind] = [
                (
                    tile,
                    {
                        "highlightbackground": COLORS["accent"],
                        "highlightcolor": COLORS["accent"],
                        "highlightthickness": 3,
                        "relief": "solid",
                        "bd": 2,
                    },
                ),
            ]
            bind_targets = [tile, badge, title_label, sub_label]

            for widget in bind_targets:
                if draggable:
                    widget.bind(
                        "<ButtonPress-1>",
                        lambda event, value=kind, variant=label if kind == "resnode" else None: self._start_palette_drag(value, event, variant),
                    )
                    widget.bind("<B1-Motion>", self._palette_drag_motion)
                    widget.bind("<ButtonRelease-1>", self._finish_palette_drag)
                else:
                    widget.bind("<Button-1>", lambda _event, value=kind: self._handle_palette_action(value))
                self._bind_palette_wheel(widget)

    def _handle_palette_action(self, action: str) -> None:
        normalized = str(action).strip().lower()
        if normalized == "container_overview":
            self._toggle_container_overview()
            return
        if normalized == "decomposer_overview":
            self._toggle_decomposer_overview()
            return
        if normalized == "decomposer2container_overview":
            self._toggle_decomposer2container_overview()
            return
        raise AssertionError(f"Unsupported palette action: {action}")

    def _refresh_container_tree_palette(self) -> None:
        if not self.palette_tree_frame:
            return
        for child in self.palette_tree_frame.winfo_children():
            child.destroy()
        self._ensure_singleton_container_group(self.project)
        self._ensure_singleton_resource_group(self.project)
        self._normalize_container_scene_scopes()
        self._create_palette_group(
            self.palette_tree_frame,
            0,
            "ToolNodes",
            [
                ("function", "f", "Function", "drag to canvas"),
            ],
        )
        self._create_palette_group(
            self.palette_tree_frame,
            1,
            "MeshNode",
            [
                ("resnode", "M", "meshNode", "drag to canvas"),
            ],
        )
        row = 2
        for zone in ("algorithm", "resource"):
            root_group_name = self._sync_zone_root_group_name(zone)
            root_group = self._find_container_group(root_group_name)
            if root_group is None:
                raise AssertionError(f"Missing root containerElement {root_group_name}")
            inventory_scope = self._inventory_scene_scope_for_zone(zone)
            top_level_groups = [
                group.name
                for group in self.project.container_groups
                if not self._is_hidden_root_group_name(group.name)
                and self._sync_zone_for_node("containerelement", group.name) == zone
                and self._container_group_scene_scope(group) == inventory_scope
                and (
                    self._group_parent_group_name(group.name) is None
                    or self._is_hidden_root_group_name(str(self._group_parent_group_name(group.name) or ""))
                )
            ]
            top_level_containers = [
                container.name
                for container in self.project.containers
                if self._sync_zone_for_node("container", container.name) == zone
                and self._container_scene_scope(container) == inventory_scope
                and (
                    self._container_parent_group_name(container.name) is None
                    or self._is_hidden_root_group_name(str(self._container_parent_group_name(container.name) or ""))
                )
            ]
            has_children = bool(top_level_groups or top_level_containers)
            if zone == "resource" and not has_children:
                continue
            section = ttk.LabelFrame(
                self.palette_tree_frame,
                text="Resource Containers" if zone == "resource" else "Algorithm Containers",
                padding=8,
            )
            section.grid(row=row, column=0, sticky="ew", pady=(0, 12))
            section.columnconfigure(0, weight=1, minsize=268)
            self._bind_palette_wheel(section)
            section_row = 0
            for child_group_name in top_level_groups:
                section_row = self._render_container_tree_palette_group(section, child_group_name, section_row, depth=0)
            for chain_names in self._container_tree_palette_chain_groups(top_level_containers):
                section_row = self._render_container_tree_palette_container_chain(section, chain_names, section_row, depth=0)
            if not has_children:
                empty_label = ttk.Label(section, text="No containers yet", foreground=COLORS["muted"])
                empty_label.grid(row=0, column=0, sticky="w")
                self._bind_palette_wheel(empty_label)
            row += 1

    def _container_tree_palette_chain_groups(self, container_names: list[str]) -> list[list[str]]:
        grouped_names: dict[str, list[str]] = {}
        ordered_keys: list[str] = []
        for container_name in container_names:
            container = self._find_container(container_name)
            if container is None:
                raise AssertionError(f"Missing container {container_name}")
            chain_key = self._container_shareptr_key(container)
            if chain_key not in grouped_names:
                grouped_names[chain_key] = []
                ordered_keys.append(chain_key)
            grouped_names[chain_key].append(container_name)
        chain_groups: list[list[str]] = []
        for chain_key in ordered_keys:
            names = grouped_names[chain_key]
            names.sort(key=lambda name: int(getattr(self._find_container(name), "reuse_chain_index", 0)))
            chain_groups.append(names)
        chain_groups.sort(key=self._container_tree_palette_chain_sort_key)
        return chain_groups

    def _container_tree_palette_chain_sort_key(self, chain_names: list[str]) -> tuple[int, str, str]:
        if not chain_names:
            raise AssertionError("Container chain sort key requires at least one node.")
        container = self._find_container(chain_names[0])
        if container is None:
            raise AssertionError(f"Missing container {chain_names[0]}")
        kind_rank = 0 if container.kind == "variable" else 1
        display_name = self._canvas_node_display_name(container, container.name)
        return kind_rank, display_name, container.name

    def _container_tree_palette_chain_key(self, container_name: str) -> str:
        container = self._find_container(container_name)
        if container is None:
            raise AssertionError(f"Missing container {container_name}")
        return self._container_shareptr_key(container)

    def _container_tree_palette_chain_state(self, container_names: list[str]) -> tuple[str, bool, int, int]:
        if not container_names:
            raise AssertionError("Container chain state requires at least one node.")
        chain_key = self._container_tree_palette_chain_key(container_names[0])
        total = len(container_names)
        revealed = bool(self.container_palette_chain_revealed.get(chain_key, False))
        max_visible = min(total, 3)
        visible_count = int(self.container_palette_chain_visible_count.get(chain_key, 1))
        visible_count = max(1, min(visible_count, max_visible))
        if not revealed:
            visible_count = 1
        max_offset = max(total - visible_count, 0)
        offset = int(self.container_palette_chain_offset.get(chain_key, 0))
        offset = max(0, min(offset, max_offset))
        self.container_palette_chain_visible_count[chain_key] = visible_count
        self.container_palette_chain_offset[chain_key] = offset
        return chain_key, revealed, visible_count, offset

    def _toggle_container_tree_palette_chain_revealed(self, container_name: str) -> None:
        chain_names = self._container_reuse_chain_names(container_name)
        chain_key, revealed, visible_count, _offset = self._container_tree_palette_chain_state(chain_names)
        if len(chain_names) <= 1:
            self._quick_view_container_chain(container_name)
            return
        next_revealed = not revealed
        self.container_palette_chain_revealed[chain_key] = next_revealed
        if next_revealed:
            self.container_palette_chain_visible_count[chain_key] = max(2, min(len(chain_names), max(visible_count, 2)))
        else:
            self.container_palette_chain_visible_count[chain_key] = 1
            self.container_palette_chain_offset[chain_key] = 0
        self._refresh_container_tree_palette()

    def _expand_container_tree_palette_chain(self, container_name: str) -> None:
        chain_names = self._container_reuse_chain_names(container_name)
        chain_key, _revealed, visible_count, _offset = self._container_tree_palette_chain_state(chain_names)
        if len(chain_names) <= 1:
            self._quick_view_container_chain(container_name)
            return
        self.container_palette_chain_revealed[chain_key] = True
        self.container_palette_chain_visible_count[chain_key] = min(len(chain_names), max(2, visible_count + 1), 3)
        self._refresh_container_tree_palette()

    def _scroll_container_tree_palette_chain(self, event: tk.Event, container_name: str) -> str | None:
        chain_names = self._container_reuse_chain_names(container_name)
        chain_key, revealed, visible_count, offset = self._container_tree_palette_chain_state(chain_names)
        if len(chain_names) <= visible_count or not revealed:
            return None
        delta = 0
        if getattr(event, "delta", 0):
            delta = -1 if int(event.delta) > 0 else 1
        elif getattr(event, "num", 0) == 4:
            delta = -1
        elif getattr(event, "num", 0) == 5:
            delta = 1
        if delta == 0:
            return None
        max_offset = len(chain_names) - visible_count
        next_offset = max(0, min(offset + delta, max_offset))
        if next_offset == offset:
            return None
        self.container_palette_chain_offset[chain_key] = next_offset
        self._refresh_container_tree_palette()
        return "break"

    def _render_container_tree_palette_group(self, parent: ttk.Frame, group_name: str, row: int, *, depth: int) -> int:
        group = self._find_container_group(group_name)
        if group is None:
            raise AssertionError(f"Missing containerElement {group_name}")
        expanded = bool(self.container_palette_expanded.get(group_name, False))
        tile = tk.Frame(parent, bg=COLORS["panel_alt"], highlightbackground=COLORS["good"], highlightthickness=1)
        tile.grid(row=row, column=0, sticky="ew", pady=(0, 6), padx=(depth * 18, 0))
        tile.columnconfigure(1, weight=1)

        arrow_label = tk.Label(tile, text="v" if expanded else ">", bg=COLORS["panel_alt"], fg=COLORS["good"], width=2, anchor="center")
        arrow_label.grid(row=0, column=0, rowspan=2, padx=8, pady=8)

        title_label = tk.Label(tile, text=group.name, bg=COLORS["panel_alt"], fg=COLORS["text"], anchor="w")
        title_label.grid(row=0, column=1, sticky="ew", pady=(8, 0))

        summary_label = tk.Label(
            tile,
            text=f"{len(group.groups)} group / {len(group.variables)} var / {len(group.arrays)} array",
            bg=COLORS["panel_alt"],
            fg=COLORS["muted"],
            anchor="w",
        )
        summary_label.grid(row=1, column=1, sticky="ew", pady=(0, 8))

        for widget in (tile, arrow_label, title_label, summary_label):
            widget.bind(
                "<ButtonPress-1>",
                lambda event, value=group_name: self._start_palette_drag("containerelement", event, source_kind="containerelement", source_name=value),
            )
            widget.bind("<B1-Motion>", self._palette_drag_motion)
            widget.bind("<ButtonRelease-1>", self._finish_palette_drag)
            widget.bind("<Double-Button-1>", lambda _event, value=group_name: self._toggle_container_tree_palette_group(value))
            self._bind_palette_wheel(widget)

        row += 1
        if not expanded:
            return row
        for child_group_name in group.groups:
            row = self._render_container_tree_palette_group(parent, child_group_name, row, depth=depth + 1)
        child_container_names = list(group.variables) + list(group.arrays)
        for chain_names in self._container_tree_palette_chain_groups(child_container_names):
            row = self._render_container_tree_palette_container_chain(parent, chain_names, row, depth=depth + 1)
        return row

    def _bind_container_tree_palette_container_widgets(self, widgets: tuple[tk.Widget, ...], container_name: str) -> None:
        for widget in widgets:
            widget.bind(
                "<ButtonPress-1>",
                lambda event, value=container_name: self._start_palette_drag("container", event, source_kind="container", source_name=value),
            )
            widget.bind("<B1-Motion>", self._palette_drag_motion)
            widget.bind("<ButtonRelease-1>", self._finish_palette_drag)
            widget.bind("<Double-Button-1>", lambda _event, value=container_name: self._expand_container_tree_palette_chain(value))
            widget.bind("<Button-3>", lambda _event, value=container_name: self._toggle_container_tree_palette_chain_revealed(value))
            widget.bind("<MouseWheel>", lambda event, value=container_name: self._scroll_container_tree_palette_chain(event, value), add="+")
            widget.bind("<Button-4>", lambda event, value=container_name: self._scroll_container_tree_palette_chain(event, value), add="+")
            widget.bind("<Button-5>", lambda event, value=container_name: self._scroll_container_tree_palette_chain(event, value), add="+")
            self._bind_palette_wheel(widget)

    def _render_container_tree_palette_container_tile(
        self,
        parent: tk.Frame,
        container_name: str,
        *,
        show_chain_index: bool,
        layout: str,
        row_index: int = 0,
        column: int = 0,
    ) -> None:
        container = self._find_container(container_name)
        if container is None:
            raise AssertionError(f"Missing container {container_name}")
        accent_color = COLORS["container"] if container.kind == "variable" else COLORS["container_array"]
        display_title = self._container_tree_palette_container_title(container)
        tile = tk.Frame(parent, bg=COLORS["panel_alt"], highlightbackground=accent_color, highlightthickness=1)
        tile.grid(
            row=row_index,
            column=column,
            sticky="nsew" if layout == "horizontal" else "ew",
            padx=(0, 8) if layout == "horizontal" else 0,
            pady=(0, 6) if layout == "vertical" else 0,
        )
        tile.columnconfigure(1, weight=1)

        badge_label = tk.Label(tile, text="v" if container.kind == "variable" else "a", bg=COLORS["panel_alt"], fg=accent_color, width=3, anchor="center")
        badge_label.grid(row=0, column=0, rowspan=2, padx=8, pady=8)
        title_label = tk.Label(
            tile,
            text=display_title,
            bg=COLORS["panel_alt"],
            fg=COLORS["text"],
            anchor="w",
            font=("Segoe UI", 10, "bold"),
        )
        title_label.grid(row=0, column=1, sticky="ew", pady=(7, 0), padx=(0, 8))
        preview_text = ", ".join(self._container_value_preview(container, 2)) or ("variable" if container.kind == "variable" else "array")
        summary_text = preview_text
        summary_label = tk.Label(
            tile,
            text=summary_text,
            bg=COLORS["panel_alt"],
            fg=COLORS["muted"],
            anchor="w",
        )
        summary_label.grid(row=1, column=1, sticky="ew", pady=(0, 8), padx=(0, 8))
        self._bind_container_tree_palette_container_widgets((tile, badge_label, title_label, summary_label), container_name)

    def _container_tree_palette_container_title(self, container: ContainerItem) -> str:
        display_name = self._canvas_node_display_name(container, container.name)
        reuse_index = int(getattr(container, "reuse_chain_index", 0))
        if reuse_index <= 0:
            return display_name
        return f"{display_name}#{reuse_index - 1}"

    def _render_container_tree_palette_container_chain(self, parent: ttk.Frame, container_names: list[str], row: int, *, depth: int) -> int:
        if not container_names:
            raise AssertionError("Container chain row cannot be empty.")
        show_chain_index = len(container_names) > 1
        _chain_key, _revealed, visible_count, offset = self._container_tree_palette_chain_state(container_names)
        first_container = self._find_container(container_names[0])
        if first_container is None:
            raise AssertionError(f"Missing container {container_names[0]}")
        if first_container.kind == "variable" and len(container_names) > 1:
            visible_names = list(container_names)
        else:
            visible_names = container_names[offset:offset + visible_count]
        if not visible_names:
            visible_names = [container_names[0]]
        if first_container.kind == "array":
            row_shell = tk.Frame(parent, bg=COLORS["window"], highlightbackground="#ffffff", highlightthickness=1)
            row_shell.grid(row=row, column=0, sticky="ew", pady=(0, 6), padx=(depth * 18, 0))
            for column in range(3):
                row_shell.columnconfigure(column, weight=1, uniform=f"array_chain_{row}")
            self._bind_palette_wheel(row_shell)
            self._bind_container_tree_palette_container_widgets((row_shell,), container_names[0])
            for column, container_name in enumerate(visible_names):
                self._render_container_tree_palette_container_tile(
                    row_shell,
                    container_name,
                    show_chain_index=show_chain_index,
                    layout="horizontal",
                    row_index=0,
                    column=column,
                )
            return row + 1
        row_shell = tk.Frame(parent, bg=COLORS["window"], highlightbackground="#ffffff", highlightthickness=1)
        row_shell.grid(row=row, column=0, sticky="ew", pady=(0, 6), padx=(depth * 18, 0))
        for column in range(3):
            row_shell.columnconfigure(column, weight=1, uniform=f"variable_chain_{row}")
        self._bind_palette_wheel(row_shell)
        self._bind_container_tree_palette_container_widgets((row_shell,), container_names[0])
        for item_row, container_name in enumerate(visible_names):
            self._render_container_tree_palette_container_tile(
                row_shell,
                container_name,
                show_chain_index=show_chain_index,
                layout="vertical",
                row_index=item_row,
                column=0,
            )
        return row + 1

    def _render_container_tree_palette_container(self, parent: ttk.Frame, container_name: str, row: int, *, depth: int) -> int:
        return self._render_container_tree_palette_container_chain(parent, [container_name], row, depth=depth)

    def _toggle_container_tree_palette_group(self, group_name: str) -> None:
        self.container_palette_expanded[group_name] = not bool(self.container_palette_expanded.get(group_name, False))
        self._refresh_container_tree_palette()

    def _refresh_scene_tabs(self) -> None:
        if self.canvas_view_mode == "container_overview":
            active_tab = "container"
        elif self.canvas_view_mode == "decomposer_overview":
            active_tab = "decomposer"
        elif self.canvas_view_mode == "reflector_overview":
            active_tab = "reflector"
        elif self.canvas_view_mode == "interventioner_overview":
            active_tab = "interventioner"
        elif self.canvas_view_mode == "interventioner_pretick":
            active_tab = "pretick"
        elif self.canvas_view_mode == "interventioner_aftertick":
            active_tab = "aftertick"
        elif self.canvas_view_mode == "interventioner_render":
            active_tab = "render"
        elif self.canvas_view_mode == "decomposer2container_overview":
            active_tab = "decomposer2container"
        elif self.canvas_view_mode == "all_in_one":
            active_tab = "all_in_one"
        else:
            active_tab = "main"
        self.scene_tab_var.set(active_tab)
        ordered_tabs = list(self.scene_tab_buttons.items())
        for _tab_key, widget in ordered_tabs:
            if widget.winfo_manager():
                widget.pack_forget()
        for tab_key, widget in ordered_tabs:
            phase_only = bool(self.scene_tab_phase_only.get(tab_key))
            should_show = (not phase_only) or self._is_interventioner_view_mode()
            if should_show:
                target_row = self.scene_tab_phase_row if phase_only else self.scene_tab_primary_row
                if target_row is None:
                    raise AssertionError(f"Missing scene tab row for {tab_key}")
                widget.pack(in_=target_row, side="left", padx=(0, 6))
            is_active = tab_key == active_tab
            widget.configure(
                bg=COLORS["accent"] if is_active else COLORS["panel_alt"],
                fg=COLORS["window"] if is_active else COLORS["muted"],
                highlightbackground=COLORS["accent"] if is_active else COLORS["grid"],
                highlightcolor=COLORS["accent"] if is_active else COLORS["grid"],
                highlightthickness=1,
            )

    def _canvas_view_mode_scene_label(self, view_mode: str) -> str:
        normalized = self._normalize_canvas_view_mode_name(view_mode)
        if normalized == "container_overview":
            return "containerScene"
        if normalized == "decomposer_overview":
            return "decomposerScene"
        if normalized == "reflector_overview":
            return "reflectorScene"
        if normalized == "interventioner_overview":
            return "interventionerScene"
        if normalized == "interventioner_pretick":
            return "preTick"
        if normalized == "interventioner_aftertick":
            return "afterTick"
        if normalized == "interventioner_render":
            return "renderResult"
        if normalized == "decomposer2container_overview":
            return "d2cScene"
        if normalized == "all_in_one":
            return "allInOne"
        return "algorithmScene"

    def _interface4agents_clear_highlight(self) -> None:
        if self.interface4agents_highlight_after_id is not None:
            self.root.after_cancel(self.interface4agents_highlight_after_id)
            self.interface4agents_highlight_after_id = None
        for widget, config in self.interface4agents_highlight_restore:
            widget.configure(**config)
        self.interface4agents_highlight_restore = []
        if self.canvas:
            self.canvas.delete("interface4agents_flash")
            self.canvas.delete("highlight_demo")

    def _interface4agents_highlight_is_active(self) -> bool:
        if self.interface4agents_highlight_restore:
            return True
        if self.canvas and (self.canvas.find_withtag("interface4agents_flash") or self.canvas.find_withtag("highlight_demo")):
            return True
        return False

    def _interface4agents_widget_or_parent_is_highlighted(self, widget: tk.Widget | None) -> bool:
        current = widget
        highlighted_widgets = {item[0] for item in self.interface4agents_highlight_restore}
        while current is not None:
            if current in highlighted_widgets:
                return True
            current = current.master
        return False

    def _interface4agents_handle_global_click(self, event: tk.Event) -> None:
        if not self._interface4agents_highlight_is_active():
            return
        widget = event.widget if isinstance(event.widget, tk.Widget) else None
        if widget is not None and self._interface4agents_widget_or_parent_is_highlighted(widget):
            return
        if self.canvas and widget is self.canvas and (self.canvas.find_withtag("interface4agents_flash") or self.canvas.find_withtag("highlight_demo")):
            return
        self._interface4agents_clear_highlight()

    def _interface4agents_highlight_target(self, key: str) -> str:
        normalized = str(key).strip().lower()
        targets = self.interface4agents_highlight_targets[normalized]
        self._interface4agents_clear_highlight()
        for widget, config in targets:
            restore_config = {option: widget.cget(option) for option in config}
            self.interface4agents_highlight_restore.append((widget, restore_config))
            widget.configure(**config)
        first_widget = targets[0][0]
        if self.palette_canvas and not normalized.startswith("scene:"):
            self.palette_canvas.update_idletasks()
            inner = self.palette_inner_frame
            if inner is not None:
                canvas_height = float(self.palette_canvas.winfo_height())
                inner_height = float(inner.winfo_height())
                if inner_height > canvas_height:
                    widget_top = float(first_widget.winfo_y())
                    widget_height = float(first_widget.winfo_height())
                    target = max(0.0, min(widget_top - 24.0, inner_height - canvas_height))
                    if widget_top + widget_height > target + canvas_height:
                        target = max(0.0, widget_top + widget_height - canvas_height + 24.0)
                    self.palette_canvas.yview_moveto(target / (inner_height - canvas_height))
        if normalized.startswith("scene:") and self.scene_tabs_canvas:
            self.scene_tabs_canvas.xview_moveto(0.0)
        return normalized

    def _set_canvas_view_mode(self, view_mode: str, *, log_message: str | None = None) -> None:
        normalized = self._normalize_canvas_view_mode_name(view_mode)
        self._ensure_singleton_container_group(self.project)
        self._ensure_singleton_resource_group(self.project)
        if normalized == self.canvas_view_mode:
            self.drag_palette_mode_var.set("blueprint" if normalized == "container_overview" else "container_tree")
            self._apply_drag_palette_mode_layout()
            self._refresh_scene_tabs()
            return
        self._store_canvas_viewport_state(self.canvas_view_mode)
        self._reset_canvas_interaction_states()
        preserve_selected_stage_name = self.selected_stage_name if self._is_interventioner_view_mode(normalized) else None
        self.canvas_view_mode = normalized
        self._restore_canvas_viewport_state(normalized)
        if normalized != "graph":
            self.selection_state = None
            self.selected_container_name = None
            self.selected_rule_name = None
            self.selected_reflector_name = None
            self.selected_res_node_name = None
            self.selected_function_name = None
            self.selected_function_text_name = None
            self.selected_stage_name = None
            self.selected_container_group_name = None
            if preserve_selected_stage_name and self._find_stage(preserve_selected_stage_name):
                self.selected_stage_name = preserve_selected_stage_name
        self.drag_palette_mode_var.set("blueprint" if normalized == "container_overview" else "container_tree")
        self._apply_drag_palette_mode_layout()
        self._refresh_scene_tabs()
        resolved_log_message = log_message or f"Switched to {self._canvas_view_mode_scene_label(normalized)}."
        self._log(resolved_log_message)
        self._refresh_all()

    def _bind_palette_wheel(self, widget: tk.Widget) -> None:
        widget.bind("<MouseWheel>", self._on_palette_mouse_wheel)
        widget.bind("<Button-4>", self._on_palette_mouse_wheel)
        widget.bind("<Button-5>", self._on_palette_mouse_wheel)

    def _toggle_container_overview(self) -> None:
        if self.canvas_view_mode == "container_overview":
            self._set_canvas_view_mode("graph", log_message="Switched to algorithmScene.")
            return
        self._set_canvas_view_mode("container_overview", log_message="Switched to containerScene.")

    def _toggle_decomposer_overview(self) -> None:
        if self.canvas_view_mode == "decomposer_overview":
            self._set_canvas_view_mode("graph", log_message="Switched to algorithmScene.")
            return
        self._set_canvas_view_mode("decomposer_overview", log_message="Switched to decomposerScene.")

    def _toggle_decomposer2container_overview(self) -> None:
        if self.canvas_view_mode == "decomposer2container_overview":
            self._set_canvas_view_mode("graph", log_message="Switched to algorithmScene.")
            return
        self._set_canvas_view_mode("decomposer2container_overview", log_message="Switched to decomposer2containerScene.")

    def _start_palette_drag(
        self,
        kind: str,
        event: tk.Event,
        variant: str | None = None,
        *,
        source_kind: str | None = None,
        source_name: str | None = None,
    ) -> None:
        self.palette_drag_state = {
            "kind": kind,
            "variant": variant,
            "source_kind": source_kind,
            "source_name": source_name,
            "x_root": event.x_root,
            "y_root": event.y_root,
            "active": False,
            "name": "",
            "waste_area": False,
        }

    def _palette_drag_motion(self, event: tk.Event) -> None:
        if not self.palette_drag_state:
            return
        self.palette_drag_state["x_root"] = event.x_root
        self.palette_drag_state["y_root"] = event.y_root
        if not self.canvas:
            return
        left = self.canvas.winfo_rootx()
        top = self.canvas.winfo_rooty()
        right = left + self.canvas.winfo_width()
        bottom = top + self.canvas.winfo_height()
        inside_canvas = left <= event.x_root <= right and top <= event.y_root <= bottom
        if not self.palette_drag_state["active"]:
            self.palette_drag_state["waste_area"] = event.y_root < top
            if not inside_canvas:
                return
            kind = str(self.palette_drag_state["kind"])
            if kind in {"microcontainer", "micronode"}:
                self.palette_drag_state["active"] = True
                self.palette_drag_state["name"] = "__micronode__"
                return
            variant = self.palette_drag_state.get("variant")
            source_kind = self.palette_drag_state.get("source_kind")
            source_name = self.palette_drag_state.get("source_name")
            x, y = self._scene_point(event.x_root - left, event.y_root - top)
            self._push_operation_recording_suppression("palette_drag")
            try:
                self._drop_palette_item(
                    kind,
                    x,
                    y,
                    variant=str(variant) if variant is not None else None,
                    source_kind=str(source_kind) if source_kind is not None else None,
                    source_name=str(source_name) if source_name is not None else None,
                )
            finally:
                self._pop_operation_recording_suppression()
            name = self._palette_drag_selected_name(kind)
            if not name:
                return
            self.palette_drag_state["active"] = True
            self.palette_drag_state["name"] = name
            return
        kind = str(self.palette_drag_state["kind"])
        name = str(self.palette_drag_state["name"])
        if name:
            x, y = self._scene_point(event.x_root - left, event.y_root - top)
            self._push_operation_recording_suppression("palette_drag")
            try:
                self._move_palette_drag_item(kind, name, x, y)
            finally:
                self._pop_operation_recording_suppression()
        self.palette_drag_state["waste_area"] = event.y_root < top

    def _finish_palette_drag(self, event: tk.Event) -> None:
        if not self.palette_drag_state:
            return
        kind = str(self.palette_drag_state["kind"])
        variant = self.palette_drag_state.get("variant")
        x_root = event.x_root
        y_root = event.y_root
        if not self.canvas:
            self.palette_drag_state = None
            return
        left = self.canvas.winfo_rootx()
        top = self.canvas.winfo_rooty()
        right = left + self.canvas.winfo_width()
        bottom = top + self.canvas.winfo_height()
        active = bool(self.palette_drag_state.get("active"))
        name = str(self.palette_drag_state.get("name") or "")
        if kind in {"microcontainer", "micronode"}:
            if left <= x_root <= right and top <= y_root <= bottom:
                x, y = self._scene_point(x_root - left, y_root - top)
                self._push_operation_recording_suppression("palette_drag")
                try:
                    self._drop_palette_item(kind, x, y, variant=str(variant) if variant is not None else None)
                finally:
                    self._pop_operation_recording_suppression()
            self.palette_drag_state = None
            self._refresh_canvas_interaction_state(schedule_manifest_refresh=False)
            return
        if not active and left <= x_root <= right and top <= y_root <= bottom:
            source_kind = self.palette_drag_state.get("source_kind")
            source_name = self.palette_drag_state.get("source_name")
            x, y = self._scene_point(x_root - left, y_root - top)
            self._push_operation_recording_suppression("palette_drag")
            try:
                self._drop_palette_item(
                    kind,
                    x,
                    y,
                    variant=str(variant) if variant is not None else None,
                    source_kind=str(source_kind) if source_kind is not None else None,
                    source_name=str(source_name) if source_name is not None else None,
                )
            finally:
                self._pop_operation_recording_suppression()
            name = self._palette_drag_selected_name(kind)
            if name:
                self.palette_drag_state["active"] = True
                self.palette_drag_state["name"] = name
            else:
                self.palette_drag_state = None
                self._refresh_canvas_interaction_state(schedule_manifest_refresh=False)
                return
            name = str(self.palette_drag_state.get("name") or "")
        if active and name and y_root < top:
            self._push_operation_recording_suppression("palette_drag")
            try:
                self._delete_palette_drag_item(kind, name)
            finally:
                self._pop_operation_recording_suppression()
        elif active and name and left <= x_root <= right and top <= y_root <= bottom:
            if kind in {"container", "containerelement"}:
                self._push_operation_recording_suppression("palette_drag")
                try:
                    self._attach_dragged_node_to_group_if_inside({"kind": kind, "name": name})
                    self._sync_all_container_groups()
                finally:
                    self._pop_operation_recording_suppression()
        self.palette_drag_state = None
        self._refresh_canvas_interaction_state(schedule_manifest_refresh=False)

    def _drop_palette_item(
        self,
        kind: str,
        x: float,
        y: float,
        variant: str | None = None,
        *,
        source_kind: str | None = None,
        source_name: str | None = None,
    ) -> None:
        normalized_source_kind = str(source_kind or "").strip().lower()
        normalized_source_name = str(source_name or "").strip()
        container_related_drag = kind in {"container", "containerelement", "variable", "array", "microcontainer", "micronode"} or normalized_source_kind in {"container", "containerelement"}
        if self.canvas_view_mode == "decomposer2container_overview" and not container_related_drag:
            self._set_canvas_view_mode("graph", log_message="Switched back to algorithmScene for node placement.")
        if self.canvas_view_mode == "container_overview" and kind not in {"variable", "array"} and not container_related_drag:
            self._set_canvas_view_mode("graph", log_message="Switched back to algorithmScene for tool-node placement.")
        if self.canvas_view_mode == "reflector_overview" and kind not in {"variable", "array", "reflector"} and not container_related_drag:
            self._set_canvas_view_mode("graph", log_message="Switched back to algorithmScene for tool-node placement.")
        if self.canvas_view_mode in {"interventioner_pretick", "interventioner_aftertick", "interventioner_render"} and kind in {"interventioner", "stage"}:
            self._set_canvas_view_mode("interventioner_overview", log_message="Switched to interventionerScene for stage placement.")
        if self._is_interventioner_view_mode() and kind not in {"variable", "array", "function", "interventioner", "stage"} and not container_related_drag:
            self._set_canvas_view_mode("graph", log_message="Switched back to algorithmScene for tool-node placement.")
        if kind == "resnode" and self.canvas_view_mode not in {"decomposer_overview", "all_in_one"}:
            self._log("meshNode can only be placed in decomposerScene.")
            return
        layout_target = self._container_layout_drop_target(x, y)
        if layout_target is not None:
            if kind in {"microcontainer", "micronode"}:
                if layout_target.kind != "variable":
                    self._log("MicroNode can only be dropped into an expanded v node.")
                    return
                micro_name = self._preferred_layout_field_name(layout_target, "variable", "micro")
                field_item = self._add_layout_field_to_container(
                    layout_target,
                    "variable",
                    source_name=micro_name,
                    bit_width=8,
                    rule_text=f"from {layout_target.name} to {micro_name} 8",
                )
                self.selected_container_name = layout_target.name
                self.selected_container_group_name = None
                self._refresh_all()
                self._log(f"Added micronode to {layout_target.name}: {field_item.rule_text}.")
                return
            if kind in {"variable", "array"} and not normalized_source_kind and not normalized_source_name:
                field_item = self._add_layout_field_to_container(layout_target, kind)
                self.selected_container_name = layout_target.name
                self.selected_container_group_name = None
                self._refresh_all()
                self._log(f"Added layout rule to {layout_target.name}: {field_item.rule_text}.")
                return
            if normalized_source_kind == "container" and normalized_source_name:
                source_container = self._find_container(normalized_source_name)
                if source_container is None:
                    raise AssertionError(f"Missing source container for layout rule: {normalized_source_name}")
                field_item = self._add_layout_field_to_container(
                    layout_target,
                    source_container.kind,
                    source_name=normalized_source_name,
                )
                self.selected_container_name = layout_target.name
                self.selected_container_group_name = None
                self._refresh_all()
                self._log(f"Added layout rule to {layout_target.name}: {field_item.rule_text}.")
                return
        if kind in {"variable", "array"} and not normalized_source_kind and not normalized_source_name:
            if self._current_container_scene_scope() != self._inventory_scene_scope_for_zone("algorithm"):
                self._log("Variable/Array can only be created in containerScene. Drag an existing container from Container Tree to use it here.")
                return
        if normalized_source_kind or normalized_source_name:
            if normalized_source_kind not in {"container", "containerelement"} or not normalized_source_name:
                raise AssertionError(f"Unsupported tree palette source: {source_kind}:{source_name}")
            reused_existing_node = False
            if normalized_source_kind == "container":
                duplicate_name, reused_existing_node = self._extract_or_duplicate_container_for_drag(normalized_source_name, x=x, y=y)
                duplicate_kind = "container"
                self.selected_container_name = duplicate_name
                self.selected_container_group_name = None
            else:
                duplicate_kind, duplicate_name = self._duplicate_canvas_node(
                    normalized_source_kind,
                    normalized_source_name,
                    x=x,
                    y=y,
                    scene_scope=self._current_container_scene_scope(),
                )
            if duplicate_kind == "containerelement":
                self.selected_container_group_name = duplicate_name
                self.selected_container_name = None
            elif duplicate_kind != "container":
                raise AssertionError(f"Unexpected duplicate kind from tree palette: {duplicate_kind}")
            self.selection_state = None
            self.selected_rule_name = None
            self.selected_reflector_name = None
            self.selected_stage_name = None
            self.selected_res_node_name = None
            self.selected_function_name = None
            self.selected_function_text_name = None
            self._refresh_all()
            if reused_existing_node:
                self._log(f"Extracted {normalized_source_kind} {normalized_source_name} from its parent.")
            else:
                self._log(f"Cloned {normalized_source_kind} {normalized_source_name} into {duplicate_name}.")
            return
        if kind in {"microcontainer", "micronode"}:
            self._log("MicroNode must be dropped into the layout area of an expanded v node.")
            return
        zone = self._primary_sync_zone_for_view()
        if kind == "containerelement":
            name = self._singleton_ui_node_name(kind) or self._next_group_name_for_zone(zone)
            existing_group = self._find_container_group(name)
            if existing_group is not None:
                existing_group.x = x
                existing_group.y = y
                existing_group.scene_scope = self._inventory_scene_scope_for_zone(zone)
                self.selected_container_group_name = name
                self.selected_container_name = None
                self.selected_rule_name = None
                self.selected_reflector_name = None
                self.selected_stage_name = None
                self.selected_res_node_name = None
                self._refresh_all()
                self._log(f"Moved micronode {name}.")
                return
            group = ContainerGroupItem(
                name=name,
                scene_scope=self._inventory_scene_scope_for_zone(zone),
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
            self._log(f"Added micronode {name}.")
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
            name = self._next_container_name_for_zone(kind, zone)
            container = ContainerItem(
                name=name,
                kind=kind,
                scene_scope=self._inventory_scene_scope_for_zone(zone),
                x=x,
                y=y,
            )
            self._ensure_container_shareptr_identity(container)
            self.project.containers.append(container)
            self.selected_container_name = name
            self.selected_reflector_name = None
            self.selected_stage_name = None
            self.selected_res_node_name = None
            self._refresh_all()
            self._log(f"Added container {name}.")
            return
        if kind == "array":
            name = self._next_container_name_for_zone(kind, zone)
            container = ContainerItem(
                name=name,
                kind=kind,
                scene_scope=self._inventory_scene_scope_for_zone(zone),
                stride=12,
                x=x,
                y=y,
            )
            self._ensure_container_shareptr_identity(container)
            self.project.containers.append(container)
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

    def _palette_drag_selected_name(self, kind: str) -> str:
        normalized = str(kind).strip().lower()
        if normalized == "container":
            return str(self.selected_container_name or "")
        if kind == "containerelement":
            return str(self.selected_container_group_name or "")
        if kind == "decomposer":
            return str(self.selected_rule_name or "")
        if kind in {"variable", "array"}:
            return str(self.selected_container_name or "")
        if kind == "reflector":
            return str(self.selected_reflector_name or "")
        if kind in {"interventioner", "stage"}:
            return str(self.selected_stage_name or "")
        if kind == "resnode":
            return str(self.selected_res_node_name or "")
        if kind == "function":
            return str(self.selected_function_name or "")
        if kind == "functiontext":
            return str(self.selected_function_text_name or "")
        return ""

    def _move_palette_drag_item(self, kind: str, name: str, x: float, y: float) -> None:
        if kind == "containerelement":
            item = self._find_container_group(name)
            if item:
                self._move_container_group_and_members(item, float(x) - float(item.x), float(y) - float(item.y))
        elif kind == "decomposer":
            item = self._find_rule(name)
            if item:
                item.x = x
                item.y = y
        elif kind in {"container", "variable", "array"}:
            item = self._find_container(name)
            if item:
                item.x = x
                item.y = y
        elif kind == "reflector":
            item = self._find_reflector(name)
            if item:
                item.x = x
                item.y = y
        elif kind in {"interventioner", "stage"}:
            item = self._find_stage(name)
            if item:
                item.x = x
                item.y = y
        elif kind == "resnode":
            item = self._find_res_node(name)
            if item:
                item.x = x
                item.y = y
        elif kind == "function":
            item = self._find_function_frame(name)
            if item:
                item.x = x
                item.y = y
        elif kind == "functiontext":
            item = self._find_function_text_item(name)
            if item:
                item.x = x
                item.y = y
        self._refresh_canvas_drag_preview()

    def _delete_palette_drag_item(self, kind: str, name: str) -> None:
        if kind == "containerelement":
            if name == "container":
                return
            self.selected_container_group_name = name
            self._delete_selected_container_group()
            return
        if kind == "decomposer":
            self.selected_rule_name = name
            self._delete_selected_rule()
            return
        if kind in {"container", "variable", "array"}:
            self.selected_container_name = name
            self._delete_selected_container()
            return
        if kind == "reflector":
            self.selected_reflector_name = name
            self._delete_selected_reflector()
            return
        if kind in {"interventioner", "stage"}:
            self.selected_stage_name = name
            self._delete_selected_stage()
            return
        if kind == "resnode":
            self.selected_res_node_name = name
            self._delete_selected_res_node()
            return
        if kind == "function":
            self.selected_function_name = name
            self._delete_selected_function()
            return
        if kind == "functiontext":
            self.selected_function_text_name = name
            self._delete_selected_function_text()
            return

    def _resource_output_ports(self, resource_kind: str, outputs: list[str] | None = None) -> list[str]:
        if outputs:
            return list(outputs)
        kind = resource_kind.strip().lower()
        if kind:
            return [kind]
        return ["out"]
