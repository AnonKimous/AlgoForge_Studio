#pragma once

#include "common_data/common_data.h"

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <string>
#include <unordered_map>
#include <vector>

namespace algorithm {

enum class AlgorithmReflectionRefreshMode {
  EveryTick = 0,
  CaptureOnceAfterCompletion = 1,
};

enum class AlgorithmPipelineTickMode {
  NonCircular = 0,
  Circular = 1,
};

enum class AlgorithmContainerStorageKind {
  Array,
  TemporaryRegister,
  TemporaryCache,
};

struct AlgorithmProfile {
  std::string algorithm_name;
  std::string container_manifest_name;
};

struct AlgorithmReflectionBinding {
  std::vector<std::string> container_names;
  std::string reflection_object_name;
  std::string filter_name;
};

struct AlgorithmReflector {
  std::string algorithm_name;
  AlgorithmReflectionRefreshMode refresh_mode{AlgorithmReflectionRefreshMode::EveryTick};
  std::unordered_map<std::string, std::vector<AlgorithmReflectionBinding>> reflection_objects_by_container_name;
  std::unordered_map<std::string, AlgorithmReflectionBinding> container_bindings_by_reflection_object_name;

  bool empty() const {
    return reflection_objects_by_container_name.empty() &&
      container_bindings_by_reflection_object_name.empty();
  }

  void Clear() {
    algorithm_name.clear();
    refresh_mode = AlgorithmReflectionRefreshMode::EveryTick;
    reflection_objects_by_container_name.clear();
    container_bindings_by_reflection_object_name.clear();
  }

  std::vector<const AlgorithmReflectionBinding*> FilterReflectionObjects(
    const std::string& container_name,
    const std::string& filter_name = {}) const {
    std::vector<const AlgorithmReflectionBinding*> filtered;
    const auto found = reflection_objects_by_container_name.find(container_name);
    if (found == reflection_objects_by_container_name.end()) {
      return filtered;
    }

    filtered.reserve(found->second.size());
    for (const AlgorithmReflectionBinding& binding : found->second) {
      if (!filter_name.empty() && binding.filter_name != filter_name) {
        continue;
      }
      filtered.push_back(&binding);
    }
    return filtered;
  }

  const AlgorithmReflectionBinding* FindContainerBindingForReflectionObject(
    const std::string& reflection_object_name) const {
    const auto found = container_bindings_by_reflection_object_name.find(reflection_object_name);
    return found == container_bindings_by_reflection_object_name.end()
      ? nullptr
      : &found->second;
  }
};

struct AlgorithmRuntimeTransferBinding {
  std::string from_name;
  std::string to_name;
  bool required{true};
};

struct AlgorithmRuntimeTransferEdge {
  std::string source_stage_name;
  std::string target_stage_name;
  std::vector<AlgorithmRuntimeTransferBinding> bindings;
};

struct AlgorithmRuntimeTransferStageLayout {
  std::string stage_name;
  uint32_t declared_variable_count{0u};
  uint32_t declared_array_count{0u};
  uint32_t shared_variable_count{0u};
  uint32_t shared_array_count{0u};
  uint32_t extra_variable_count{0u};
  uint32_t extra_array_count{0u};
  uint32_t extra_variable_offset{0u};
  uint32_t extra_array_offset{0u};
};

struct AlgorithmRuntimeTransferMap {
  std::string algorithm_name;
  // Linear pipeline map: each stage may have at most one successor and one predecessor.
  std::vector<AlgorithmRuntimeTransferEdge> stage_links;
  std::vector<AlgorithmRuntimeTransferStageLayout> stage_layouts;
  uint32_t pipeline_shared_variable_count{0u};
  uint32_t pipeline_shared_array_count{0u};
  uint32_t pipeline_total_extra_variable_count{0u};
  uint32_t pipeline_total_extra_array_count{0u};
  std::string pipeline_shared_stage_buffer_slot_name;
  bool supports_circular_tick{false};
  bool valid{false};

  bool empty() const {
    return stage_links.empty();
  }

  void Clear() {
    algorithm_name.clear();
    stage_links.clear();
    stage_layouts.clear();
    pipeline_shared_variable_count = 0u;
    pipeline_shared_array_count = 0u;
    pipeline_total_extra_variable_count = 0u;
    pipeline_total_extra_array_count = 0u;
    pipeline_shared_stage_buffer_slot_name.clear();
    supports_circular_tick = false;
    valid = false;
  }

  bool SupportsCircularTick() const {
    return supports_circular_tick;
  }

  const AlgorithmRuntimeTransferEdge* FindEdge(
    const std::string& source_stage_name,
    const std::string& target_stage_name) const {
    for (const AlgorithmRuntimeTransferEdge& edge : stage_links) {
      if (edge.source_stage_name == source_stage_name && edge.target_stage_name == target_stage_name) {
        return &edge;
      }
    }
    return nullptr;
  }

