#pragma once

#include "common_data/common_data.h"

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <string>
#include <unordered_map>
#include <vector>

namespace algorithm {

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
  std::unordered_map<std::string, std::vector<AlgorithmReflectionBinding>> reflection_objects_by_container_name;
  std::unordered_map<std::string, AlgorithmReflectionBinding> container_bindings_by_reflection_object_name;

  bool empty() const {
    return reflection_objects_by_container_name.empty() &&
      container_bindings_by_reflection_object_name.empty();
  }

  void Clear() {
    algorithm_name.clear();
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

enum class AlgorithmContainerStorageKind {
  Array,
  TemporaryRegister,
  TemporaryCache,
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
};

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
  return nullptr;
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
