#include "algorithm_support/algorithm_protocol.h"
#include "algorithm_support/algorithm_package_location.h"
#include "algorithm_support/algorithm_package_paths.h"
#include "algorithm_support/algorithm_types.h"
#include "algorithm_support/algorithm_json_utils.h"

#include "cJSON.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iterator>
#include <memory>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace algorithm_support {

namespace {

namespace fs = std::filesystem;

bool _LoadPackageRuntimeLifetime(
  const algorithm::AlgorithmPackageLocation& package_location,
  algorithm_management::AlgorithmTickLifetime* out_tick_lifetime,
  std::string* out_error_message) {
  if (!out_tick_lifetime) {
    if (out_error_message) {
      *out_error_message = "AlgorithmTickLifetime output pointer is null.";
    }
    return false;
  }

  *out_tick_lifetime = algorithm_management::AlgorithmTickLifetime::Continuous;

  const std::string algorithm_name = package_location.algorithm_name.empty()
    ? package_location.manifest_name
    : package_location.algorithm_name;
  const fs::path package_json_path = algorithm::package_paths::ResolvePackageJsonPath(
    package_location.package_root,
    package_location.manifest_path,
    algorithm_name);
  if (package_json_path.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to resolve package JSON file for runtime lifetime.";
    }
    return false;
  }

  const std::string json_text = json_utils::ReadTextFile(package_json_path);
  if (json_text.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to read package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    if (out_error_message) {
      *out_error_message = "Failed to parse package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  const cJSON* runtime = cJSON_GetObjectItemCaseSensitive(root, "runtime");
  if (runtime) {
    if (!cJSON_IsObject(runtime)) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package runtime section is invalid: " + package_json_path.generic_string();
      }
      return false;
    }

    const cJSON* launch_once = cJSON_GetObjectItemCaseSensitive(runtime, "launchOnce");
    if (launch_once) {
      if (!cJSON_IsBool(launch_once)) {
        cJSON_Delete(root);
        if (out_error_message) {
          *out_error_message = "Package runtime launchOnce must be boolean: " + package_json_path.generic_string();
        }
        return false;
      }
      if (cJSON_IsTrue(launch_once)) {
        *out_tick_lifetime = algorithm_management::AlgorithmTickLifetime::LaunchOnceThenHold;
      }
    }
  }

