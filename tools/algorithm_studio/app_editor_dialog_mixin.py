from __future__ import annotations

import tkinter as tk
from tkinter import messagebox, ttk

try:
    from .backend import ContainerFieldItem, ContainerItem, FunctionFrameItem, FunctionTextItem
    from .shared import COLORS
except ImportError:
    from backend import ContainerFieldItem, ContainerItem, FunctionFrameItem, FunctionTextItem
    from shared import COLORS


class AlgorithmStudioEditorDialogMixin:
    def _container_section_from_tags(self, tags: tuple[str, ...]) -> str | None:
        for tag in tags:
            if tag.startswith("container_section:"):
                section = tag.split(":", 1)[1].strip()
                if section in {"structure", "value", "fields"}:
                    return section
        return None

    def _container_layout_field_index_from_tags(self, tags: tuple[str, ...]) -> int | None:
        for tag in tags:
            if not tag.startswith("container_field:"):
                continue
            parts = tag.split(":")
            if len(parts) != 3:
                raise AssertionError(f"Invalid container field tag: {tag}")
            try:
                return int(parts[2])
            except Exception as exc:  # noqa: BLE001
                raise AssertionError(f"Invalid container field tag index: {tag}") from exc
        return None

    def _container_layout_fields(self, container: ContainerItem) -> list[ContainerFieldItem]:
        fields = getattr(container, "layout_fields", None)
        if fields is None:
            container.layout_fields = []
            fields = container.layout_fields
        if not isinstance(fields, list):
            raise AssertionError(f"Container {container.name} layout_fields must be a list.")
        for field_item in fields:
            if not isinstance(field_item, ContainerFieldItem):
                raise AssertionError(f"Container {container.name} has an invalid layout field entry.")
        return fields

    def _container_total_layout_bits(self, container: ContainerItem) -> int:
        stride = max(1, int(getattr(container, "stride", 4) or 4))
        return stride * 8

    def _container_layout_bits_used(self, container: ContainerItem, *, exclude_index: int | None = None) -> int:
        total = 0
        for index, field_item in enumerate(self._container_layout_fields(container)):
            if exclude_index is not None and index == exclude_index:
                continue
            total += int(field_item.bit_width)
        return total

    def _validate_container_layout_fields(self, container: ContainerItem) -> None:
        total_bits = self._container_total_layout_bits(container)
        used_bits = self._container_layout_bits_used(container)
        if used_bits > total_bits:
            raise AssertionError(
                f"Container {container.name} layout fields use {used_bits} bits, exceeding stride budget {total_bits} bits."
            )

    def _layout_field_structure_token(self, field_item: ContainerFieldItem) -> str:
        normalized_kind = str(field_item.kind or "").strip().lower()
        if normalized_kind not in {"variable", "array"}:
            raise AssertionError(f"Unsupported container field kind: {field_item.kind}")
        bit_width = int(field_item.bit_width)
        if bit_width <= 0:
            raise AssertionError(f"Container field {field_item.name} has non-positive bit width.")
        rule_text = str(field_item.rule_text or "").strip()
        if not rule_text:
            raise AssertionError(f"Container field {field_item.name} has empty rule text.")
        prefix = "v" if normalized_kind == "variable" else "a"
        return f"{prefix}:{bit_width}:{rule_text}"

    def _sync_container_structure_from_layout_fields(self, container: ContainerItem) -> None:
        fields = self._container_layout_fields(container)
        if not fields:
            container.structure = []
            return
        container.structure = [self._layout_field_structure_token(field_item) for field_item in fields]

    def _default_layout_field_rule_text(self, source_name: str, target_name: str, bit_width: int) -> str:
        normalized_source_name = str(source_name or "").strip()
        normalized_target_name = str(target_name or "").strip()
        if not normalized_source_name:
            raise AssertionError("Layout field source name cannot be empty.")
        if not normalized_target_name:
            raise AssertionError("Layout field target name cannot be empty.")
        if bit_width <= 0:
            raise AssertionError("Layout field bit width must be positive.")
        return f"from {normalized_source_name} to {normalized_target_name} {bit_width}"

    def _default_layout_field_source_name(self, kind: str) -> str:
        normalized = str(kind or "").strip().lower()
        if normalized not in {"variable", "array"}:
            raise AssertionError(f"Unsupported layout field kind: {kind}")
        return "v1" if normalized == "variable" else "a1"

    def _default_layout_field_target_name(self, container: ContainerItem) -> str:
        existing = {str(field_item.name).strip().lower() for field_item in self._container_layout_fields(container)}
        index = 1
        while True:
            candidate = f"field_{index}"
            if candidate.lower() not in existing:
                return candidate
            index += 1

    def _next_container_layout_field_name(self, container: ContainerItem, kind: str) -> str:
        normalized = str(kind or "").strip().lower()
        if normalized not in {"variable", "array"}:
            raise AssertionError(f"Unsupported layout field kind: {kind}")
        prefix = "v" if normalized == "variable" else "a"
        existing = {str(field_item.name).strip() for field_item in self._container_layout_fields(container)}
        index = 1
        while True:
            candidate = f"{prefix}{index}"
            if candidate not in existing:
                return candidate
            index += 1

    def _preferred_layout_field_name(self, container: ContainerItem, kind: str, preferred_name: str | None = None) -> str:
        normalized = str(kind or "").strip().lower()
        if normalized not in {"variable", "array"}:
            raise AssertionError(f"Unsupported layout field kind: {kind}")
        candidate = str(preferred_name or "").strip()
        existing = {str(field_item.name).strip() for field_item in self._container_layout_fields(container)}
        if candidate and candidate not in existing:
            return candidate
        return self._next_container_layout_field_name(container, normalized)

    def _add_layout_field_to_container(
        self,
        container: ContainerItem,
        kind: str,
        *,
        source_name: str | None = None,
        rule_text: str | None = None,
        bit_width: int = 32,
    ) -> ContainerFieldItem:
        normalized = str(kind or "").strip().lower()
        if normalized not in {"variable", "array"}:
            raise AssertionError(f"Unsupported layout field kind: {kind}")
        resolved_bit_width = int(bit_width)
        if resolved_bit_width <= 0:
            raise AssertionError("Layout field bit width must be positive.")
        resolved_source_name = str(source_name or "").strip() or self._default_layout_field_source_name(normalized)
        field_name = self._preferred_layout_field_name(container, normalized, resolved_source_name)
        target_name = self._default_layout_field_target_name(container)
        field_item = ContainerFieldItem(
            name=field_name,
            kind=normalized,
            bit_width=resolved_bit_width,
            rule_text=str(rule_text or "").strip() or self._default_layout_field_rule_text(resolved_source_name, target_name, resolved_bit_width),
        )
        self._container_layout_fields(container).append(field_item)
        self._validate_container_layout_fields(container)
        self._sync_container_structure_from_layout_fields(container)
        return field_item

    def _container_layout_field_summary_lines(self, container: ContainerItem, limit: int = 4) -> list[str]:
        fields = self._container_layout_fields(container)
        if not fields:
            return ["Drag v/a here to add layout rules"]
        lines: list[str] = []
        for field_item in fields[:limit]:
            rule_text = str(field_item.rule_text or "").strip()
            if not rule_text:
                raise AssertionError(f"Container field {container.name}:{field_item.name} is missing rule text.")
            lines.append(rule_text)
        if len(fields) > limit:
            lines.append(f"... ({len(fields)} total)")
        return lines

    def _container_structure_preview(self, container: ContainerItem, limit: int = 3) -> list[str]:
        layout_fields = self._container_layout_fields(container)
        if layout_fields:
            return self._container_layout_field_summary_lines(container, limit=limit)
        items = [str(value).strip() for value in getattr(container, "structure", []) if str(value).strip()]
        if not items:
            return ["-"]
        if len(items) <= limit:
            return items
        return items[:limit] + [f"... ({len(items)} total)"]

    def _container_structure_signature(self, container: ContainerItem) -> str:
        layout_fields = self._container_layout_fields(container)
        if layout_fields:
            signature = "".join("v" if field_item.kind == "variable" else "a" for field_item in layout_fields)
            return signature or "-"
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

    def _open_container_layout_field_editor(self, container: ContainerItem, field_index: int) -> None:
        fields = self._container_layout_fields(container)
        if field_index < 0 or field_index >= len(fields):
            raise AssertionError(f"Container field index out of range for {container.name}: {field_index}")
        field_item = fields[field_index]
        dialog = tk.Toplevel(self.root)
        dialog.title(f"{container.name} :: {field_item.name}")
        dialog.transient(self.root)
        dialog.grab_set()
        dialog.resizable(False, False)

        body = ttk.Frame(dialog, padding=12)
        body.grid(row=0, column=0, sticky="nsew")
        body.columnconfigure(1, weight=1)

        ttk.Label(body, text="Field Name").grid(row=0, column=0, sticky="w")
        name_var = tk.StringVar(value=field_item.name)
        ttk.Entry(body, textvariable=name_var).grid(row=0, column=1, sticky="ew", padx=(8, 0))

        ttk.Label(body, text="Kind").grid(row=1, column=0, sticky="w", pady=(10, 0))
        kind_var = tk.StringVar(value=field_item.kind)
        kind_box = ttk.Combobox(body, textvariable=kind_var, values=("variable", "array"), state="readonly")
        kind_box.grid(row=1, column=1, sticky="ew", padx=(8, 0), pady=(10, 0))

        ttk.Label(body, text="Bit Width").grid(row=2, column=0, sticky="w", pady=(10, 0))
        bit_width_var = tk.StringVar(value=str(field_item.bit_width))
        ttk.Entry(body, textvariable=bit_width_var).grid(row=2, column=1, sticky="ew", padx=(8, 0), pady=(10, 0))

        ttk.Label(body, text="Rule Text").grid(row=3, column=0, sticky="w", pady=(10, 0))
        rule_text_var = tk.StringVar(value=field_item.rule_text)
        ttk.Entry(body, textvariable=rule_text_var).grid(row=3, column=1, sticky="ew", padx=(8, 0), pady=(10, 0))

        hint_label = ttk.Label(
            body,
            text=(
                f"Stride budget: {self._container_total_layout_bits(container)} bits. "
                "Use free rule text such as: from v1 to x,y 16,16"
            ),
            foreground=COLORS["muted"],
            wraplength=360,
        )
        hint_label.grid(row=4, column=0, columnspan=2, sticky="ew", pady=(12, 0))

        button_row = ttk.Frame(body)
        button_row.grid(row=5, column=0, columnspan=2, sticky="ew", pady=(14, 0))
        button_row.columnconfigure((0, 1, 2), weight=1)

        def _apply_and_close() -> None:
            try:
                updated_name = name_var.get().strip()
                if not updated_name:
                    raise AssertionError("Container field name cannot be empty.")
                updated_kind = kind_var.get().strip().lower()
                if updated_kind not in {"variable", "array"}:
                    raise AssertionError(f"Unsupported container field kind: {updated_kind}")
                updated_bit_width = int(bit_width_var.get().strip())
                if updated_bit_width <= 0:
                    raise AssertionError("Container field bit width must be positive.")
                updated_rule_text = rule_text_var.get().strip()
                if not updated_rule_text:
                    raise AssertionError("Container field rule text cannot be empty.")
                used_bits_without_current = self._container_layout_bits_used(container, exclude_index=field_index)
                total_bits = self._container_total_layout_bits(container)
                if used_bits_without_current + updated_bit_width > total_bits:
                    raise AssertionError(
                        f"Container {container.name} would use {used_bits_without_current + updated_bit_width} bits, exceeding {total_bits} bits."
                    )
                field_item.name = updated_name
                field_item.kind = updated_kind
                field_item.bit_width = updated_bit_width
                field_item.rule_text = updated_rule_text
                self._sync_container_structure_from_layout_fields(container)
                self._refresh_all()
                self._log(f"Updated layout field {container.name}:{field_item.name}.")
                dialog.destroy()
            except Exception as exc:  # noqa: BLE001
                messagebox.showerror("Invalid field", str(exc))

        def _delete_field() -> None:
            del fields[field_index]
            self._sync_container_structure_from_layout_fields(container)
            self._refresh_all()
            self._log(f"Deleted layout field {container.name}:{field_item.name}.")
            dialog.destroy()

        ttk.Button(button_row, text="Apply", command=_apply_and_close).grid(row=0, column=0, sticky="ew", padx=(0, 6))
        ttk.Button(button_row, text="Delete", command=_delete_field).grid(row=0, column=1, sticky="ew", padx=6)
        ttk.Button(button_row, text="Cancel", command=dialog.destroy).grid(row=0, column=2, sticky="ew", padx=(6, 0))

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

    def _function_link_preview_items(self, function_name: str, direction: str) -> list[tuple[str, str, str]]:
        if direction not in {"input", "output"}:
            raise AssertionError(f"Unsupported function link direction: {direction}")
        items: list[tuple[str, str, str]] = []
        if direction == "input":
            connections = [
                connection
                for connection in self.project.connections
                if connection.target_kind == "function" and connection.target_name == function_name
            ]
            connections.sort(key=lambda connection: (connection.target_port, connection.source_kind, connection.source_name, connection.source_port))
            for connection in connections:
                text = f"{connection.target_port or 'in'} <- {connection.source_kind}:{connection.source_name}"
                items.append((text, connection.source_kind, connection.source_name))
        else:
            connections = [
                connection
                for connection in self.project.connections
                if connection.source_kind == "function" and connection.source_name == function_name
            ]
            connections.sort(key=lambda connection: (connection.source_port, connection.target_kind, connection.target_name, connection.target_port))
            for connection in connections:
                text = f"{connection.source_port or 'out'} -> {connection.target_kind}:{connection.target_name}"
                items.append((text, connection.target_kind, connection.target_name))
        return items

    def _function_link_preview_color(self, kind: str, name: str) -> str:
        normalized_kind = str(kind or "").strip().lower()
        if normalized_kind == "container":
            container = self._find_container(str(name).strip())
            if container is not None and container.kind == "array":
                return COLORS["container_array"]
            return COLORS["container"]
        if normalized_kind == "containerelement":
            return COLORS["accent"]
        if normalized_kind == "decomposer":
            return COLORS["descriptor"]
        if normalized_kind == "reflector":
            return COLORS["accent_2"]
        if normalized_kind == "resnode":
            return COLORS["resource"]
        if normalized_kind in {"interventioner", "stage"}:
            return COLORS["stage"]
        if normalized_kind == "function":
            return COLORS["agent"]
        if normalized_kind == "functiontext":
            return COLORS["muted"]
        return COLORS["edge"]

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
        layout_fields_active = bool(self._container_layout_fields(container))

        structure_title = "Layout Rules Preview" if layout_fields_active else "Structure"
        structure_frame = ttk.LabelFrame(body, text=structure_title, padding=8)
        structure_frame.grid(row=2, column=0, sticky="nsew", pady=(12, 0), padx=(0, 6))
        structure_frame.columnconfigure(0, weight=1)
        structure_frame.rowconfigure(1, weight=1)
        if layout_fields_active:
            hint = ttk.Label(
                structure_frame,
                text="This container uses internal layout rules. Double-click a rule row on the canvas to edit it.",
                foreground=COLORS["muted"],
                wraplength=360,
                justify="left",
            )
            hint.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 8))
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
        structure_text.grid(row=1, column=0, sticky="nsew")
        structure_text.insert("1.0", "\n".join(self._container_structure_preview(container, limit=9999)))
        if layout_fields_active:
            structure_text.configure(state="disabled")
        structure_scroll = ttk.Scrollbar(structure_frame, orient="vertical", command=structure_text.yview)
        structure_scroll.grid(row=1, column=1, sticky="ns")
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
            if layout_fields_active:
                self._sync_container_structure_from_layout_fields(container)
            else:
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
        dialog.geometry("960x780")
        dialog.minsize(860, 680)

        body = ttk.Frame(dialog, padding=12)
        body.grid(row=0, column=0, sticky="nsew")
        dialog.columnconfigure(0, weight=1)
        dialog.rowconfigure(0, weight=1)
        body.columnconfigure(0, weight=1)
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

        top_row = ttk.Frame(body)
        top_row.grid(row=2, column=0, sticky="nsew", pady=(12, 0))
        top_row.columnconfigure(0, weight=1)
        top_row.columnconfigure(1, weight=1)
        top_row.columnconfigure(2, weight=1)

        expected_input_frame = ttk.LabelFrame(top_row, text="Expected Input", padding=8)
        expected_input_frame.grid(row=0, column=0, sticky="nsew", padx=(0, 8))
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

        expected_output_frame = ttk.LabelFrame(top_row, text="Expected Output", padding=8)
        expected_output_frame.grid(row=0, column=1, sticky="nsew", padx=8)
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

        linked_column = ttk.Frame(top_row)
        linked_column.grid(row=0, column=2, sticky="nsew", padx=(8, 0))
        linked_column.columnconfigure(0, weight=1)
        linked_column.rowconfigure(0, weight=1)
        linked_column.rowconfigure(1, weight=1)

        linked_in_frame = ttk.LabelFrame(linked_column, text="Linked In", padding=6)
        linked_in_frame.grid(row=0, column=0, sticky="nsew")
        linked_in_frame.columnconfigure(0, weight=1)
        linked_in_frame.rowconfigure(1, weight=1)
        linked_in_toolbar = ttk.Frame(linked_in_frame)
        linked_in_toolbar.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 6))
        linked_in_toolbar.columnconfigure(0, weight=1)
        linked_in_items = tk.Frame(
            linked_in_frame,
            bg=COLORS["canvas"],
            relief="flat",
            borderwidth=0,
            highlightthickness=0,
        )
        linked_in_items.grid(row=1, column=0, sticky="nsew")

        linked_out_frame = ttk.LabelFrame(linked_column, text="Linked Out", padding=6)
        linked_out_frame.grid(row=1, column=0, sticky="nsew", pady=(8, 0))
        linked_out_frame.columnconfigure(0, weight=1)
        linked_out_frame.rowconfigure(1, weight=1)
        linked_out_toolbar = ttk.Frame(linked_out_frame)
        linked_out_toolbar.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 6))
        linked_out_toolbar.columnconfigure(0, weight=1)
        linked_out_items = tk.Frame(
            linked_out_frame,
            bg=COLORS["canvas"],
            relief="flat",
            borderwidth=0,
            highlightthickness=0,
        )
        linked_out_items.grid(row=1, column=0, sticky="nsew")

        script_frame = ttk.LabelFrame(body, text="Script / Pseudocode", padding=8)
        script_frame.grid(row=3, column=0, sticky="nsew", pady=(12, 0))
        script_frame.columnconfigure(0, weight=1)
        script_frame.rowconfigure(2, weight=1)

        node_info_label = ttk.Label(script_frame, foreground=COLORS["muted"], anchor="w")
        node_info_label.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 8))

        script_toolbar = ttk.Frame(script_frame)
        script_toolbar.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(0, 8))
        script_toolbar.columnconfigure(0, weight=1)
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
        script_text.grid(row=2, column=0, sticky="nsew")
        script_text.insert("1.0", item.script)
        script_scroll_y = ttk.Scrollbar(script_frame, orient="vertical", command=script_text.yview)
        script_scroll_y.grid(row=2, column=1, sticky="ns")
        script_scroll_x = ttk.Scrollbar(script_frame, orient="horizontal", command=script_text.xview)
        script_scroll_x.grid(row=3, column=0, sticky="ew")
        script_text.configure(yscrollcommand=script_scroll_y.set, xscrollcommand=script_scroll_x.set)

        button_row = ttk.Frame(body)
        button_row.grid(row=4, column=0, sticky="ew", pady=(12, 0))
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

        def set_linked_nodes_preview(parent: tk.Frame, direction: str) -> None:
            for child in list(parent.winfo_children()):
                child.destroy()
            preview_items = self._function_link_preview_items(item.name, direction)
            if not preview_items:
                empty_label = tk.Label(
                    parent,
                    text=f"No linked {direction} nodes.",
                    anchor="w",
                    bg=COLORS["canvas"],
                    fg=COLORS["muted"],
                    padx=8,
                    pady=6,
                )
                empty_label.grid(row=0, column=0, sticky="ew")
                return
            parent.columnconfigure(0, weight=1)
            for index, (line_text, node_kind, node_name) in enumerate(preview_items):
                item_bg = self._function_link_preview_color(node_kind, node_name)
                label = tk.Label(
                    parent,
                    text=line_text,
                    anchor="w",
                    justify="left",
                    bg=item_bg,
                    fg="#ffffff",
                    padx=10,
                    pady=6,
                    relief="flat",
                )
                label.grid(row=index, column=0, sticky="ew", pady=(0, 6 if index < len(preview_items) - 1 else 0))

        def refresh_linked_node_views() -> None:
            set_linked_nodes_preview(linked_in_items, "input")
            set_linked_nodes_preview(linked_out_items, "output")

        def refresh_node_info_bar(_event: tk.Event | None = None) -> None:
            name_text = name_entry.get().strip() or item.name
            input_text = input_name_entry.get().strip() or "in"
            output_text = output_name_entry.get().strip() or "out"
            script_language = script_language_entry.get().strip() or "pseudocode"
            node_info_label.configure(
                text=f"fun {name_text} | in: {input_text} | out: {output_text} | lang: {script_language}"
            )

        refresh_node_info_bar()
        refresh_linked_node_views()
        for widget in (name_entry, input_name_entry, output_name_entry):
            widget.bind("<KeyRelease>", refresh_node_info_bar)
        script_language_entry.bind("<<ComboboxSelected>>", refresh_node_info_bar)
        script_language_entry.bind("<KeyRelease>", refresh_node_info_bar)

        ttk.Button(expected_input_toolbar, text="Load Linked Inputs", command=load_expected_input).grid(row=0, column=1, sticky="e")
        ttk.Button(expected_output_toolbar, text="Load Linked Outputs", command=load_expected_output).grid(row=0, column=1, sticky="e")
        ttk.Button(linked_in_toolbar, text="Refresh In", command=refresh_linked_node_views).grid(row=0, column=1, sticky="e")
        ttk.Button(linked_out_toolbar, text="Refresh Out", command=refresh_linked_node_views).grid(row=0, column=1, sticky="e")

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