  const AlgorithmRuntimeTransferStageLayout* FindStageLayout(
    const std::string& stage_name) const {
    for (const AlgorithmRuntimeTransferStageLayout& stage_layout : stage_layouts) {
      if (stage_layout.stage_name == stage_name) {
        return &stage_layout;
      }
    }
    return nullptr;
  }

  std::vector<const AlgorithmRuntimeTransferEdge*> FindOutgoingEdges(
    const std::string& stage_name) const {
    std::vector<const AlgorithmRuntimeTransferEdge*> edges;
    for (const AlgorithmRuntimeTransferEdge& edge : stage_links) {
      if (edge.source_stage_name == stage_name) {
        edges.push_back(&edge);
      }
    }
    return edges;
  }

  std::vector<const AlgorithmRuntimeTransferEdge*> FindIncomingEdges(
    const std::string& stage_name) const {
    std::vector<const AlgorithmRuntimeTransferEdge*> edges;
    for (const AlgorithmRuntimeTransferEdge& edge : stage_links) {
      if (edge.target_stage_name == stage_name) {
        edges.push_back(&edge);
      }
    }
    return edges;
  }
};

struct AlgorithmStandardContainerLayout {
  std::string layout_name;
  std::string layout_kind;
  uint32_t variable_count{0u};
  uint32_t array_count{0u};
  std::string variable_prefix{"v"};
  std::string array_prefix{"a"};

  bool enabled() const {
    return !layout_name.empty() && (variable_count > 0u || array_count > 0u);
  }

  std::string MakeVariableName(uint32_t index) const {
    return variable_prefix + std::to_string(index + 1u);
  }

  std::string MakeArrayName(uint32_t index) const {
    return array_prefix + std::to_string(index + 1u);
  }

  bool HasMandatoryPipelineStageBuffer() const {
    return array_count > 0u;
  }

  std::string MakeMandatoryPipelineStageBufferName() const {
    return HasMandatoryPipelineStageBuffer()
      ? MakeArrayName(array_count - 1u)
      : std::string{};
  }