  cJSON_Delete(root);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

std::string _TrimRuntimeToken(std::string value) {
  const auto is_space = [](unsigned char ch) {
    return std::isspace(ch) != 0;
  };
  const auto begin = std::find_if_not(value.begin(), value.end(), is_space);
  const auto end = std::find_if_not(value.rbegin(), value.rend(), is_space).base();
  if (begin >= end) {
    return {};
  }
  return std::string(begin, end);
}

bool _ParseRuntimeStageLinkKey(
  const std::string& key,
  std::string* out_source_stage_name,
  std::string* out_target_stage_name,
  std::string* out_error_message) {
  if (!out_source_stage_name || !out_target_stage_name) {
    if (out_error_message) {
      *out_error_message = "Runtime stage link output pointers are null.";
    }
    return false;
  }

  const std::string::size_type arrow = key.find("->");
  if (arrow == std::string::npos || key.find("->", arrow + 2u) != std::string::npos) {
    if (out_error_message) {
      *out_error_message = "Runtime stage link key must use the form source->target: " + key;
    }
    return false;
  }

  *out_source_stage_name = _TrimRuntimeToken(key.substr(0, arrow));
  *out_target_stage_name = _TrimRuntimeToken(key.substr(arrow + 2u));
  if (out_source_stage_name->empty() || out_target_stage_name->empty()) {
    if (out_error_message) {
      *out_error_message = "Runtime stage link key contains an empty stage name: " + key;
    }
    return false;
  }
  return true;
}

bool _AppendRuntimeTransferBinding(
  algorithm::AlgorithmRuntimeTransferEdge* out_edge,
  algorithm::AlgorithmRuntimeTransferBinding binding,
  std::string* out_error_message) {
  if (!out_edge) {
    if (out_error_message) {
      *out_error_message = "Runtime transfer edge output pointer is null.";
    }
    return false;
  }
  if (binding.from_name.empty() || binding.to_name.empty()) {
    if (out_error_message) {
      *out_error_message = "Runtime transfer binding is missing a source or target name.";
    }
    return false;
  }
  for (const algorithm::AlgorithmRuntimeTransferBinding& existing : out_edge->bindings) {
    if (existing.from_name == binding.from_name) {
      if (out_error_message) {
        *out_error_message = "Runtime transfer binding source is duplicated: " + binding.from_name;
      }
      return false;
    }
    if (existing.to_name == binding.to_name) {
      if (out_error_message) {
        *out_error_message = "Runtime transfer binding target is duplicated: " + binding.to_name;
      }
      return false;
    }
  }
  out_edge->bindings.push_back(std::move(binding));
  return true;
}

bool _ValidateRuntimeTransferEdgeLinearity(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const algorithm::AlgorithmRuntimeTransferEdge& edge,
  const std::string& package_path,
  std::string* out_error_message) {
  if (edge.source_stage_name == edge.target_stage_name) {
    if (out_error_message) {
      *out_error_message = "Runtime transfer edge cannot loop to itself: " + edge.source_stage_name +
        "->" + edge.target_stage_name + " in " + package_path;
    }
    return false;
  }

  for (const algorithm::AlgorithmRuntimeTransferEdge& existing : transfer_map.stage_links) {
    if (existing.source_stage_name == edge.source_stage_name &&
        existing.target_stage_name == edge.target_stage_name) {
      if (out_error_message) {
        *out_error_message = "Runtime transfer edge is duplicated: " +
          edge.source_stage_name + "->" + edge.target_stage_name;
      }
      return false;
    }
    if (existing.source_stage_name == edge.source_stage_name) {
      if (out_error_message) {
        *out_error_message = "Runtime transfer map must be linear: stage '" + edge.source_stage_name +
          "' already maps to next stage '" + existing.target_stage_name + "' in " + package_path;
      }
      return false;
    }
    if (existing.target_stage_name == edge.target_stage_name) {
      if (out_error_message) {
        *out_error_message = "Runtime transfer map must be linear: stage '" + edge.target_stage_name +
          "' already receives data from '" + existing.source_stage_name + "' in " + package_path;
      }
      return false;
    }
  }

  return true;
}

bool _LoadRuntimeTransferBindingsFromObject(
  const cJSON* bindings_object,
  const std::string& package_path,
  algorithm::AlgorithmRuntimeTransferEdge* out_edge,
  std::string* out_error_message) {
  if (!bindings_object || !cJSON_IsObject(bindings_object) || !out_edge) {
    if (out_error_message) {
      *out_error_message = "Runtime transfer bindings object is invalid: " + package_path;
    }
    return false;
  }

  for (const cJSON* child = bindings_object->child; child; child = child->next) {
    if (!child->string || !*child->string) {
      if (out_error_message) {
        *out_error_message = "Runtime transfer binding source name is empty: " + package_path;
      }
      return false;
    }

    algorithm::AlgorithmRuntimeTransferBinding binding{};
    binding.from_name = child->string;
    if (cJSON_IsString(child) && child->valuestring) {
      binding.to_name = child->valuestring;
    } else if (cJSON_IsObject(child)) {
      binding.to_name = json_utils::GetStringField(child, "to");
      if (binding.to_name.empty()) {
        binding.to_name = json_utils::GetStringField(child, "target");
      }
      binding.required = json_utils::GetBoolField(child, "required", true);
    } else {
      if (out_error_message) {
        *out_error_message = "Runtime transfer binding value is invalid: " + package_path;
      }
      return false;
    }

    if (!_AppendRuntimeTransferBinding(out_edge, std::move(binding), out_error_message)) {
      return false;
    }
  }

  return true;
}

bool _LoadRuntimeTransferBindingsFromArray(
  const cJSON* bindings_array,
  const std::string& package_path,
  algorithm::AlgorithmRuntimeTransferEdge* out_edge,
  std::string* out_error_message) {
  if (!bindings_array || !cJSON_IsArray(bindings_array) || !out_edge) {
    if (out_error_message) {
      *out_error_message = "Runtime transfer bindings array is invalid: " + package_path;
    }
    return false;
  }

  const int binding_count = cJSON_GetArraySize(bindings_array);
  for (int i = 0; i < binding_count; ++i) {
    const cJSON* item = cJSON_GetArrayItem(bindings_array, i);
    if (!item || !cJSON_IsObject(item)) {
      if (out_error_message) {
        *out_error_message = "Runtime transfer binding item is invalid: " + package_path;
      }
      return false;
    }

    algorithm::AlgorithmRuntimeTransferBinding binding{};
    binding.from_name = json_utils::GetStringField(item, "from");
    if (binding.from_name.empty()) {
      binding.from_name = json_utils::GetStringField(item, "source");
    }
    binding.to_name = json_utils::GetStringField(item, "to");
    if (binding.to_name.empty()) {
      binding.to_name = json_utils::GetStringField(item, "target");
    }
    binding.required = json_utils::GetBoolField(item, "required", true);
    if (!_AppendRuntimeTransferBinding(out_edge, std::move(binding), out_error_message)) {
      return false;
    }
  }

  return true;
}

bool _LoadRuntimeTransferEdgeFromObject(
  const cJSON* item,
  const std::string& package_path,
  algorithm::AlgorithmRuntimeTransferMap* out_transfer_map,
  std::string* out_error_message) {
  if (!item || !cJSON_IsObject(item) || !out_transfer_map) {
    if (out_error_message) {
      *out_error_message = "Runtime transfer edge item is invalid: " + package_path;
    }
    return false;
  }

  algorithm::AlgorithmRuntimeTransferEdge edge{};
  edge.source_stage_name = json_utils::GetStringField(item, "from_stage");
  if (edge.source_stage_name.empty()) {
    edge.source_stage_name = json_utils::GetStringField(item, "source_stage");
  }
  edge.target_stage_name = json_utils::GetStringField(item, "to_stage");
  if (edge.target_stage_name.empty()) {
    edge.target_stage_name = json_utils::GetStringField(item, "target_stage");
  }
  if (edge.source_stage_name.empty() || edge.target_stage_name.empty()) {
    if (out_error_message) {
      *out_error_message = "Runtime transfer edge is missing source or target stage: " + package_path;
    }
    return false;
  }

  const cJSON* bindings = cJSON_GetObjectItemCaseSensitive(item, "bindings");
  if (!bindings) {
    bindings = cJSON_GetObjectItemCaseSensitive(item, "map");
  }
  if (!bindings) {
    bindings = cJSON_GetObjectItemCaseSensitive(item, "links");
  }
  if (bindings) {
    if (cJSON_IsObject(bindings)) {
      if (!_LoadRuntimeTransferBindingsFromObject(bindings, package_path, &edge, out_error_message)) {
        return false;
      }
    } else if (cJSON_IsArray(bindings)) {
      if (!_LoadRuntimeTransferBindingsFromArray(bindings, package_path, &edge, out_error_message)) {
        return false;
      }
    } else {
      if (out_error_message) {
        *out_error_message = "Runtime transfer edge bindings have an invalid type: " + package_path;
      }
      return false;
    }
  }

  if (!_ValidateRuntimeTransferEdgeLinearity(*out_transfer_map, edge, package_path, out_error_message)) {
    return false;
  }

  out_transfer_map->stage_links.push_back(std::move(edge));
  return true;
}

bool _LoadRuntimeTransferEdgeFromKeyValue(
  const std::string& edge_key,
  const cJSON* value,
  const std::string& package_path,
  algorithm::AlgorithmRuntimeTransferMap* out_transfer_map,
  std::string* out_error_message) {
  if (!out_transfer_map) {
    if (out_error_message) {
      *out_error_message = "Runtime transfer map output pointer is null.";
    }
    return false;
  }

  std::string source_stage_name;
  std::string target_stage_name;
  if (!_ParseRuntimeStageLinkKey(edge_key, &source_stage_name, &target_stage_name, out_error_message)) {
    return false;
  }

  algorithm::AlgorithmRuntimeTransferEdge edge{};
  edge.source_stage_name = std::move(source_stage_name);
  edge.target_stage_name = std::move(target_stage_name);
  if (value) {
    if (cJSON_IsObject(value)) {
      if (!_LoadRuntimeTransferBindingsFromObject(value, package_path, &edge, out_error_message)) {
        return false;
      }
    } else if (cJSON_IsArray(value)) {
      if (!_LoadRuntimeTransferBindingsFromArray(value, package_path, &edge, out_error_message)) {
        return false;
      }
    } else {
      if (out_error_message) {
        *out_error_message = "Runtime transfer edge payload is invalid: " + package_path;
      }
      return false;
    }
  }

  if (!_ValidateRuntimeTransferEdgeLinearity(*out_transfer_map, edge, package_path, out_error_message)) {
    return false;
  }

  out_transfer_map->stage_links.push_back(std::move(edge));
  return true;
}

bool _LoadPackageRuntimeTransferMap(
  const algorithm::AlgorithmPackageLocation& package_location,
  algorithm::AlgorithmRuntimeTransferMap* out_transfer_map,
  bool* out_has_transfer_map,
  std::string* out_error_message) {
  if (!out_transfer_map) {
    if (out_error_message) {
      *out_error_message = "AlgorithmRuntimeTransferMap output pointer is null.";
    }
    return false;
  }

  out_transfer_map->Clear();
  if (out_has_transfer_map) {
    *out_has_transfer_map = false;
  }

  const std::string algorithm_name = package_location.algorithm_name.empty()
    ? package_location.manifest_name
    : package_location.algorithm_name;
  const fs::path package_json_path = algorithm::package_paths::ResolvePackageJsonPath(
    package_location.package_root,
    package_location.manifest_path,
    algorithm_name);
  if (package_json_path.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to resolve package JSON file for runtime transfer map.";
    }
    return false;
  }

