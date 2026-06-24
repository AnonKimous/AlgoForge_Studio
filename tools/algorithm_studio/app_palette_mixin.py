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

        selection_header = ttk.Frame(selection_shell)
        selection_header.grid(row=0, column=0, sticky="ew")
        selection_header.columnconfigure(0, weight=1)
        ttk.Label(selection_header, text="Selection").grid(row=0, column=0, sticky="w")
        selection_toggle = ttk.Button(selection_header, text="Hide", width=8, command=self._toggle_selection_panel)
        selection_toggle.grid(row=0, column=1, sticky="e")
        self.selection_toggle_button = selection_toggle
        self._bind_palette_wheel(selection_header)

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
        self.selection_name_entry = selection_name_entry
        self._bind_palette_wheel(selection_name_row)
        self._bind_palette_wheel(selection_name_entry)

        selection_buttons = ttk.Frame(selection_body)
        selection_buttons.grid(row=2, column=0, columnspan=3, sticky="ew", pady=(8, 0))
        selection_buttons.columnconfigure(0, weight=1)
        selection_buttons.columnconfigure(1, weight=1)
        selection_buttons.columnconfigure(2, weight=1)
        ttk.Button(selection_buttons, text="Copy", command=self._copy_current_selection).grid(row=0, column=0, sticky="ew", padx=(0, 6), pady=(0, 6))
        ttk.Button(selection_buttons, text="Merge", command=self._merge_current_selection).grid(row=0, column=1, sticky="ew", padx=6, pady=(0, 6))
        ttk.Button(selection_buttons, text="整理", command=self._arrange_current_selection).grid(row=0, column=2, sticky="ew", padx=(6, 0), pady=(0, 6))
        ttk.Button(selection_buttons, text="Delete", command=self._delete_current_selection).grid(row=1, column=0, sticky="ew", padx=(0, 6))
        ttk.Button(selection_buttons, text="Paste", command=self._paste_selection_from_clipboard).grid(row=1, column=1, columnspan=2, sticky="ew", padx=(6, 0))
        self._bind_palette_wheel(selection_buttons)
        self.interface4agents_highlight_targets["createcosnode"] = [
            (
                selection_summary,
                {
                    "bg": COLORS["accent"],
                    "fg": COLORS["window"],
                    "highlightbackground": COLORS["accent"],
                    "highlightcolor": COLORS["accent"],
                    "highlightthickness": 2,
                },
            )
        ]
        self.interface4agents_highlight_targets["integratechild"] = [
            (
                selection_summary,
                {
                    "bg": COLORS["accent"],
                    "fg": COLORS["window"],
                    "highlightbackground": COLORS["accent"],
                    "highlightcolor": COLORS["accent"],
                    "highlightthickness": 2,
                },
            )
        ]

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
        drag_body.rowconfigure(1, weight=1)
        self.drag_palette_body_frame = drag_body
        self._bind_palette_wheel(drag_body)

        hint = ttk.Label(drag_body, text="Drag blueprint nodes into the canvas", foreground=COLORS["muted"])
        hint.grid(row=0, column=0, sticky="w", pady=(0, 8))
        self._bind_palette_wheel(hint)

        palette_scroll_frame = ttk.Frame(drag_body)
        palette_scroll_frame.grid(row=1, column=0, sticky="nsew")
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
            0,
            "Container",
            [
                ("variable", "v", "Variable", "drag to canvas"),
                ("array", "a", "Array", "drag to canvas"),
            ],
        )
        self._create_palette_group(
            inner_frame,
            1,
            "ToolNodes",
            [
                ("reflector", "R", "Reflector", "drag to canvas"),
                ("function", "ƒ", "Function", "drag to canvas"),
                ("interventioner", "I", "Interventioner", "drag to canvas"),
            ],
        )

        self._create_palette_group(
            inner_frame,
            2,
            "MeshNode",
            [
                ("resnode", "M", "meshNode", "drag to canvas"),
            ],
        )
        _sync_palette_scrollregion()
        self._apply_selection_panel_layout()
        self._apply_drag_palette_panel_layout()

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
                        "bg": COLORS["accent"],
                        "highlightbackground": COLORS["accent"],
                        "highlightcolor": COLORS["accent"],
                        "highlightthickness": 2,
                    },
                ),
                (badge, {"bg": COLORS["accent"], "fg": COLORS["window"]}),
                (title_label, {"bg": COLORS["accent"], "fg": COLORS["window"]}),
                (sub_label, {"bg": COLORS["accent"], "fg": COLORS["window"]}),
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
            should_show = (not phase_only) or (self._is_interventioner_view_mode() and bool(self.selected_stage_name))
            if should_show:
                widget.pack(side="left", padx=(0, 6))
            is_active = tab_key == active_tab
            widget.configure(
                bg=COLORS["accent"] if is_active else COLORS["panel_alt"],
                fg=COLORS["window"] if is_active else COLORS["muted"],
                highlightbackground=COLORS["accent"] if is_active else COLORS["grid"],
                highlightcolor=COLORS["accent"] if is_active else COLORS["grid"],
                highlightthickness=1,
            )

    def _interface4agents_clear_highlight(self) -> None:
        if self.interface4agents_highlight_after_id is not None:
            self.root.after_cancel(self.interface4agents_highlight_after_id)
            self.interface4agents_highlight_after_id = None
        for widget, config in self.interface4agents_highlight_restore:
            widget.configure(**config)
        self.interface4agents_highlight_restore = []

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
        self.interface4agents_highlight_after_id = self.root.after(900, self._interface4agents_clear_highlight)
        return normalized

    def _set_canvas_view_mode(self, view_mode: str, *, log_message: str | None = None) -> None:
        normalized = str(view_mode or "").strip().lower()
        if normalized not in {
            "graph",
            "container_overview",
            "decomposer_overview",
            "reflector_overview",
            "interventioner_overview",
            "interventioner_pretick",
            "interventioner_aftertick",
            "interventioner_render",
            "decomposer2container_overview",
            "all_in_one",
        }:
            raise AssertionError(f"Unsupported canvas view mode: {view_mode}")
        self._ensure_singleton_container_group(self.project)
        self._ensure_singleton_resource_group(self.project)
        if normalized == self.canvas_view_mode:
            self._refresh_scene_tabs()
            return
        self._reset_canvas_interaction_states()
        preserve_selected_stage_name = self.selected_stage_name if self._is_interventioner_view_mode(normalized) else None
        self.canvas_view_mode = normalized
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
        if normalized == "decomposer_overview" and self._find_rule("decomposer") is None:
            self.project.decomposer_rules.append(DecomposerRule(name="decomposer", source="", target="", x=CANVAS_PADDING + 220.0, y=CANVAS_PADDING + 80.0))
        self._refresh_scene_tabs()
        if log_message:
            self._log(log_message)
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

    def _start_palette_drag(self, kind: str, event: tk.Event, variant: str | None = None) -> None:
        self.palette_drag_state = {
            "kind": kind,
            "variant": variant,
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
            variant = self.palette_drag_state.get("variant")
            x = self.canvas.canvasx(event.x_root - left) / self._canvas_zoom_factor()
            y = self.canvas.canvasy(event.y_root - top) / self._canvas_zoom_factor()
            self._drop_palette_item(kind, x, y, variant=str(variant) if variant is not None else None)
            name = self._palette_drag_selected_name(kind)
            if not name:
                return
            self.palette_drag_state["active"] = True
            self.palette_drag_state["name"] = name
            return
        kind = str(self.palette_drag_state["kind"])
        name = str(self.palette_drag_state["name"])
        if name:
            x = self.canvas.canvasx(event.x_root - left) / self._canvas_zoom_factor()
            y = self.canvas.canvasy(event.y_root - top) / self._canvas_zoom_factor()
            self._move_palette_drag_item(kind, name, x, y)
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
        if not active and left <= x_root <= right and top <= y_root <= bottom:
            x = self.canvas.canvasx(x_root - left) / self._canvas_zoom_factor()
            y = self.canvas.canvasy(y_root - top) / self._canvas_zoom_factor()
            self._drop_palette_item(kind, x, y, variant=str(variant) if variant is not None else None)
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
            self._delete_palette_drag_item(kind, name)
        self.palette_drag_state = None
        self._refresh_canvas_interaction_state(schedule_manifest_refresh=False)

    def _drop_palette_item(self, kind: str, x: float, y: float, variant: str | None = None) -> None:
        if self.canvas_view_mode == "decomposer2container_overview":
            self._set_canvas_view_mode("graph", log_message="Switched back to algorithmScene for node placement.")
        if self.canvas_view_mode == "container_overview" and kind not in {"variable", "array"}:
            self._set_canvas_view_mode("graph", log_message="Switched back to algorithmScene for tool-node placement.")
        if self.canvas_view_mode == "reflector_overview" and kind not in {"variable", "array", "reflector"}:
            self._set_canvas_view_mode("graph", log_message="Switched back to algorithmScene for tool-node placement.")
        if self._is_interventioner_view_mode() and kind not in {"variable", "array", "function", "interventioner", "stage"}:
            self._set_canvas_view_mode("graph", log_message="Switched back to algorithmScene for tool-node placement.")
        if kind == "resnode" and self.canvas_view_mode not in {"decomposer_overview", "all_in_one"}:
            self._log("meshNode can only be placed in decomposerScene.")
            return
        zone = self._primary_sync_zone_for_view()
        if kind == "containerelement":
            name = self._singleton_ui_node_name(kind) or self._next_group_name_for_zone(zone)
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
            name = self._next_container_name_for_zone(kind, zone)
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
            name = self._next_container_name_for_zone(kind, zone)
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

    def _palette_drag_selected_name(self, kind: str) -> str:
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
                item.x = x
                item.y = y
        elif kind == "decomposer":
            item = self._find_rule(name)
            if item:
                item.x = x
                item.y = y
        elif kind in {"variable", "array"}:
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
        self._refresh_canvas_interaction_state(schedule_manifest_refresh=False)

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
        if kind in {"variable", "array"}:
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