  bool TryResolveContainerName(
    const std::string& container_name,
    AlgorithmContainerStorageKind* out_storage_kind = nullptr,
    uint32_t* out_index = nullptr) const {
    const auto try_prefix = [&](const std::string& prefix, AlgorithmContainerStorageKind storage_kind) {
      if (container_name.size() <= prefix.size() ||
          container_name.compare(0, prefix.size(), prefix) != 0) {
        return false;
      }

      uint64_t index = 0u;
      for (size_t i = prefix.size(); i < container_name.size(); ++i) {
        const char ch = container_name[i];
        if (ch < '0' || ch > '9') {
          return false;
        }
        index = index * 10u + static_cast<uint64_t>(ch - '0');
        if (index > UINT32_MAX) {
          return false;
        }
      }
      if (index == 0u) {
        return false;
      }

      if (out_storage_kind) {
        *out_storage_kind = storage_kind;
      }
      if (out_index) {
        *out_index = static_cast<uint32_t>(index - 1u);
      }
      return true;
    };

    if (try_prefix(variable_prefix, AlgorithmContainerStorageKind::TemporaryRegister)) {
      return true;
    }
    if (try_prefix(array_prefix, AlgorithmContainerStorageKind::Array)) {
      return true;
    }
    return false;
  }
};

struct AlgorithmContainer {
  std::string name;
  AlgorithmContainerStorageKind storage_kind{AlgorithmContainerStorageKind::Array};
  uint32_t element_count{};
  uint32_t element_stride{};
  bool hidden{false};
  std::pmr::vector<std::byte> bytes;
};

struct AlgorithmContainerSet {
  std::string algorithm_name;
  AlgorithmStandardContainerLayout standard_layout{};
  std::vector<AlgorithmContainer> arrays;
  std::vector<AlgorithmContainer> temporary_registers;
  std::vector<AlgorithmContainer> temporary_caches;
  std::vector<AlgorithmContainer> hidden_containers;
  std::unordered_map<std::string, std::vector<std::string>> container_aliases_by_name;
};

inline void CopyAlgorithmContainer(
  const AlgorithmContainer& source_container,
  AlgorithmContainer* out_target_container) {
  if (!out_target_container) {
    return;
  }
  out_target_container->name = source_container.name;
  out_target_container->storage_kind = source_container.storage_kind;
  out_target_container->element_count = source_container.element_count;
  out_target_container->element_stride = source_container.element_stride;
  out_target_container->hidden = source_container.hidden;
  out_target_container->bytes.assign(source_container.bytes.begin(), source_container.bytes.end());
}

inline void CopyAlgorithmContainerSet(
  const AlgorithmContainerSet& source_container_set,
  AlgorithmContainerSet* out_target_container_set) {
  if (!out_target_container_set) {
    return;
  }

  out_target_container_set->algorithm_name = source_container_set.algorithm_name;
  out_target_container_set->standard_layout = source_container_set.standard_layout;

  out_target_container_set->arrays.clear();
  out_target_container_set->arrays.reserve(source_container_set.arrays.size());
  for (const AlgorithmContainer& container : source_container_set.arrays) {
    AlgorithmContainer copied_container{};
    CopyAlgorithmContainer(container, &copied_container);
    out_target_container_set->arrays.push_back(std::move(copied_container));
  }

  out_target_container_set->temporary_registers.clear();
  out_target_container_set->temporary_registers.reserve(source_container_set.temporary_registers.size());
  for (const AlgorithmContainer& container : source_container_set.temporary_registers) {
    AlgorithmContainer copied_container{};
    CopyAlgorithmContainer(container, &copied_container);
    out_target_container_set->temporary_registers.push_back(std::move(copied_container));
  }

  out_target_container_set->temporary_caches.clear();
  out_target_container_set->temporary_caches.reserve(source_container_set.temporary_caches.size());
  for (const AlgorithmContainer& container : source_container_set.temporary_caches) {
    AlgorithmContainer copied_container{};
    CopyAlgorithmContainer(container, &copied_container);
    out_target_container_set->temporary_caches.push_back(std::move(copied_container));
  }

  out_target_container_set->hidden_containers.clear();
  out_target_container_set->hidden_containers.reserve(source_container_set.hidden_containers.size());
  for (const AlgorithmContainer& container : source_container_set.hidden_containers) {
    AlgorithmContainer copied_container{};
    CopyAlgorithmContainer(container, &copied_container);
    out_target_container_set->hidden_containers.push_back(std::move(copied_container));
  }

  out_target_container_set->container_aliases_by_name = source_container_set.container_aliases_by_name;
}

inline bool HasDeclaredAlgorithmContainerName(
  const AlgorithmContainerSet& container_set,
  const std::string& container_name) {
  for (const AlgorithmContainer& container : container_set.arrays) {
    if (container.name == container_name) return true;
  }
  for (const AlgorithmContainer& container : container_set.temporary_registers) {
    if (container.name == container_name) return true;
  }
  for (const AlgorithmContainer& container : container_set.temporary_caches) {
    if (container.name == container_name) return true;
  }
  return false;
}

inline bool TryResolveAlgorithmContainerReferenceNames(
  const AlgorithmContainerSet& container_set,
  const std::string& container_name,
  std::vector<std::string>* out_container_names) {
  if (!out_container_names) {
    return false;
  }
  out_container_names->clear();
  if (container_name.empty()) {
    return false;
  }

  if (HasDeclaredAlgorithmContainerName(container_set, container_name)) {
    out_container_names->push_back(container_name);
    return true;
  }

  AlgorithmContainerStorageKind storage_kind{};
  uint32_t index{};
  if (container_set.standard_layout.enabled() &&
      container_set.standard_layout.TryResolveContainerName(container_name, &storage_kind, &index)) {
    out_container_names->push_back(container_name);
    return true;
  }

  const auto alias_found = container_set.container_aliases_by_name.find(container_name);
  if (alias_found == container_set.container_aliases_by_name.end()) {
    return false;
  }

  *out_container_names = alias_found->second;
  return !out_container_names->empty();
}

inline bool TryResolveSingleAlgorithmContainerReferenceName(
  const AlgorithmContainerSet& container_set,
  const std::string& container_name,
  std::string* out_container_name) {
  if (!out_container_name) {
    return false;
  }
  out_container_name->clear();
  std::vector<std::string> resolved_names{};
  if (!TryResolveAlgorithmContainerReferenceNames(container_set, container_name, &resolved_names) ||
      resolved_names.size() != 1u) {
    return false;
  }
  *out_container_name = resolved_names.front();
  return true;
}

inline bool IsStandardContainerSlotName(
  const AlgorithmContainerSet& container_set,
  const std::string& container_name) {
  if (!container_set.standard_layout.enabled()) {
    return false;
  }
  AlgorithmContainerStorageKind storage_kind{};
  uint32_t index{};
  return container_set.standard_layout.TryResolveContainerName(container_name, &storage_kind, &index);
}

inline bool HasSameContainerStructure(
  const AlgorithmContainer& source_container,
  const AlgorithmContainer& target_container) {
  return source_container.storage_kind == target_container.storage_kind &&
    source_container.element_count == target_container.element_count &&
    source_container.element_stride == target_container.element_stride &&
    source_container.bytes.size() == target_container.bytes.size();
}

inline AlgorithmContainer* FindAlgorithmContainer(
  AlgorithmContainerSet* container_set,
  const std::string& name) {
  if (!container_set) return nullptr;
  for (AlgorithmContainer& container : container_set->arrays) {
    if (container.name == name) return &container;
  }
  for (AlgorithmContainer& container : container_set->temporary_registers) {
    if (container.name == name) return &container;
  }
  for (AlgorithmContainer& container : container_set->temporary_caches) {
    if (container.name == name) return &container;
  }
  std::string resolved_name{};
  if (TryResolveSingleAlgorithmContainerReferenceName(*container_set, name, &resolved_name) &&
      resolved_name != name) {
    return FindAlgorithmContainer(container_set, resolved_name);
  }
  AlgorithmContainerStorageKind storage_kind{};
  uint32_t index{};
  if (!container_set->standard_layout.enabled() ||
      !container_set->standard_layout.TryResolveContainerName(name, &storage_kind, &index)) {
    return nullptr;
  }
  switch (storage_kind) {
    case AlgorithmContainerStorageKind::Array:
      return index < container_set->arrays.size() ? &container_set->arrays[index] : nullptr;
    case AlgorithmContainerStorageKind::TemporaryRegister:
      return index < container_set->temporary_registers.size() ? &container_set->temporary_registers[index] : nullptr;
    case AlgorithmContainerStorageKind::TemporaryCache:
      return index < container_set->temporary_caches.size() ? &container_set->temporary_caches[index] : nullptr;
  }
  return nullptr;
}

inline const AlgorithmContainer* FindAlgorithmContainer(
  const AlgorithmContainerSet& container_set,
  const std::string& name) {
  for (const AlgorithmContainer& container : container_set.arrays) {
    if (container.name == name) return &container;
  }
  for (const AlgorithmContainer& container : container_set.temporary_registers) {
    if (container.name == name) return &container;
  }
  for (const AlgorithmContainer& container : container_set.temporary_caches) {
    if (container.name == name) return &container;
  }
  std::string resolved_name{};
  if (TryResolveSingleAlgorithmContainerReferenceName(container_set, name, &resolved_name) &&
      resolved_name != name) {
    return FindAlgorithmContainer(container_set, resolved_name);
  }
  AlgorithmContainerStorageKind storage_kind{};
  uint32_t index{};
  if (!container_set.standard_layout.enabled() ||
      !container_set.standard_layout.TryResolveContainerName(name, &storage_kind, &index)) {
    return nullptr;
  }
  switch (storage_kind) {
    case AlgorithmContainerStorageKind::Array:
      return index < container_set.arrays.size() ? &container_set.arrays[index] : nullptr;
    case AlgorithmContainerStorageKind::TemporaryRegister:
      return index < container_set.temporary_registers.size() ? &container_set.temporary_registers[index] : nullptr;
    case AlgorithmContainerStorageKind::TemporaryCache:
      return index < container_set.temporary_caches.size() ? &container_set.temporary_caches[index] : nullptr;
  }
  return nullptr;
}

inline bool HasMandatoryPipelineStageBuffer(
  const AlgorithmContainerSet& container_set) {
  return container_set.standard_layout.HasMandatoryPipelineStageBuffer() &&
    FindAlgorithmContainer(
      container_set,
      container_set.standard_layout.MakeMandatoryPipelineStageBufferName()) != nullptr;
}

inline AlgorithmContainer* FindMandatoryPipelineStageBuffer(
  AlgorithmContainerSet* container_set) {
  if (!container_set || !container_set->standard_layout.HasMandatoryPipelineStageBuffer()) {
    return nullptr;
  }
  return FindAlgorithmContainer(
    container_set,
    container_set->standard_layout.MakeMandatoryPipelineStageBufferName());
}

inline const AlgorithmContainer* FindMandatoryPipelineStageBuffer(
  const AlgorithmContainerSet& container_set) {
  if (!container_set.standard_layout.HasMandatoryPipelineStageBuffer()) {
    return nullptr;
  }
  return FindAlgorithmContainer(
    container_set,
    container_set.standard_layout.MakeMandatoryPipelineStageBufferName());
}

inline AlgorithmContainer* FindAlgorithmHiddenContainer(
  AlgorithmContainerSet* container_set,
  const std::string& name) {
  if (!container_set) return nullptr;
  for (AlgorithmContainer& container : container_set->hidden_containers) {
    if (container.name == name) return &container;
  }
  return nullptr;
}

inline const AlgorithmContainer* FindAlgorithmHiddenContainer(
  const AlgorithmContainerSet& container_set,
  const std::string& name) {
  for (const AlgorithmContainer& container : container_set.hidden_containers) {
    if (container.name == name) return &container;
  }
  return nullptr;
}

}  // namespace algorithm