  const std::string json_text = json_utils::ReadTextFile(package_json_path);
  if (json_text.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to read package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    if (out_error_message) {
      *out_error_message = "Failed to parse package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  const cJSON* runtime = cJSON_GetObjectItemCaseSensitive(root, "runtime");
  if (!runtime) {
    cJSON_Delete(root);
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }
  if (!cJSON_IsObject(runtime)) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Package runtime section is invalid: " + package_json_path.generic_string();
    }
    return false;
  }

  const cJSON* pipeline = cJSON_GetObjectItemCaseSensitive(runtime, "pipeline");
  if (!pipeline) {
    cJSON_Delete(root);
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }
  if (!cJSON_IsObject(pipeline)) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Package runtime pipeline section is invalid: " + package_json_path.generic_string();
    }
    return false;
  }

  const cJSON* mappings = cJSON_GetObjectItemCaseSensitive(pipeline, "mappings");
  if (!mappings) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Package runtime pipeline is missing mappings: " + package_json_path.generic_string();
    }
    return false;
  }

  out_transfer_map->algorithm_name = package_location.algorithm_name;

  if (cJSON_IsObject(mappings)) {
    for (const cJSON* child = mappings->child; child; child = child->next) {
      if (!child->string || !*child->string) {
        cJSON_Delete(root);
        if (out_error_message) {
          *out_error_message = "Runtime transfer mapping key is empty: " + package_json_path.generic_string();
        }
        return false;
      }
      if (!_LoadRuntimeTransferEdgeFromKeyValue(
            child->string,
            child,
            package_json_path.generic_string(),
            out_transfer_map,
            out_error_message)) {
        cJSON_Delete(root);
        return false;
      }
    }
  } else if (cJSON_IsArray(mappings)) {
    const int mapping_count = cJSON_GetArraySize(mappings);
    for (int i = 0; i < mapping_count; ++i) {
      const cJSON* item = cJSON_GetArrayItem(mappings, i);
      if (!_LoadRuntimeTransferEdgeFromObject(
            item,
            package_json_path.generic_string(),
            out_transfer_map,
            out_error_message)) {
        cJSON_Delete(root);
        return false;
      }
    }
  } else {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Package runtime pipeline mappings are invalid: " + package_json_path.generic_string();
    }
    return false;
  }

  cJSON_Delete(root);
  out_transfer_map->valid = true;
  if (out_has_transfer_map) {
    *out_has_transfer_map = true;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _AppendContainer(
  const std::string& container_name,
  algorithm::AlgorithmContainerStorageKind storage_kind,
  uint32_t element_count,
  uint32_t element_stride,
  algorithm::AlgorithmContainerSet* out_container_set) {
  algorithm::AlgorithmContainer container{};
  container.name = container_name;
  container.storage_kind = storage_kind;
  container.element_count = element_count;
  container.element_stride = element_stride;
  container.bytes.resize(static_cast<size_t>(element_count) * static_cast<size_t>(element_stride));

  switch (storage_kind) {
    case algorithm::AlgorithmContainerStorageKind::Array:
      out_container_set->arrays.push_back(std::move(container));
      break;
    case algorithm::AlgorithmContainerStorageKind::TemporaryRegister:
      out_container_set->temporary_registers.push_back(std::move(container));
      break;
    case algorithm::AlgorithmContainerStorageKind::TemporaryCache:
      out_container_set->temporary_caches.push_back(std::move(container));
      break;
  }
  return true;
}

bool _AppendHiddenInterventionSignalContainer(algorithm::AlgorithmContainerSet* out_container_set) {
  if (!out_container_set) {
    return false;
  }

  algorithm::AlgorithmContainer signal_container{};
  signal_container.name = "__algorithm_intervention_signal";
  signal_container.storage_kind = algorithm::AlgorithmContainerStorageKind::TemporaryCache;
  signal_container.element_count = 1u;
  signal_container.element_stride = sizeof(uint32_t);
  signal_container.hidden = true;
  signal_container.bytes.resize(sizeof(uint32_t));
  out_container_set->hidden_containers.push_back(std::move(signal_container));
  return true;
}

std::string _BuildStandardContainerLayoutKey(const algorithm::AlgorithmContainerSet& container_set) {
  std::vector<std::string> tokens{};
  tokens.reserve(container_set.temporary_registers.size() + container_set.arrays.size());

  for (const algorithm::AlgorithmContainer& container : container_set.temporary_registers) {
    tokens.push_back(
      "v:" + container.name + ":" +
      std::to_string(container.element_count) + ":" +
      std::to_string(container.element_stride));
  }
  for (const algorithm::AlgorithmContainer& container : container_set.arrays) {
    tokens.push_back(
      "a:" + container.name + ":" +
      std::to_string(container.element_count) + ":" +
      std::to_string(container.element_stride));
  }

  if (tokens.empty()) {
    return {};
  }

  std::sort(tokens.begin(), tokens.end());
  std::string key{"standard:"};
  for (const std::string& token : tokens) {
    key.append(token);
    key.push_back('|');
  }
  return key;
}

bool _LoadContainerSetFromPackageJson(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algorithm::AlgorithmContainerSet>* out_container_set,
  std::string* out_error_message) {
  if (!out_container_set) {
    if (out_error_message) {
      *out_error_message = "Package container set output pointer is null.";
    }
    return false;
  }

  out_container_set->reset();

  const std::string algorithm_name = package_location.algorithm_name.empty()
    ? package_location.manifest_name
    : package_location.algorithm_name;
  const fs::path package_json_path = algorithm::package_paths::ResolvePackageJsonPath(
    package_location.package_root,
    package_location.manifest_path,
    algorithm_name);
  if (package_json_path.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to resolve package JSON file.";
    }
    return false;
  }

  const std::string json_text = json_utils::ReadTextFile(package_json_path);
  if (json_text.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to read package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    if (out_error_message) {
      *out_error_message = "Failed to parse package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  const cJSON* container_section = cJSON_GetObjectItemCaseSensitive(root, "container");
  if (!container_section || !cJSON_IsObject(container_section)) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Package JSON is missing container section: " + package_json_path.generic_string();
    }
    return false;
  }

  auto container_set = std::make_shared<algorithm::AlgorithmContainerSet>();
  container_set->algorithm_name = package_location.algorithm_name;

  const cJSON* variable_item = cJSON_GetObjectItemCaseSensitive(container_section, "variable");
  if (cJSON_IsNumber(variable_item)) {
    const uint32_t variable_count = json_utils::GetUintField(container_section, "variable");
    for (uint32_t i = 0; i < variable_count; ++i) {
      if (!_AppendContainer("v" + std::to_string(i + 1u), algorithm::AlgorithmContainerStorageKind::TemporaryRegister, 1u, sizeof(float), container_set.get())) {
        cJSON_Delete(root);
        return false;
      }
    }
  } else if (cJSON_IsObject(variable_item)) {
    for (const cJSON* child = variable_item->child; child; child = child->next) {
      if (!child->string || !*child->string) {
        cJSON_Delete(root);
        if (out_error_message) {
          *out_error_message = "Package variable container name is empty: " + package_json_path.generic_string();
        }
        return false;
      }
      const uint32_t element_count = cJSON_IsObject(child) ? std::max<uint32_t>(1u, json_utils::GetUintField(child, "count", 1u)) : 1u;
      const std::vector<uint32_t> shape = json_utils::GetShapeField(child);
      const uint32_t element_stride = shape.empty()
        ? sizeof(float)
        : static_cast<uint32_t>(shape.size()) * sizeof(float);
      if (!_AppendContainer(child->string, algorithm::AlgorithmContainerStorageKind::TemporaryRegister, element_count, element_stride, container_set.get())) {
        cJSON_Delete(root);
        return false;
      }
    }
  }

  const cJSON* variable_array_item = cJSON_GetObjectItemCaseSensitive(container_section, "variableArray");
  if (cJSON_IsNumber(variable_array_item)) {
    const uint32_t variable_array_count = json_utils::GetUintField(container_section, "variableArray");
    for (uint32_t i = 0; i < variable_array_count; ++i) {
      if (!_AppendContainer("a" + std::to_string(i + 1u), algorithm::AlgorithmContainerStorageKind::Array, 1u, sizeof(float), container_set.get())) {
        cJSON_Delete(root);
        return false;
      }
    }
  } else if (cJSON_IsObject(variable_array_item)) {
    for (const cJSON* child = variable_array_item->child; child; child = child->next) {
      if (!child->string || !*child->string) {
        cJSON_Delete(root);
        if (out_error_message) {
          *out_error_message = "Package array container name is empty: " + package_json_path.generic_string();
        }
        return false;
      }
      const uint32_t element_count = cJSON_IsObject(child) ? std::max<uint32_t>(1u, json_utils::GetUintField(child, "count", 1u)) : 1u;
      const std::vector<uint32_t> shape = json_utils::GetShapeField(child);
      const uint32_t element_stride = shape.empty()
        ? sizeof(float)
        : static_cast<uint32_t>(shape.size()) * sizeof(float);
      if (!_AppendContainer(child->string, algorithm::AlgorithmContainerStorageKind::Array, element_count, element_stride, container_set.get())) {
        cJSON_Delete(root);
        return false;
      }
    }
  }

  cJSON_Delete(root);
  container_set->standard_layout.layout_kind = "standard";
  container_set->standard_layout.variable_count = static_cast<uint32_t>(container_set->temporary_registers.size());
  container_set->standard_layout.array_count = static_cast<uint32_t>(container_set->arrays.size());
  container_set->standard_layout.variable_prefix = "v";
  container_set->standard_layout.array_prefix = "a";
  container_set->standard_layout.layout_name = _BuildStandardContainerLayoutKey(*container_set);
  *out_container_set = std::move(container_set);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _LoadPackageRuntimeReflector(
  const algorithm::AlgorithmPackageLocation& package_location,
  algorithm::AlgorithmReflector* out_reflector,
  std::string* out_error_message) {
  if (!out_reflector) {
    if (out_error_message) {
      *out_error_message = "AlgorithmReflector output pointer is null.";
    }
    return false;
  }

  const std::string algorithm_name = package_location.algorithm_name.empty()
    ? package_location.manifest_name
    : package_location.algorithm_name;
  const fs::path package_json_path = algorithm::package_paths::ResolvePackageJsonPath(
    package_location.package_root,
    package_location.manifest_path,
    algorithm_name);
  if (package_json_path.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to resolve package JSON file for reflector.";
    }
    return false;
  }

  const std::string json_text = json_utils::ReadTextFile(package_json_path);
  if (json_text.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to read package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    if (out_error_message) {
      *out_error_message = "Failed to parse package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  const cJSON* reflector = cJSON_GetObjectItemCaseSensitive(root, "reflector");
  if (!reflector || !cJSON_IsObject(reflector)) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Package JSON is missing reflector section: " + package_json_path.generic_string();
    }
    return false;
  }

  const cJSON* items = cJSON_GetObjectItemCaseSensitive(reflector, "items");
  if (!items || !cJSON_IsArray(items)) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Package reflector is missing items: " + package_json_path.generic_string();
    }
    return false;
  }

  out_reflector->Clear();
  out_reflector->algorithm_name = package_location.algorithm_name;

  const cJSON* refresh_mode_item = cJSON_GetObjectItemCaseSensitive(reflector, "refreshMode");
  if (refresh_mode_item) {
    if (cJSON_IsString(refresh_mode_item) && refresh_mode_item->valuestring) {
      const std::string refresh_mode = refresh_mode_item->valuestring;
      if (refresh_mode == "captureOnce" ||
          refresh_mode == "capture_once" ||
          refresh_mode == "onceAfterCompletion" ||
          refresh_mode == "once_after_completion") {
        out_reflector->refresh_mode = algorithm::AlgorithmReflectionRefreshMode::CaptureOnceAfterCompletion;
      } else if (refresh_mode == "everyTick" ||
                 refresh_mode == "every_tick") {
        out_reflector->refresh_mode = algorithm::AlgorithmReflectionRefreshMode::EveryTick;
      } else {
        cJSON_Delete(root);
        if (out_error_message) {
          *out_error_message = "Package reflector refreshMode is invalid: " + package_json_path.generic_string();
        }
        return false;
      }
    } else if (cJSON_IsBool(refresh_mode_item)) {
      out_reflector->refresh_mode = cJSON_IsTrue(refresh_mode_item)
        ? algorithm::AlgorithmReflectionRefreshMode::CaptureOnceAfterCompletion
        : algorithm::AlgorithmReflectionRefreshMode::EveryTick;
    } else {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package reflector refreshMode must be string or boolean: " + package_json_path.generic_string();
      }
      return false;
    }
  }

  const cJSON* function_name_item = cJSON_GetObjectItemCaseSensitive(reflector, "functionName");
  const std::string default_filter_name =
    function_name_item && cJSON_IsString(function_name_item) && function_name_item->valuestring
      ? function_name_item->valuestring
      : std::string{};

  const int item_count = cJSON_GetArraySize(items);
  for (int i = 0; i < item_count; ++i) {
    const cJSON* item = cJSON_GetArrayItem(items, i);
    if (!item || !cJSON_IsObject(item)) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package reflector item is invalid: " + package_json_path.generic_string();
      }
      return false;
    }

    std::vector<std::string> container_names{};
    std::string reflection_object_name;
    const cJSON* reflect_fun = cJSON_GetObjectItemCaseSensitive(item, "reflectFun");
    const std::string item_filter_name =
      reflect_fun && cJSON_IsString(reflect_fun) && reflect_fun->valuestring
        ? reflect_fun->valuestring
        : default_filter_name;

    if (item_filter_name == "direct") {
      const cJSON* from_item = cJSON_GetObjectItemCaseSensitive(item, "from");
      const cJSON* to_item = cJSON_GetObjectItemCaseSensitive(item, "to");
      const std::vector<std::string> from_names = json_utils::GetStringList(from_item);
      const std::vector<std::string> to_names = json_utils::GetStringList(to_item);
      if (from_names.size() != to_names.size() || from_names.empty()) {
        cJSON_Delete(root);
        if (out_error_message) {
          *out_error_message = "Package reflector direct item must contain same-length from/to lists: " + package_json_path.generic_string();
        }
        return false;
      }

      for (size_t j = 0; j < from_names.size(); ++j) {
        algorithm::AlgorithmReflectionBinding binding{};
        binding.container_names = {from_names[j]};
        binding.reflection_object_name = to_names[j];
        binding.filter_name = item_filter_name;
        out_reflector->container_bindings_by_reflection_object_name.emplace(
          binding.reflection_object_name,
          binding);
        out_reflector->reflection_objects_by_container_name[binding.container_names.front()].push_back(binding);
      }
      continue;
    }

    const cJSON* input = cJSON_GetObjectItemCaseSensitive(item, "input");
    if (!input || !cJSON_IsObject(input)) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package reflector item is missing input object: " + package_json_path.generic_string();
      }
      return false;
    }

    const cJSON* varity = cJSON_GetObjectItemCaseSensitive(input, "varity");
    const cJSON* array = cJSON_GetObjectItemCaseSensitive(input, "array");
    if (varity) {
      const std::vector<std::string> varity_names = json_utils::GetStringList(varity);
      container_names.insert(container_names.end(), varity_names.begin(), varity_names.end());
    }
    if (array) {
      const std::vector<std::string> array_names = json_utils::GetStringList(array);
      container_names.insert(container_names.end(), array_names.begin(), array_names.end());
    }
    if (container_names.empty()) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package reflector item has an empty input list: " + package_json_path.generic_string();
      }
      return false;
    }

    const cJSON* output = cJSON_GetObjectItemCaseSensitive(item, "output");
    if (!output || !cJSON_IsObject(output)) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package reflector item is missing output object: " + package_json_path.generic_string();
      }
      return false;
    }

    const cJSON* output_var = cJSON_GetObjectItemCaseSensitive(output, "v");
    const cJSON* output_arr = cJSON_GetObjectItemCaseSensitive(output, "a");
    const cJSON* output_kind = output_var && cJSON_IsObject(output_var)
      ? output_var
      : (output_arr && cJSON_IsObject(output_arr) ? output_arr : nullptr);
    if (output_kind) {
      const cJSON* name_item = cJSON_GetObjectItemCaseSensitive(output_kind, "name");
      if (name_item && cJSON_IsString(name_item) && name_item->valuestring) {
        reflection_object_name = name_item->valuestring;
      }
    }
    if (reflection_object_name.empty()) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package reflector item is missing output name: " + package_json_path.generic_string();
      }
      return false;
    }

    algorithm::AlgorithmReflectionBinding binding{};
    binding.container_names = std::move(container_names);
    binding.reflection_object_name = std::move(reflection_object_name);
    binding.filter_name = std::move(item_filter_name);

    out_reflector->container_bindings_by_reflection_object_name.emplace(
      binding.reflection_object_name,
      binding);
    for (const std::string& container_name : binding.container_names) {
      out_reflector->reflection_objects_by_container_name[container_name].push_back(binding);
    }
  }

  cJSON_Delete(root);
  return true;
}

