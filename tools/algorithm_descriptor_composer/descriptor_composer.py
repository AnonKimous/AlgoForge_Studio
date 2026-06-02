#!/usr/bin/env python3

from __future__ import annotations

import json
import copy
from dataclasses import dataclass, field
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from typing import Any


SECTION_ORDER = [
    "arrays_to_allocate",
    "temporary_registers_to_allocate",
    "temporary_caches_to_allocate",
    "filled_data_formats",
    "algorithm_required_formats",
]


@dataclass
class ContainerEntry:
    section: str
    source_name: str
    element_count: int
    element_stride: int


@dataclass
class DescriptorRecord:
    source_path: Path
    package_name: str
    algorithm_name: str
    cpu_available: bool
    gpu_available: bool
    raw: dict[str, Any]
    containers: list[ContainerEntry] = field(default_factory=list)


@dataclass
class PipelineStep:
    record: DescriptorRecord
    alias_map: dict[str, str] = field(default_factory=dict)


def _as_int(value: Any, fallback: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return fallback


def _as_bool(value: Any, fallback: bool = False) -> bool:
    if isinstance(value, bool):
        return value
    if value is None:
        return fallback
    if isinstance(value, (int, float)):
        return bool(value)
    if isinstance(value, str):
        normalized = value.strip().lower()
        if normalized in {"1", "true", "yes", "on"}:
            return True
        if normalized in {"0", "false", "no", "off"}:
            return False
    return fallback


def _load_descriptor(path: Path) -> DescriptorRecord:
    payload = json.loads(path.read_text(encoding="utf-8"))
    data_contract = payload.get("data_contract", {})
    package_name = str(payload.get("package_name") or payload.get("algorithm_name") or path.stem)
    algorithm_name = str(payload.get("algorithm_name") or package_name)
    cpu_available = _as_bool(payload.get("cpu_available"), False)
    gpu_available = _as_bool(payload.get("gpu_available"), False)
    raw_containers: list[ContainerEntry] = []

    for section in SECTION_ORDER:
        for entry in data_contract.get(section, []):
            source_name = str(entry.get("name") or "").strip()
            if not source_name:
                continue
            raw_containers.append(
                ContainerEntry(
                    section=section,
                    source_name=source_name,
                    element_count=_as_int(entry.get("element_count")),
                    element_stride=_as_int(entry.get("element_stride")),
                )
            )

    return DescriptorRecord(
        source_path=path,
        package_name=package_name,
        algorithm_name=algorithm_name,
        cpu_available=cpu_available,
        gpu_available=gpu_available,
        raw=payload,
        containers=raw_containers,
    )


def _serialize_container(container: ContainerEntry, alias_name: str | None = None) -> dict[str, Any]:
    return {
        "name": alias_name or container.source_name,
        "element_count": container.element_count,
        "element_stride": container.element_stride,
    }


class DescriptorComposerApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("Algorithm Container Descriptor Composer")
        self.root.geometry("1280x840")

        self.available_records: list[DescriptorRecord] = []
        self.pipeline_steps: list[PipelineStep] = []
        self.selected_container_source_name: str | None = None

        self.pipeline_name_var = tk.StringVar(value="composite_pipeline")
        self.composite_cpu_var = tk.BooleanVar(value=False)
        self.composite_gpu_var = tk.BooleanVar(value=False)
        self.alias_var = tk.StringVar()
        self.status_var = tk.StringVar(value="Load one or more container descriptor JSON files to begin.")

        self._build_layout()
        self._refresh_all_views()

    def _build_layout(self) -> None:
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(2, weight=1)

        header = ttk.Frame(self.root, padding=12)
        header.grid(row=0, column=0, sticky="ew")
        header.columnconfigure(1, weight=1)

        ttk.Label(header, text="Pipeline Name").grid(row=0, column=0, sticky="w")
        pipeline_entry = ttk.Entry(header, textvariable=self.pipeline_name_var, width=42)
        pipeline_entry.grid(row=0, column=1, sticky="ew", padx=(8, 16))
        pipeline_entry.bind("<KeyRelease>", lambda _event: self._refresh_preview())

        ttk.Checkbutton(header, text="CPU Available", variable=self.composite_cpu_var, command=self._refresh_preview).grid(row=0, column=2, padx=(0, 8))
        ttk.Checkbutton(header, text="GPU Available", variable=self.composite_gpu_var, command=self._refresh_preview).grid(row=0, column=3, padx=(0, 16))

        ttk.Button(header, text="Load Descriptor Files", command=self._load_descriptors).grid(row=0, column=4, padx=(0, 8))
        ttk.Button(header, text="Export Composite JSON", command=self._export_composite).grid(row=0, column=5)

        content = ttk.Frame(self.root, padding=(12, 0, 12, 12))
        content.grid(row=1, column=0, sticky="nsew")
        content.columnconfigure(0, weight=1)
        content.columnconfigure(1, weight=1)
        content.columnconfigure(2, weight=2)
        content.rowconfigure(0, weight=1)

        self._build_available_panel(content)
        self._build_pipeline_panel(content)
        self._build_step_panel(content)

        preview = ttk.LabelFrame(self.root, text="Composite Preview", padding=8)
        preview.grid(row=2, column=0, sticky="nsew", padx=12, pady=(0, 8))
        preview.rowconfigure(0, weight=1)
        preview.columnconfigure(0, weight=1)

        self.preview_text = tk.Text(preview, wrap="none", height=18, font=("Consolas", 10))
        self.preview_text.grid(row=0, column=0, sticky="nsew")
        preview_scroll = ttk.Scrollbar(preview, orient="vertical", command=self.preview_text.yview)
        preview_scroll.grid(row=0, column=1, sticky="ns")
        self.preview_text.configure(yscrollcommand=preview_scroll.set)

        status = ttk.Label(self.root, textvariable=self.status_var, relief="sunken", anchor="w", padding=(8, 4))
        status.grid(row=3, column=0, sticky="ew")

    def _build_available_panel(self, parent: ttk.Frame) -> None:
        frame = ttk.LabelFrame(parent, text="Loaded Descriptors", padding=8)
        frame.grid(row=0, column=0, sticky="nsew", padx=(0, 8))
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(0, weight=1)

        self.available_list = tk.Listbox(frame, exportselection=False, height=20)
        self.available_list.grid(row=0, column=0, sticky="nsew")
        self.available_list.bind("<<ListboxSelect>>", lambda _event: self._refresh_preview())

        scrollbar = ttk.Scrollbar(frame, orient="vertical", command=self.available_list.yview)
        scrollbar.grid(row=0, column=1, sticky="ns")
        self.available_list.configure(yscrollcommand=scrollbar.set)

        button_bar = ttk.Frame(frame)
        button_bar.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        ttk.Button(button_bar, text="Add To Pipeline", command=self._add_selected_descriptor).pack(side="left")
        ttk.Button(button_bar, text="Remove Loaded", command=self._remove_loaded_descriptor).pack(side="left", padx=(8, 0))

    def _build_pipeline_panel(self, parent: ttk.Frame) -> None:
        frame = ttk.LabelFrame(parent, text="Pipeline Order", padding=8)
        frame.grid(row=0, column=1, sticky="nsew", padx=(0, 8))
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(0, weight=1)

        self.pipeline_list = tk.Listbox(frame, exportselection=False, height=20)
        self.pipeline_list.grid(row=0, column=0, sticky="nsew")
        self.pipeline_list.bind("<<ListboxSelect>>", lambda _event: self._refresh_step_details())

        scrollbar = ttk.Scrollbar(frame, orient="vertical", command=self.pipeline_list.yview)
        scrollbar.grid(row=0, column=1, sticky="ns")
        self.pipeline_list.configure(yscrollcommand=scrollbar.set)

        button_bar = ttk.Frame(frame)
        button_bar.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        ttk.Button(button_bar, text="Move Up", command=self._move_step_up).pack(side="left")
        ttk.Button(button_bar, text="Move Down", command=self._move_step_down).pack(side="left", padx=(8, 0))
        ttk.Button(button_bar, text="Remove Step", command=self._remove_pipeline_step).pack(side="left", padx=(8, 0))

    def _build_step_panel(self, parent: ttk.Frame) -> None:
        frame = ttk.LabelFrame(parent, text="Step Containers and Aliases", padding=8)
        frame.grid(row=0, column=2, sticky="nsew")
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(0, weight=1)

        columns = ("section", "source", "alias", "count", "stride")
        self.container_tree = ttk.Treeview(frame, columns=columns, show="headings", selectmode="browse", height=18)
        for column, title, width in [
            ("section", "Section", 170),
            ("source", "Original", 200),
            ("alias", "Alias", 200),
            ("count", "Count", 80),
            ("stride", "Stride", 80),
        ]:
            self.container_tree.heading(column, text=title)
            self.container_tree.column(column, width=width, stretch=True)
        self.container_tree.grid(row=0, column=0, sticky="nsew")
        self.container_tree.bind("<<TreeviewSelect>>", lambda _event: self._sync_alias_entry_from_selection())

        scrollbar = ttk.Scrollbar(frame, orient="vertical", command=self.container_tree.yview)
        scrollbar.grid(row=0, column=1, sticky="ns")
        self.container_tree.configure(yscrollcommand=scrollbar.set)

        alias_bar = ttk.Frame(frame)
        alias_bar.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        alias_bar.columnconfigure(1, weight=1)

        ttk.Label(alias_bar, text="Alias").grid(row=0, column=0, sticky="w")
        alias_entry = ttk.Entry(alias_bar, textvariable=self.alias_var)
        alias_entry.grid(row=0, column=1, sticky="ew", padx=(8, 8))
        alias_entry.bind("<KeyRelease>", lambda _event: self._refresh_preview())
        ttk.Button(alias_bar, text="Apply Alias", command=self._apply_alias).grid(row=0, column=2)
        ttk.Button(alias_bar, text="Keep Original", command=self._reset_alias_to_original).grid(row=0, column=3, padx=(8, 0))

    def _load_descriptors(self) -> None:
        paths = filedialog.askopenfilenames(
            title="Select container descriptor JSON files",
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
        )
        if not paths:
            return

        loaded = 0
        for raw_path in paths:
            path = Path(raw_path)
            try:
                record = _load_descriptor(path)
            except Exception as exc:  # noqa: BLE001
                messagebox.showerror("Load failed", f"{path}\n\n{exc}")
                continue
            self.available_records.append(record)
            loaded += 1

        if loaded:
            self.status_var.set(f"Loaded {loaded} container descriptor file(s).")
            self._refresh_all_views()

    def _remove_loaded_descriptor(self) -> None:
        selection = self.available_list.curselection()
        if not selection:
            return
        index = selection[0]
        removed = self.available_records.pop(index)
        self.status_var.set(f"Removed {removed.algorithm_name}.")
        self._refresh_all_views()

    def _add_selected_descriptor(self) -> None:
        selection = self.available_list.curselection()
        if not selection:
            messagebox.showinfo("Select one", "Pick a loaded descriptor first.")
            return
        record = self.available_records[selection[0]]
        self.pipeline_steps.append(PipelineStep(record=copy.deepcopy(record), alias_map={}))
        self.status_var.set(f"Added {record.algorithm_name} to pipeline.")
        self._refresh_all_views()
        self.pipeline_list.selection_clear(0, tk.END)
        self.pipeline_list.selection_set(len(self.pipeline_steps) - 1)
        self.pipeline_list.see(len(self.pipeline_steps) - 1)
        self._refresh_step_details()

    def _remove_pipeline_step(self) -> None:
        index = self._selected_pipeline_index()
        if index is None:
            return
        removed = self.pipeline_steps.pop(index)
        self.status_var.set(f"Removed pipeline step {removed.record.algorithm_name}.")
        self._refresh_all_views()
        if self.pipeline_steps:
            next_index = min(index, len(self.pipeline_steps) - 1)
            self.pipeline_list.selection_set(next_index)
            self.pipeline_list.see(next_index)
        self._refresh_step_details()

    def _move_step_up(self) -> None:
        index = self._selected_pipeline_index()
        if index is None or index == 0:
            return
        self.pipeline_steps[index - 1], self.pipeline_steps[index] = self.pipeline_steps[index], self.pipeline_steps[index - 1]
        self._refresh_all_views()
        self.pipeline_list.selection_set(index - 1)
        self.pipeline_list.see(index - 1)
        self._refresh_step_details()

    def _move_step_down(self) -> None:
        index = self._selected_pipeline_index()
        if index is None or index >= len(self.pipeline_steps) - 1:
            return
        self.pipeline_steps[index + 1], self.pipeline_steps[index] = self.pipeline_steps[index], self.pipeline_steps[index + 1]
        self._refresh_all_views()
        self.pipeline_list.selection_set(index + 1)
        self.pipeline_list.see(index + 1)
        self._refresh_step_details()

    def _selected_pipeline_index(self) -> int | None:
        selection = self.pipeline_list.curselection()
        if not selection:
            return None
        index = selection[0]
        if 0 <= index < len(self.pipeline_steps):
            return index
        return None

    def _current_step(self) -> PipelineStep | None:
        index = self._selected_pipeline_index()
        if index is None:
            return None
        return self.pipeline_steps[index]

    def _refresh_available_list(self) -> None:
        self.available_list.delete(0, tk.END)
        for record in self.available_records:
            self.available_list.insert(
                tk.END,
                f"{record.algorithm_name}  [{record.source_path.name}]",
            )

    def _refresh_pipeline_list(self) -> None:
        self.pipeline_list.delete(0, tk.END)
        for step in self.pipeline_steps:
            self.pipeline_list.insert(tk.END, step.record.algorithm_name)

    def _refresh_step_details(self) -> None:
        for item in self.container_tree.get_children():
            self.container_tree.delete(item)

        step = self._current_step()
        self.selected_container_source_name = None
        self.alias_var.set("")

        if not step:
            self._refresh_preview()
            return

        for container in step.record.containers:
            alias_name = step.alias_map.get(container.source_name, container.source_name)
            self.container_tree.insert(
                "",
                tk.END,
                values=(
                    container.section,
                    container.source_name,
                    alias_name,
                    container.element_count,
                    container.element_stride,
                ),
            )

        children = self.container_tree.get_children()
        if children:
            self.container_tree.selection_set(children[0])
            self.container_tree.focus(children[0])
            self._sync_alias_entry_from_selection()

        self._refresh_preview()

    def _selected_container(self) -> ContainerEntry | None:
        step = self._current_step()
        selection = self.container_tree.selection()
        if not step or not selection:
            return None

        source_name = self.container_tree.item(selection[0], "values")[1]
        for container in step.record.containers:
            if container.source_name == source_name:
                return container
        return None

    def _sync_alias_entry_from_selection(self) -> None:
        step = self._current_step()
        container = self._selected_container()
        if not step or not container:
            return
        self.selected_container_source_name = container.source_name
        self.alias_var.set(step.alias_map.get(container.source_name, container.source_name))

    def _apply_alias(self) -> None:
        step = self._current_step()
        container = self._selected_container()
        if not step or not container:
            return

        alias_name = self.alias_var.get().strip()
        if not alias_name or alias_name == container.source_name:
            step.alias_map.pop(container.source_name, None)
            alias_name = container.source_name
        else:
            step.alias_map[container.source_name] = alias_name

        self._refresh_step_details()
        self._select_container_by_name(container.source_name)
        self.status_var.set(f"Alias set: {container.source_name} -> {alias_name}")

    def _reset_alias_to_original(self) -> None:
        step = self._current_step()
        container = self._selected_container()
        if not step or not container:
            return
        step.alias_map.pop(container.source_name, None)
        self.alias_var.set(container.source_name)
        self._refresh_step_details()
        self._select_container_by_name(container.source_name)
        self.status_var.set(f"Alias reset for {container.source_name}.")

    def _select_container_by_name(self, source_name: str) -> None:
        for item in self.container_tree.get_children():
            values = self.container_tree.item(item, "values")
            if len(values) >= 2 and values[1] == source_name:
                self.container_tree.selection_set(item)
                self.container_tree.focus(item)
                break

    def _refresh_all_views(self) -> None:
        self._refresh_available_list()
        self._refresh_pipeline_list()
        self._refresh_preview()

    def _build_composite_payload(self) -> tuple[dict[str, Any], list[str]]:
        pipeline_name = self.pipeline_name_var.get().strip() or "composite_pipeline"
        warnings: list[str] = []
        component_descriptors: list[dict[str, Any]] = []
        ordered_bindings: list[dict[str, Any]] = []
        container_aliases: list[dict[str, Any]] = []

        merged_contract: dict[str, list[dict[str, Any]]] = {section: [] for section in SECTION_ORDER}
        seen_names_by_section: dict[str, set[str]] = {section: set() for section in SECTION_ORDER}
        seen_alias_keys: set[tuple[str, str]] = set()

        for step in self.pipeline_steps:
            record = step.record
            component_descriptors.append(copy.deepcopy(record.raw))

            binding = {
                "package_name": record.package_name,
                "algorithm_name": record.algorithm_name,
                "input_containers": [],
                "output_containers": [],
                "container_aliases": [],
            }

            for container in record.containers:
                alias_name = step.alias_map.get(container.source_name, container.source_name).strip() or container.source_name
                alias_entry = {
                    "package_name": record.package_name,
                    "source_name": container.source_name,
                    "alias_name": alias_name,
                }

                if alias_name != container.source_name:
                    binding["container_aliases"].append(alias_entry)
                    key = (record.package_name, container.source_name)
                    if key not in seen_alias_keys:
                        container_aliases.append(alias_entry)
                        seen_alias_keys.add(key)

                serialized_entry = _serialize_container(container, alias_name)
                if container.section == "algorithm_required_formats":
                    binding["input_containers"].append(alias_name)
                else:
                    binding["output_containers"].append(alias_name)

                if alias_name in seen_names_by_section[container.section]:
                    warnings.append(
                        f"Duplicate {container.section} alias '{alias_name}' in {record.algorithm_name}; keeping the first definition."
                    )
                    continue

                merged_contract[container.section].append(serialized_entry)
                seen_names_by_section[container.section].add(alias_name)

            ordered_bindings.append(binding)

        composite_descriptor = {
            "algorithm_name": pipeline_name,
            "cpu_available": self.composite_cpu_var.get(),
            "gpu_available": self.composite_gpu_var.get(),
            "data_contract": {
                section: merged_contract[section] for section in SECTION_ORDER
            },
        }
        if container_aliases:
            composite_descriptor["data_contract"]["container_aliases"] = container_aliases
        else:
            composite_descriptor["data_contract"]["container_aliases"] = []

        payload = {
            "pipeline_name": pipeline_name,
            "ordered_bindings": ordered_bindings,
            "component_descriptors": component_descriptors,
            "composite_container_descriptor": composite_descriptor,
        }
        return payload, warnings

    def _refresh_preview(self) -> None:
        payload, warnings = self._build_composite_payload()
        preview_text = json.dumps(payload, indent=2, ensure_ascii=False)
        self.preview_text.delete("1.0", tk.END)
        self.preview_text.insert("1.0", preview_text)

        if warnings:
            self.status_var.set(" | ".join(warnings[:3]))
        elif self.pipeline_steps:
            self.status_var.set(f"Prepared {len(self.pipeline_steps)} pipeline step(s).")
        elif self.available_records:
            self.status_var.set("Loaded container descriptors. Add one or more steps to compose a pipeline.")
        else:
            self.status_var.set("Load one or more container descriptor JSON files to begin.")

    def _export_composite(self) -> None:
        payload, warnings = self._build_composite_payload()
        if not self.pipeline_steps:
            messagebox.showinfo("Nothing to export", "Add at least one algorithm step first.")
            return

        if warnings:
            messagebox.showwarning(
                "Composition warnings",
                "\n".join(warnings),
            )

        path = filedialog.asksaveasfilename(
            title="Save composite container descriptor",
            defaultextension=".json",
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
        )
        if not path:
            return

        Path(path).write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
        self.status_var.set(f"Exported {path}.")


def main() -> None:
    root = tk.Tk()
    try:
        ttk.Style().theme_use("clam")
    except tk.TclError:
        pass
    DescriptorComposerApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
