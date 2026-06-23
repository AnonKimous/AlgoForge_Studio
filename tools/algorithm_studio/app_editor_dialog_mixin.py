from __future__ import annotations

import tkinter as tk
from tkinter import messagebox, ttk

try:
    from .backend import ContainerItem, FunctionFrameItem, FunctionTextItem
    from .shared import COLORS
except ImportError:
    from backend import ContainerItem, FunctionFrameItem, FunctionTextItem
    from shared import COLORS


class AlgorithmStudioEditorDialogMixin:
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

    def _function_connection_snapshot(self, kind: str, name: str, port: str) -> str:
        node = self._node_by_kind_name(kind, name)
        if node is None:
            raise AssertionError(f"Missing connected node {kind}:{name}")
        lines = [
            f"node: {kind}:{name}",
            f"port: {port or '-'}",
        ]
        if kind == "container":
            assert isinstance(node, ContainerItem)
            lines.extend(
                [
                    f"kind: {node.kind}",
                    f"count: {node.count}",
                    f"stride: {node.stride}",
                    f"structure: {self._container_structure_signature(node)}",
                ]
            )
            if node.kind == "variable":
                lines.append(f"value: {node.value.strip() or '-'}")
            else:
                preview = self._container_value_preview(node, limit=8)
                lines.append("values:")
                lines.extend(preview)
            return "\n".join(lines)
        if kind == "containerelement":
            lines.append("content:")
            lines.extend(self._container_group_detail_text(name).splitlines())
            return "\n".join(lines)
        if kind == "function":
            assert isinstance(node, FunctionFrameItem)
            lines.extend(
                [
                    f"input ports: {node.input_name or 'in'}",
                    f"output ports: {node.output_name or 'out'}",
                    f"script language: {getattr(node, 'script_language', 'pseudocode') or 'pseudocode'}",
                    "expected input:",
                    node.expected_input.strip() or "-",
                    "expected output:",
                    node.expected_output.strip() or "-",
                    "script:",
                    node.script.strip() or "-",
                ]
            )
            return "\n".join(lines)
        if kind == "functiontext":
            lines.extend(
                [
                    f"function: {node.function_name or '-'}",
                    "text:",
                    node.text.strip() or "-",
                ]
            )
            return "\n".join(lines)
        if kind in {"interventioner", "stage"}:
            lines.extend(
                [
                    f"stage kind: {node.kind or '-'}",
                    f"functions: {', '.join(node.functions) or '-'}",
                    f"used variables: {', '.join(node.used_variables) or '-'}",
                    f"used arrays: {', '.join(node.used_arrays) or '-'}",
                    f"shader.vertex: {node.shader_vertex or '-'}",
                    f"shader.fragment: {node.shader_fragment or '-'}",
                ]
            )
            return "\n".join(lines)
        if kind == "decomposer":
            lines.extend(
                [
                    f"source: {node.source or '-'}",
                    f"target: {node.target or '-'}",
                    f"map kind: {node.map_kind or '-'}",
                    f"descriptor: {node.descriptor_script or '-'}",
                    f"resource mode: {node.resource_mode or '-'}",
                    f"resource script: {node.resource_script or '-'}",
                ]
            )
            return "\n".join(lines)
        if kind == "reflector":
            lines.extend(
                [
                    f"filter: {node.reflect_fun or '-'}",
                    f"direct from: {', '.join(node.direct_from) or '-'}",
                    f"direct to: {', '.join(node.direct_to) or '-'}",
                ]
            )
            return "\n".join(lines)
        if kind == "resnode":
            lines.extend(
                [
                    f"resource kind: {node.resource_kind or '-'}",
                    f"outputs: {', '.join(node.outputs) or '-'}",
                ]
            )
            return "\n".join(lines)
        raise AssertionError(f"Unsupported connected node kind: {kind}")

    def _load_function_linked_text(self, function_name: str, direction: str) -> str:
        if direction not in {"input", "output"}:
            raise AssertionError(f"Unsupported function link direction: {direction}")
        blocks: list[str] = []
        if direction == "input":
            connections = [
                connection
                for connection in self.project.connections
                if connection.target_kind == "function" and connection.target_name == function_name
            ]
            connections.sort(key=lambda connection: (connection.target_port, connection.source_kind, connection.source_name, connection.source_port))
            for connection in connections:
                header = f"[{connection.target_port or 'in'}] <- {connection.source_kind}:{connection.source_name}.{connection.source_port or 'out'}"
                blocks.append(header + "\n" + self._function_connection_snapshot(connection.source_kind, connection.source_name, connection.source_port))
        else:
            connections = [
                connection
                for connection in self.project.connections
                if connection.source_kind == "function" and connection.source_name == function_name
            ]
            connections.sort(key=lambda connection: (connection.source_port, connection.target_kind, connection.target_name, connection.target_port))
            for connection in connections:
                header = f"[{connection.source_port or 'out'}] -> {connection.target_kind}:{connection.target_name}.{connection.target_port or 'in'}"
                blocks.append(header + "\n" + self._function_connection_snapshot(connection.target_kind, connection.target_name, connection.target_port))
        if not blocks:
            raise RuntimeError(f"No linked {direction} nodes found for {function_name}.")
        title = "Linked Inputs" if direction == "input" else "Linked Outputs"
        return f"{title} for {function_name}\n\n" + "\n\n".join(blocks)

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
        dialog.geometry("960x720")
        dialog.minsize(860, 620)

        body = ttk.Frame(dialog, padding=12)
        body.grid(row=0, column=0, sticky="nsew")
        dialog.columnconfigure(0, weight=1)
        dialog.rowconfigure(0, weight=1)
        body.columnconfigure(0, weight=1)
        body.columnconfigure(1, weight=1)
        body.columnconfigure(2, weight=1)
        body.rowconfigure(3, weight=1)

        title_row = ttk.Frame(body)
        title_row.grid(row=0, column=0, columnspan=3, sticky="ew")
        title_row.columnconfigure(1, weight=1)
        title_row.columnconfigure(3, weight=1)
        title_row.columnconfigure(5, weight=1)
        title_row.columnconfigure(7, weight=1)
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
        output_name_entry.grid(row=0, column=5, sticky="ew", padx=(6, 18))
        output_name_entry.insert(0, item.output_name or "out")
        ttk.Label(title_row, text="Script Language").grid(row=0, column=6, sticky="w")
        script_language_entry = ttk.Combobox(
            title_row,
            values=["pseudocode", "natural-language", "cpp", "python", "java", "glsl", "custom"],
            state="normal",
        )
        script_language_entry.grid(row=0, column=7, sticky="ew", padx=(6, 0))
        script_language_entry.insert(0, getattr(item, "script_language", "pseudocode") or "pseudocode")

        helper_label = ttk.Label(
            body,
            text=(
                "Use any script style here: pseudocode, natural language, C++, Python, Java, GLSL, or your own format. "
                "The assistant only helps make the script logically consistent. Build-time translation happens later."
            ),
            foreground=COLORS["muted"],
            wraplength=900,
            justify="left",
        )
        helper_label.grid(row=1, column=0, columnspan=3, sticky="ew", pady=(10, 0))

        expected_input_frame = ttk.LabelFrame(body, text="Expected Input", padding=8)
        expected_input_frame.grid(row=2, column=0, sticky="nsew", pady=(12, 0), padx=(0, 6))
        expected_input_frame.columnconfigure(0, weight=1)
        expected_input_frame.rowconfigure(1, weight=1)
        expected_input_toolbar = ttk.Frame(expected_input_frame)
        expected_input_toolbar.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 8))
        expected_input_toolbar.columnconfigure(0, weight=1)
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
        expected_input_text.grid(row=1, column=0, sticky="nsew")
        expected_input_text.insert("1.0", item.expected_input)
        expected_input_scroll = ttk.Scrollbar(expected_input_frame, orient="vertical", command=expected_input_text.yview)
        expected_input_scroll.grid(row=1, column=1, sticky="ns")
        expected_input_text.configure(yscrollcommand=expected_input_scroll.set)

        expected_output_frame = ttk.LabelFrame(body, text="Expected Output", padding=8)
        expected_output_frame.grid(row=2, column=1, sticky="nsew", pady=(12, 0), padx=6)
        expected_output_frame.columnconfigure(0, weight=1)
        expected_output_frame.rowconfigure(1, weight=1)
        expected_output_toolbar = ttk.Frame(expected_output_frame)
        expected_output_toolbar.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 8))
        expected_output_toolbar.columnconfigure(0, weight=1)
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
        expected_output_text.grid(row=1, column=0, sticky="nsew")
        expected_output_text.insert("1.0", item.expected_output)
        expected_output_scroll = ttk.Scrollbar(expected_output_frame, orient="vertical", command=expected_output_text.yview)
        expected_output_scroll.grid(row=1, column=1, sticky="ns")
        expected_output_text.configure(yscrollcommand=expected_output_scroll.set)

        notes_frame = ttk.LabelFrame(body, text="Linked Notes", padding=8)
        notes_frame.grid(row=2, column=2, sticky="nsew", pady=(12, 0), padx=(6, 0))
        notes_frame.columnconfigure(0, weight=1)
        notes_frame.rowconfigure(0, weight=1)
        notes_text = tk.Text(
            notes_frame,
            wrap="word",
            height=10,
            bg=COLORS["panel_alt"],
            fg=COLORS["text"],
            insertbackground=COLORS["text"],
            relief="flat",
            borderwidth=0,
            highlightthickness=1,
            highlightbackground=COLORS["grid"],
            highlightcolor=COLORS["accent"],
        )
        notes_text.grid(row=0, column=0, sticky="nsew")
        linked_text_item = self._find_function_text_for_function(item.name)
        if linked_text_item is not None:
            notes_text.insert("1.0", linked_text_item.text)
        notes_scroll = ttk.Scrollbar(notes_frame, orient="vertical", command=notes_text.yview)
        notes_scroll.grid(row=0, column=1, sticky="ns")
        notes_text.configure(yscrollcommand=notes_scroll.set)

        script_frame = ttk.LabelFrame(body, text="Script / Pseudocode", padding=8)
        script_frame.grid(row=3, column=0, columnspan=3, sticky="nsew", pady=(12, 0))
        script_frame.columnconfigure(0, weight=1)
        script_frame.rowconfigure(0, weight=1)
        script_text = tk.Text(
            script_frame,
            wrap="none",
            height=16,
            bg=COLORS["panel_alt"],
            fg=COLORS["text"],
            insertbackground=COLORS["text"],
            relief="flat",
            borderwidth=0,
            highlightthickness=1,
            highlightbackground=COLORS["grid"],
            highlightcolor=COLORS["accent"],
            font=("Consolas", 10),
            undo=False,
        )
        script_text.grid(row=0, column=0, sticky="nsew")
        script_text.insert("1.0", item.script)
        script_scroll_y = ttk.Scrollbar(script_frame, orient="vertical", command=script_text.yview)
        script_scroll_y.grid(row=0, column=1, sticky="ns")
        script_scroll_x = ttk.Scrollbar(script_frame, orient="horizontal", command=script_text.xview)
        script_scroll_x.grid(row=1, column=0, sticky="ew")
        script_text.configure(yscrollcommand=script_scroll_y.set, xscrollcommand=script_scroll_x.set)

        button_row = ttk.Frame(body)
        button_row.grid(row=4, column=0, columnspan=3, sticky="ew", pady=(12, 0))
        button_row.columnconfigure(0, weight=1)
        button_row.columnconfigure(1, weight=1)
        button_row.columnconfigure(2, weight=1)

        def load_expected_input() -> None:
            try:
                loaded_text = self._load_function_linked_text(item.name, "input")
            except Exception as exc:  # noqa: BLE001
                messagebox.showerror("Load linked inputs error", str(exc))
                return
            expected_input_text.delete("1.0", tk.END)
            expected_input_text.insert("1.0", loaded_text)

        def load_expected_output() -> None:
            try:
                loaded_text = self._load_function_linked_text(item.name, "output")
            except Exception as exc:  # noqa: BLE001
                messagebox.showerror("Load linked outputs error", str(exc))
                return
            expected_output_text.delete("1.0", tk.END)
            expected_output_text.insert("1.0", loaded_text)

        ttk.Button(expected_input_toolbar, text="Load Linked Inputs", command=load_expected_input).grid(row=0, column=1, sticky="e")
        ttk.Button(expected_output_toolbar, text="Load Linked Outputs", command=load_expected_output).grid(row=0, column=1, sticky="e")

        def apply_fields() -> None:
            new_name = name_entry.get().strip()
            if not new_name:
                raise RuntimeError("Function name is required.")
            self._rename_function_frame(item, new_name)
            dialog.title(f"fun {item.name}")
            item.input_name = input_name_entry.get().strip() or "in"
            item.output_name = output_name_entry.get().strip() or "out"
            item.script_language = script_language_entry.get().strip() or "pseudocode"
            item.expected_input = expected_input_text.get("1.0", tk.END).strip()
            item.expected_output = expected_output_text.get("1.0", tk.END).strip()
            item.script = script_text.get("1.0", tk.END).strip()

            notes_value = notes_text.get("1.0", tk.END).strip()
            text_item = self._find_function_text_for_function(item.name)
            if notes_value:
                if text_item is None:
                    text_item = self._ensure_function_text_item(item.name)
                text_item.text = notes_value
            elif text_item is not None:
                text_item.text = ""

        def save() -> None:
            try:
                apply_fields()
            except Exception as exc:  # noqa: BLE001
                messagebox.showerror("Function save error", str(exc))
                return
            self._refresh_all()
            dialog.destroy()

        def request() -> None:
            self._sync_agent_client_settings()
            try:
                apply_fields()
            except Exception as exc:  # noqa: BLE001
                messagebox.showerror("Function request error", str(exc))
                return
            current_script = script_text.get("1.0", tk.END).strip()
            current_text_item = self._find_function_text_for_function(item.name)
            current_solution_text = "" if current_text_item is None else current_text_item.text.strip()
            prompt = "\n".join(
                [
                    "You are helping complete a single fun node inside Algorithm Studio.",
                    "Return plain text only.",
                    "Do not emit interface4agents blocks.",
                    "Do not return build-ready C++ or shader code unless the selected script language explicitly asks for it.",
                    "Keep the response in the selected script language or style.",
                    "",
                    f"Function name: {item.name}",
                    f"Script language: {item.script_language}",
                    f"Input port: {item.input_name}",
                    f"Output port: {item.output_name}",
                    "",
                    "Expected input:",
                    item.expected_input or "-",
                    "",
                    "Expected output:",
                    item.expected_output or "-",
                    "",
                    "Current function script:",
                    current_script or "-",
                    "",
                    "Linked notes:",
                    current_solution_text or "-",
                    "",
                    self._agent_document_context(),
                    "",
                    "Task:",
                    (
                        "Rewrite or continue the function script so it is logically consistent with the described inputs and outputs. "
                        "The script may be pseudocode, natural language, or any programming language chosen above. "
                        "Return only the final script body that should be saved into this fun node."
                    ),
                ]
            ).strip()
            selection = f"function:{item.name}"
            try:
                approved = self._authorize_chat_request(selection, prompt)
            except Exception as exc:  # noqa: BLE001
                message = self._compact_activity_text(str(exc), limit=120) or "Function approval failed."
                self._finish_execution_trace("Function Assistant", False, f"Approval error: {message}")
                self._append_chat_message("error", message)
                messagebox.showerror("Approval error", message)
                return
            if not approved:
                self._finish_execution_trace("Function Assistant", False, "Request was not approved.")
                return

            assist_button.configure(state="disabled")
            save_button.configure(state="disabled")
            close_button.configure(state="disabled")
            self.status_var.set(f"Sending script assist request for {item.name}...")

            def emit_event(event: dict[str, str]) -> None:
                mapped = dict(event)
                mapped["title"] = "Function Assistant"
                self.root.after(0, lambda event=mapped: self._handle_agent_event(event))

            def finish_success(response: str) -> None:
                tool_calls, visible_response = self._extract_agent_tool_calls(response)
                if tool_calls:
                    raise RuntimeError("Function assistant must return plain text, not tool calls.")
                content = visible_response.strip()
                if not content:
                    raise RuntimeError("Function assistant returned empty content.")
                script_text.delete("1.0", tk.END)
                script_text.insert("1.0", content)
                apply_fields()
                self.selected_function_name = item.name
                self.selected_function_text_name = None
                self._refresh_all()
                self._append_chat_message("assistant", content)
                self._finish_execution_trace("Function Assistant", True, "Script updated.")
                self.status_var.set(f"Function assistant finished for {item.name}.")
                assist_button.configure(state="normal")
                save_button.configure(state="normal")
                close_button.configure(state="normal")

            def finish_error(exc: Exception) -> None:
                message = self._compact_activity_text(str(exc), limit=120) or "Function assistant failed."
                self._append_chat_message("error", message)
                messagebox.showerror("Function assistant error", message)
                self._finish_execution_trace("Function Assistant", False, message)
                self.status_var.set("Function assistant failed.")
                assist_button.configure(state="normal")
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

        assist_button = ttk.Button(button_row, text="Assist Script", command=request)
        assist_button.grid(row=0, column=0, sticky="ew", padx=(0, 6))
        save_button = ttk.Button(button_row, text="Save", command=save)
        save_button.grid(row=0, column=1, sticky="ew", padx=6)
        close_button = ttk.Button(button_row, text="Close", command=dialog.destroy)
        close_button.grid(row=0, column=2, sticky="ew", padx=(6, 0))