struct PackageDefaultResourceBinding {
  std::string resource_name;
  std::string resource_kind;
  std::string source_path;
  bool required{true};
};

struct PackageDefaultDescriptorValue {
  std::string descriptor_name;
  float scalar_value{0.0f};
};

struct PackageDefaultSchema {
  bool valid{false};
  bool has_default_file{false};
  std::vector<PackageDefaultResourceBinding> resource_bindings;
  std::vector<PackageDefaultDescriptorValue> descriptor_values;
  std::string error_message;
};

PackageDefaultSchema _LoadPackageDefaultSchema(
  const algorithm::AlgorithmPackageLocation& package_location) {
  PackageDefaultSchema schema{};
  const fs::path package_default_json_path = algorithm::package_paths::ResolvePackageDefaultJsonPath(
    package_location.package_root,
    package_location.manifest_path);
  if (package_default_json_path.empty()) {
    schema.error_message = "Failed to resolve package default JSON file.";
    return schema;
  }

  std::error_code ec;
  if (!fs::exists(package_default_json_path, ec) || !fs::is_regular_file(package_default_json_path, ec)) {
    schema.valid = true;
    schema.has_default_file = false;
    return schema;
  }

  schema.has_default_file = true;

  const std::string json_text = json_utils::ReadTextFile(package_default_json_path);
  if (json_text.empty()) {
    schema.error_message = "Failed to read package default JSON file: " + package_default_json_path.generic_string();
    return schema;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    schema.error_message = "Failed to parse package default JSON file: " + package_default_json_path.generic_string();
    return schema;
  }

  const std::string algorithm_name = json_utils::GetStringField(root, "algorithm_name");
  const std::string expected_algorithm_name = package_location.algorithm_name.empty()
    ? package_location.manifest_name
    : package_location.algorithm_name;
  if (algorithm_name.empty() || (!expected_algorithm_name.empty() && algorithm_name != expected_algorithm_name)) {
    schema.error_message =
      "Package default JSON algorithm_name mismatch: " + package_default_json_path.generic_string();
    cJSON_Delete(root);
    return schema;
  }

  const cJSON* resource_bindings = cJSON_GetObjectItemCaseSensitive(root, "resource_bindings");
  if (resource_bindings) {
    if (!cJSON_IsArray(resource_bindings)) {
      schema.error_message =
        "Package default JSON resource_bindings must be an array: " + package_default_json_path.generic_string();
      cJSON_Delete(root);
      return schema;
    }
    const int resource_count = cJSON_GetArraySize(resource_bindings);
    schema.resource_bindings.reserve(resource_count > 0 ? static_cast<size_t>(resource_count) : 0u);
    for (int i = 0; i < resource_count; ++i) {
      const cJSON* item = cJSON_GetArrayItem(resource_bindings, i);
      if (!item || !cJSON_IsObject(item)) {
        schema.error_message =
          "Package default JSON resource binding is invalid: " + package_default_json_path.generic_string();
        cJSON_Delete(root);
        return schema;
      }
      PackageDefaultResourceBinding binding{};
      binding.resource_name = json_utils::GetStringField(item, "resource_name");
      binding.resource_kind = json_utils::GetStringField(item, "resource_kind");
      binding.source_path = json_utils::GetStringField(item, "source_path");
      const cJSON* required_item = cJSON_GetObjectItemCaseSensitive(item, "required");
      if (required_item) {
        if (!cJSON_IsBool(required_item)) {
          schema.error_message =
            "Package default JSON resource binding required field must be boolean: " +
            package_default_json_path.generic_string();
          cJSON_Delete(root);
          return schema;
        }
        binding.required = cJSON_IsTrue(required_item);
      }
      if (binding.resource_name.empty() || binding.resource_kind.empty()) {
        schema.error_message =
          "Package default JSON resource binding is missing a name or kind: " +
          package_default_json_path.generic_string();
        cJSON_Delete(root);
        return schema;
      }
      schema.resource_bindings.push_back(std::move(binding));
    }
  }

  const cJSON* descriptor_values = cJSON_GetObjectItemCaseSensitive(root, "descriptor_values");
  if (descriptor_values) {
    if (!cJSON_IsArray(descriptor_values)) {
      schema.error_message =
        "Package default JSON descriptor_values must be an array: " + package_default_json_path.generic_string();
      cJSON_Delete(root);
      return schema;
    }
    const int descriptor_count = cJSON_GetArraySize(descriptor_values);
    schema.descriptor_values.reserve(descriptor_count > 0 ? static_cast<size_t>(descriptor_count) : 0u);
    for (int i = 0; i < descriptor_count; ++i) {
      const cJSON* item = cJSON_GetArrayItem(descriptor_values, i);
      if (!item || !cJSON_IsObject(item)) {
        schema.error_message =
          "Package default JSON descriptor value is invalid: " + package_default_json_path.generic_string();
        cJSON_Delete(root);
        return schema;
      }
      PackageDefaultDescriptorValue value{};
      value.descriptor_name = json_utils::GetStringField(item, "descriptor_name");
      if (value.descriptor_name.empty()) {
        schema.error_message =
          "Package default JSON descriptor value is missing descriptor_name: " +
          package_default_json_path.generic_string();
        cJSON_Delete(root);
        return schema;
      }
      if (!cJSON_GetObjectItemCaseSensitive(item, "scalar_value") ||
          !cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(item, "scalar_value"))) {
        schema.error_message =
          "Package default JSON descriptor value is missing scalar_value: " +
          package_default_json_path.generic_string();
        cJSON_Delete(root);
        return schema;
      }
      value.scalar_value = static_cast<float>(cJSON_GetObjectItemCaseSensitive(item, "scalar_value")->valuedouble);
      schema.descriptor_values.push_back(std::move(value));
    }
  }

  cJSON_Delete(root);
  schema.valid = true;
  return schema;
}

}  // namespace

bool LoadAlgorithmPackageReflectorFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algorithm::AlgorithmReflector>* out_reflector,
  std::string* out_error_message) {
  if (!out_reflector) {
    if (out_error_message) {
      *out_error_message = "AlgorithmReflector output pointer is null.";
    }
    return false;
  }

  algorithm::AlgorithmReflector reflector{};
  std::string reflector_error_message;
  if (!_LoadPackageRuntimeReflector(package_location, &reflector, &reflector_error_message)) {
    if (out_error_message) {
      *out_error_message = std::move(reflector_error_message);
    }
    return false;
  }

  *out_reflector = std::make_shared<algorithm::AlgorithmReflector>(std::move(reflector));
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool LoadAlgorithmPackageTransferMapFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algorithm::AlgorithmRuntimeTransferMap>* out_transfer_map,
  bool* out_has_transfer_map,
  std::string* out_error_message) {
  if (!out_transfer_map) {
    if (out_error_message) {
      *out_error_message = "AlgorithmRuntimeTransferMap output pointer is null.";
    }
    return false;
  }

  algorithm::AlgorithmRuntimeTransferMap transfer_map{};
  bool has_transfer_map = false;
  std::string transfer_map_error_message;
  if (!_LoadPackageRuntimeTransferMap(
        package_location,
        &transfer_map,
        &has_transfer_map,
        &transfer_map_error_message)) {
    if (out_error_message) {
      *out_error_message = std::move(transfer_map_error_message);
    }
    return false;
  }

  if (!has_transfer_map) {
    out_transfer_map->reset();
    if (out_has_transfer_map) {
      *out_has_transfer_map = false;
    }
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  *out_transfer_map = std::make_shared<algorithm::AlgorithmRuntimeTransferMap>(std::move(transfer_map));
  if (out_has_transfer_map) {
    *out_has_transfer_map = true;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool CreateAlgorithmObjectFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  agent::AlgorithmObject* out_group,
  std::string* out_error_message) {
  if (!out_group) {
    if (out_error_message) {
      *out_error_message = "AlgorithmObject output pointer is null.";
    }
    return false;
  }

  if (!package_location.valid) {
    if (out_error_message) {
      *out_error_message = "Algorithm package location is invalid.";
    }
    return false;
  }

  agent::AlgorithmObject group{};
  group.algorithm_profile.algorithm_name = package_location.algorithm_name;
  group.algorithm_profile.container_manifest_name = package_location.manifest_name.empty()
    ? package_location.algorithm_name
    : package_location.manifest_name;

  if (!_LoadContainerSetFromPackageJson(package_location, &group.shared_container_set, out_error_message)) {
    return false;
  }

  algorithm_management::AlgorithmTickLifetime tick_lifetime = algorithm_management::AlgorithmTickLifetime::Continuous;
  if (!_LoadPackageRuntimeLifetime(package_location, &tick_lifetime, out_error_message)) {
    return false;
  }
  group.tick_lifetime = tick_lifetime;

  bool has_runtime_transfer_map = false;
  if (!LoadAlgorithmPackageTransferMapFromLocation(
        package_location,
        &group.runtime_transfer_map,
        &has_runtime_transfer_map,
        out_error_message)) {
    return false;
  }
  if (!has_runtime_transfer_map) {
    group.runtime_transfer_map.reset();
  }

  auto _ApplyLaunchOnceReflectionPolicy = [&group]() {
    if (group.tick_lifetime == algorithm_management::AlgorithmTickLifetime::LaunchOnceThenHold &&
        group.algorithm_reflector &&
        group.algorithm_reflector->refresh_mode == algorithm::AlgorithmReflectionRefreshMode::EveryTick) {
      group.algorithm_reflector->refresh_mode = algorithm::AlgorithmReflectionRefreshMode::CaptureOnceAfterCompletion;
    }
  };

  AlgorithmPluginComponents plugin_components{};
  std::string plugin_error_message;
  if (package_location.has_plugin_module &&
      TryLoadAlgorithmPluginComponents(package_location, &plugin_components, &plugin_error_message)) {
    group.cpu_symbol = plugin_components.cpu_symbol;
    group.gpu_symbol = plugin_components.gpu_symbol;
    group.intervention = plugin_components.intervention;
    group.temporaryTest_main_thread_executor = plugin_components.temporary_test_executor;
    if (plugin_components.runtime_reflector) {
      group.algorithm_reflector = std::move(plugin_components.runtime_reflector);
    }
    if (!group.algorithm_reflector) {
      std::shared_ptr<algorithm::AlgorithmReflector> runtime_reflector{};
      if (LoadAlgorithmPackageReflectorFromLocation(
            package_location,
            &runtime_reflector,
            nullptr)) {
        group.algorithm_reflector = std::move(runtime_reflector);
      }
    }
    _ApplyLaunchOnceReflectionPolicy();
    if (group.intervention) {
      _AppendHiddenInterventionSignalContainer(group.shared_container_set.get());
    }
    *out_group = std::move(group);
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }
  if (!plugin_error_message.empty()) {
    if (out_error_message) {
      *out_error_message = std::move(plugin_error_message);
    }
    return false;
  }

  group.cpu_symbol = true;
  group.gpu_symbol = true;
  if (!LoadAlgorithmInterventionFromLocation(
        package_location,
        &group.intervention,
        out_error_message)) {
    return false;
  }
  if (group.intervention) {
    _AppendHiddenInterventionSignalContainer(group.shared_container_set.get());
  }
  std::shared_ptr<algorithm::AlgorithmReflector> runtime_reflector{};
  if (LoadAlgorithmPackageReflectorFromLocation(
        package_location,
        &runtime_reflector,
        nullptr)) {
    group.algorithm_reflector = std::move(runtime_reflector);
  }

  _ApplyLaunchOnceReflectionPolicy();

  *out_group = std::move(group);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool LoadAlgorithmPackageDefaultBindingsFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::vector<algorithm_management::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<algorithm_management::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file,
  std::string* out_error_message) {
  if (!out_resource_bindings || !out_descriptor_values) {
    if (out_error_message) {
      *out_error_message = "Default binding output pointers are null.";
    }
    return false;
  }

  out_resource_bindings->clear();
  out_descriptor_values->clear();
  if (out_has_default_file) {
    *out_has_default_file = false;
  }

  if (!package_location.valid) {
    if (out_error_message) {
      *out_error_message = "Algorithm package location is invalid.";
    }
    return false;
  }

  const PackageDefaultSchema schema = _LoadPackageDefaultSchema(package_location);
  if (!schema.valid) {
    if (out_error_message) {
      *out_error_message = schema.error_message;
    }
    return false;
  }

  if (!schema.has_default_file) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  out_resource_bindings->reserve(schema.resource_bindings.size());
  for (const PackageDefaultResourceBinding& binding : schema.resource_bindings) {
    out_resource_bindings->push_back(algorithm_management::AlgorithmResourceBinding{
      .resource_name = binding.resource_name,
      .resource_kind = binding.resource_kind,
      .source_path = binding.source_path,
    });
  }
  out_descriptor_values->reserve(schema.descriptor_values.size());
  for (const PackageDefaultDescriptorValue& value : schema.descriptor_values) {
    out_descriptor_values->push_back(algorithm_management::AlgorithmDescriptorValue{
      .descriptor_name = value.descriptor_name,
      .scalar_value = value.scalar_value,
    });
  }

  if (out_has_default_file) {
    *out_has_default_file = true;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace algorithm_support
