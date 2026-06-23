from __future__ import annotations

import re


class AlgorithmStudioContainerTransactionMixin:
    def _parse_standard_container_name(self, name: str) -> tuple[str, int] | None:
        match = re.fullmatch(r"([va])(\d+)", str(name).strip())
        if not match:
            return None
        return match.group(1), int(match.group(2))

    def _canonical_shared_name(self, name: str) -> str:
        normalized = str(name).strip()
        match = re.fullmatch(r"((?:v|a)\d+)(?:_\d+)?", normalized)
        if match:
            return match.group(1)
        return normalized

    def _rewrite_container_name_list(
        self,
        values: list[str],
        *,
        removed_names: set[str],
        rename_map: dict[str, str],
    ) -> list[str]:
        rewritten: list[str] = []
        for name in values:
            if name in removed_names:
                continue
            target_name = rename_map.get(name, name)
            if target_name not in rewritten:
                rewritten.append(target_name)
        return rewritten

    def _rename_remaining_container_objects(self, rename_map: dict[str, str]) -> None:
        if not rename_map:
            return
        staged_names: dict[str, str] = {}
        serial = 0
        for container in self.project.containers:
            old_name = container.name
            new_name = rename_map.get(old_name)
            if not new_name or new_name == old_name:
                continue
            temp_name = f"__rename__{serial}__{old_name}"
            serial += 1
            staged_names[temp_name] = new_name
            container.name = temp_name
        for container in self.project.containers:
            final_name = staged_names.get(container.name)
            if final_name:
                container.name = final_name

    def _rewrite_selection_state_after_container_change(
        self,
        removed_names: set[str],
        rename_map: dict[str, str],
    ) -> None:
        if self.selected_container_name in removed_names:
            self.selected_container_name = None
        elif self.selected_container_name:
            self.selected_container_name = rename_map.get(self.selected_container_name, self.selected_container_name)
        if not self.selection_state:
            return
        self.selection_state["variables"] = self._rewrite_container_name_list(
            list(self.selection_state.get("variables", [])),
            removed_names=removed_names,
            rename_map=rename_map,
        )
        self.selection_state["arrays"] = self._rewrite_container_name_list(
            list(self.selection_state.get("arrays", [])),
            removed_names=removed_names,
            rename_map=rename_map,
        )

    def _apply_container_reference_transaction(
        self,
        *,
        removed_names: set[str],
        rename_map: dict[str, str],
    ) -> None:
        for group in self.project.container_groups:
            group.variables = self._rewrite_container_name_list(
                list(group.variables),
                removed_names=removed_names,
                rename_map=rename_map,
            )
            group.arrays = self._rewrite_container_name_list(
                list(group.arrays),
                removed_names=removed_names,
                rename_map=rename_map,
            )

        removed_rule_names: set[str] = set()
        next_rules = []
        for rule in self.project.decomposer_rules:
            if rule.source in removed_names or rule.target in removed_names:
                removed_rule_names.add(rule.name)
                continue
            rule.source = rename_map.get(rule.source, rule.source)
            rule.target = rename_map.get(rule.target, rule.target)
            next_rules.append(rule)
        self.project.decomposer_rules = next_rules

        removed_reflector_names: set[str] = set()
        next_reflectors = []
        for item in self.project.reflector_items:
            referenced_names = set(item.direct_from) | set(item.direct_to) | set(item.inputs_varity) | set(item.inputs_array)
            if item.output_name:
                referenced_names.add(item.output_name)
            if referenced_names & removed_names:
                removed_reflector_names.add(item.name)
                continue
            item.direct_from = [rename_map.get(name, name) for name in item.direct_from]
            item.direct_to = [rename_map.get(name, name) for name in item.direct_to]
            item.inputs_varity = [rename_map.get(name, name) for name in item.inputs_varity]
            item.inputs_array = [rename_map.get(name, name) for name in item.inputs_array]
            item.output_name = rename_map.get(item.output_name, item.output_name)
            next_reflectors.append(item)
        self.project.reflector_items = next_reflectors

        removed_stage_names: set[str] = set()
        next_stages = []
        for stage in self.project.intervention_stages:
            referenced_names = set(stage.used_variables) | set(stage.used_arrays)
            if referenced_names & removed_names:
                removed_stage_names.add(stage.name)
                continue
            stage.used_variables = [rename_map.get(name, name) for name in stage.used_variables]
            stage.used_arrays = [rename_map.get(name, name) for name in stage.used_arrays]
            next_stages.append(stage)
        self.project.intervention_stages = next_stages

        removed_nodes_by_kind = {
            "decomposer": removed_rule_names,
            "reflector": removed_reflector_names,
            "interventioner": removed_stage_names,
            "stage": removed_stage_names,
        }
        next_connections = []
        for connection in self.project.connections:
            if connection.source_kind == "container":
                if connection.source_name in removed_names:
                    continue
                connection.source_name = rename_map.get(connection.source_name, connection.source_name)
            elif connection.source_name in removed_nodes_by_kind.get(connection.source_kind, set()):
                continue
            if connection.target_kind == "container":
                if connection.target_name in removed_names:
                    continue
                connection.target_name = rename_map.get(connection.target_name, connection.target_name)
            elif connection.target_name in removed_nodes_by_kind.get(connection.target_kind, set()):
                continue
            next_connections.append(connection)
        self.project.connections = next_connections
        self._rewrite_selection_state_after_container_change(removed_names, rename_map)

    def _delete_container_by_name(
        self,
        name: str,
        *,
        refresh: bool = True,
        log_message: bool = True,
    ) -> None:
        container = self._find_container(name)
        if not container:
            raise AssertionError(f"Missing container {name}")
        rename_map: dict[str, str] = {}
        standard_info = self._parse_standard_container_name(container.name)
        if standard_info is not None:
            prefix, deleted_index = standard_info
            for other in self.project.containers:
                if other.name == container.name:
                    continue
                other_info = self._parse_standard_container_name(other.name)
                if other_info is None:
                    continue
                other_prefix, other_index = other_info
                if other_prefix != prefix or other_index <= deleted_index:
                    continue
                rename_map[other.name] = f"{prefix}{other_index - 1}"
        self.project.containers = [item for item in self.project.containers if item.name != container.name]
        self._apply_container_reference_transaction(
            removed_names={container.name},
            rename_map=rename_map,
        )
        self._rename_remaining_container_objects(rename_map)
        self._sync_all_container_groups()
        if refresh:
            self._refresh_all()
        if log_message:
            self._log(f"Deleted container {container.name}.")

    def _delete_containers_by_name(self, names: list[str]) -> None:
        normalized_names: list[str] = []
        for name in names:
            normalized = str(name).strip()
            if not normalized or normalized in normalized_names:
                continue
            if self._find_container(normalized) is None:
                continue
            normalized_names.append(normalized)
        if not normalized_names:
            self._log("No containers were eligible for deletion.")
            return

        def _sort_key(value: str) -> tuple[int, str, int]:
            parsed = self._parse_standard_container_name(value)
            if parsed is None:
                return (1, value, -1)
            prefix, index = parsed
            return (0, prefix, -index)

        for name in sorted(normalized_names, key=_sort_key):
            if self._find_container(name) is None:
                continue
            self._delete_container_by_name(name, refresh=False, log_message=False)
        self._refresh_all()
        self._log(f"Deleted {len(normalized_names)} container(s).")

    def _insert_standard_container_before(self, source_name: str, target_name: str) -> None:
        source = self._find_container(source_name)
        target = self._find_container(target_name)
        if source is None or target is None:
            raise AssertionError(f"Insert requires existing containers: {source_name}, {target_name}")
        if source.name == target.name:
            self._log(f"Skipped insert because {source.name} is already in the requested slot.")
            return
        if source.kind != target.kind:
            raise AssertionError("Insert requires both containers to share the same kind.")
        source_info = self._parse_standard_container_name(source.name)
        target_info = self._parse_standard_container_name(target.name)
        if source_info is None or target_info is None:
            raise AssertionError("Insert only supports standard vN/aN containers.")
        source_prefix, _source_index = source_info
        target_prefix, _target_index = target_info
        if source_prefix != target_prefix:
            raise AssertionError("Insert only supports containers from the same standard sequence.")
        ordered_names = [
            item.name
            for item in sorted(
                (
                    container
                    for container in self.project.containers
                    if self._parse_standard_container_name(container.name) is not None
                    and self._parse_standard_container_name(container.name)[0] == source_prefix
                ),
                key=lambda item: self._parse_standard_container_name(item.name)[1],
            )
        ]
        if source.name not in ordered_names or target.name not in ordered_names:
            raise AssertionError("Insert sequence is missing one of the requested containers.")
        ordered_names.remove(source.name)
        target_position = ordered_names.index(target.name)
        ordered_names.insert(target_position, source.name)
        rename_map: dict[str, str] = {}
        for index, current_name in enumerate(ordered_names, start=1):
            final_name = f"{source_prefix}{index}"
            if current_name != final_name:
                rename_map[current_name] = final_name
        self._apply_container_reference_transaction(
            removed_names=set(),
            rename_map=rename_map,
        )
        self._rename_remaining_container_objects(rename_map)
        self.selected_container_name = rename_map.get(source.name, source.name)
        self._sync_all_container_groups()
        self._refresh_all()
        self._log(f"Inserted {source.name} before {target.name}.")
