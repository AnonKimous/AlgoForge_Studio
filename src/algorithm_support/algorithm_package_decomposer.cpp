#include "algorithm_support/algorithm_protocol.h"
#include "algorithm_management/algorithm_abi.h"

#include "algorithm_support/algorithm_container_manifest.h"
#include "algorithm_support/algorithm_json_utils.h"
#include "algorithm_support/algorithm_package_location.h"
#include "algorithm_support/algorithm_package_paths.h"

#include "cJSON.h"

#include <algorithm>
#include <filesystem>
#include <cstring>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <string_view>
#include <utility>

namespace algorithm_support {

namespace {

namespace fs = std::filesystem;

void _SetErrorMessage(std::string* out_error_message, std::string message) {
  if (out_error_message) {
    *out_error_message = std::move(message);
  }
}

struct PackageDecomposerMeshBinding {
  std::string resource_kind;
  std::string resource_name;
  std::string container_name;
};

struct PackageDecomposerDescriptionBinding {
  std::vector<std::string> from_names;
  std::string target_container_name;
  std::vector<uint32_t> target_indices;
  std::vector<std::string> target_container_names;
};

struct PackageDecomposerDescriptionEntry {
  std::string name;
  std::vector<PackageDecomposerDescriptionBinding> bindings;
};

struct PackageDecomposerSchema {
  std::vector<PackageDecomposerMeshBinding> mesh_bindings;
  std::vector<PackageDecomposerDescriptionEntry> description_entries;
  bool valid{false};
  std::string error_message;
};

bool _ParsePackageDecomposerBinding(
  const cJSON* binding_item,
  const std::string& package_path,
  PackageDecomposerDescriptionBinding* out_binding,
  std::string* out_error_message) {
  if (!binding_item || !cJSON_IsObject(binding_item) || !out_binding) {
    _SetErrorMessage(out_error_message, "Invalid description binding in package JSON: " + package_path);
    return false;
  }

  out_binding->from_names = json_utils::GetStringList(cJSON_GetObjectItemCaseSensitive(binding_item, "from"));
  const cJSON* to_item = cJSON_GetObjectItemCaseSensitive(binding_item, "to");
  if (to_item && cJSON_IsObject(to_item)) {
    out_binding->target_container_name = json_utils::GetStringField(to_item, "container");
    out_binding->target_indices = json_utils::GetUintList(cJSON_GetObjectItemCaseSensitive(to_item, "indices"));
  } else {
    out_binding->target_container_names = json_utils::GetStringList(to_item);
  }

  if (out_binding->from_names.empty()) {
    _SetErrorMessage(out_error_message, "Description binding is missing source names: " + package_path);
    return false;
  }
  if (out_binding->target_container_name.empty() &&
      out_binding->target_container_names.empty() &&
      out_binding->target_indices.empty()) {
    _SetErrorMessage(out_error_message, "Description binding is missing targets: " + package_path);
    return false;
  }
  return true;
}

bool _ParsePackageDecomposerDescriptionEntry(
  const cJSON* entry_item,
  const std::string& package_path,
  PackageDecomposerDescriptionEntry* out_entry,
  std::string* out_error_message) {
  if (!entry_item || !cJSON_IsObject(entry_item) || !out_entry) {
    _SetErrorMessage(out_error_message, "Invalid description entry in package JSON: " + package_path);
    return false;
  }

  out_entry->name = json_utils::GetStringField(entry_item, "name");
  if (out_entry->name.empty()) {
    _SetErrorMessage(out_error_message, "Description entry is missing a name: " + package_path);
    return false;
  }

  PackageDecomposerDescriptionBinding binding{};
  if (!_ParsePackageDecomposerBinding(entry_item, package_path, &binding, out_error_message)) {
    return false;
  }
  out_entry->bindings.push_back(std::move(binding));
  return true;
}

bool _ParsePackageDecomposerResourceGroup(
  const cJSON* group_item,
  const std::string& resource_kind,
  const std::string& package_path,
  PackageDecomposerSchema* out_schema,
  std::string* out_error_message) {
  if (!group_item || !cJSON_IsObject(group_item) || !out_schema) {
    _SetErrorMessage(out_error_message, "Invalid resource group in package JSON: " + package_path);
    return false;
  }

  for (const cJSON* child = group_item->child; child; child = child->next) {
    if (!child->string || !*child->string || !cJSON_IsString(child) || !child->valuestring) {
      _SetErrorMessage(out_error_message, "Invalid resource binding in package JSON: " + package_path);
      return false;
    }
    out_schema->mesh_bindings.push_back(PackageDecomposerMeshBinding{
      .resource_kind = resource_kind,
      .resource_name = child->string,
      .container_name = child->valuestring,
    });
  }
  return true;
}

PackageDecomposerSchema _LoadPackageDecomposerSchema(
  const algorithm::AlgorithmPackageLocation& package_location) {
  PackageDecomposerSchema schema{};
  const std::string algorithm_name = package_location.algorithm_name.empty()
    ? package_location.manifest_name
    : package_location.algorithm_name;
  const fs::path package_json_path = algorithm::package_paths::ResolvePackageJsonPath(
    package_location.package_root,
    package_location.manifest_path,
    algorithm_name);
  if (package_json_path.empty()) {
    schema.error_message = "Failed to resolve package JSON file for decomposer.";
    return schema;
  }

  const std::string json_text = json_utils::ReadTextFile(package_json_path);
  if (json_text.empty()) {
    schema.error_message = "Failed to read package JSON file: " + package_json_path.generic_string();
    return schema;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    schema.error_message = "Failed to parse package JSON file: " + package_json_path.generic_string();
    return schema;
  }

  const cJSON* decomposer = cJSON_GetObjectItemCaseSensitive(root, "decomposer");
  if (!decomposer || !cJSON_IsObject(decomposer)) {
    schema.error_message = "Decomposer JSON file is missing a 'decomposer' object: " + package_json_path.generic_string();
    cJSON_Delete(root);
    return schema;
  }

  const cJSON* res = cJSON_GetObjectItemCaseSensitive(decomposer, "res");
  if (!res || !cJSON_IsObject(res)) {
    schema.error_message = "Decomposer JSON file is missing a 'res' object: " + package_json_path.generic_string();
    cJSON_Delete(root);
    return schema;
  }
  for (const cJSON* kind_item = res->child; kind_item; kind_item = kind_item->next) {
    if (!kind_item->string || !*kind_item->string) {
      schema.error_message = "Invalid resource group in package JSON: " + package_json_path.generic_string();
      cJSON_Delete(root);
      return schema;
    }
    if (!cJSON_IsObject(kind_item)) {
      schema.error_message = "Invalid resource group payload in package JSON: " + package_json_path.generic_string();
      cJSON_Delete(root);
      return schema;
    }
    if (!_ParsePackageDecomposerResourceGroup(
          kind_item,
          kind_item->string,
          package_json_path.generic_string(),
          &schema,
          &schema.error_message)) {
      cJSON_Delete(root);
      return schema;
    }
  }

  const cJSON* description = cJSON_GetObjectItemCaseSensitive(decomposer, "description");
  if (description && cJSON_IsArray(description)) {
    const int description_count = cJSON_GetArraySize(description);
    schema.description_entries.reserve(description_count > 0 ? static_cast<size_t>(description_count) : 0u);
    for (int i = 0; i < description_count; ++i) {
      const cJSON* entry_item = cJSON_GetArrayItem(description, i);
      PackageDecomposerDescriptionEntry entry{};
      if (!_ParsePackageDecomposerDescriptionEntry(
            entry_item,
            package_json_path.generic_string(),
            &entry,
            &schema.error_message)) {
        cJSON_Delete(root);
        return schema;
      }
      schema.description_entries.push_back(std::move(entry));
    }
  }

  cJSON_Delete(root);
  schema.valid = true;
  return schema;
}

bool _WriteFloatValue(
  algorithm::AlgorithmContainer* container,
  float value) {
  if (!container) {
    return false;
  }
  if (container->bytes.size() < sizeof(float)) {
    return false;
  }
  std::memcpy(container->bytes.data(), &value, sizeof(float));
  return true;
}

bool _WriteFloatValueAtIndex(
  algorithm::AlgorithmContainer* container,
  uint32_t index,
  float value) {
  if (!container) {
    return false;
  }
  const size_t byte_offset = static_cast<size_t>(index) * static_cast<size_t>(container->element_stride);
  if (container->bytes.size() < byte_offset + sizeof(float)) {
    return false;
  }
  std::memcpy(container->bytes.data() + byte_offset, &value, sizeof(float));
  return true;
}

const agent::AlgorithmDescriptorValue* _FindDescriptorValue(
  const std::vector<agent::AlgorithmDescriptorValue>& descriptor_values,
  std::string_view descriptor_name) {
  for (const agent::AlgorithmDescriptorValue& value : descriptor_values) {
    if (value.descriptor_name == descriptor_name) {
      return &value;
    }
  }
  return nullptr;
}

const agent::AlgorithmResourceBinding* _FindResourceBinding(
  const std::vector<agent::AlgorithmResourceBinding>& resource_bindings,
  std::string_view resource_name) {
  for (const agent::AlgorithmResourceBinding& binding : resource_bindings) {
    if (binding.resource_name == resource_name) {
      return &binding;
    }
  }
  return nullptr;
}

class PackageSchemaDecomposer final {
 public:
  explicit PackageSchemaDecomposer(const algorithm::AlgorithmPackageLocation& package_location)
    : schema_(_LoadPackageDecomposerSchema(package_location)) {}

  bool valid() const { return schema_.valid; }
  const std::string& error_message() const { return schema_.error_message; }

  bool GetRequestedResources(
    const algorithm::AlgorithmProfile& algorithm_profile,
    algorithm_management::AlgorithmRequestedResources* out_requested_resources) const {
    if (!out_requested_resources) {
      return false;
    }
    *out_requested_resources = {};
    out_requested_resources->algorithm_name = algorithm_profile.algorithm_name;
    if (!schema_.valid) {
      return false;
    }

    out_requested_resources->required_resources.reserve(schema_.mesh_bindings.size());
    for (const PackageDecomposerMeshBinding& binding : schema_.mesh_bindings) {
      out_requested_resources->required_resources.push_back(agent::AlgorithmRequestedResources::RequiredResource{
        .resource_name = binding.resource_name,
        .resource_kind = binding.resource_kind,
        .required = true,
      });
    }
    out_requested_resources->valid = true;
    return true;
  }

  bool GetRequestedDescriptorBindings(
    const algorithm::AlgorithmProfile& algorithm_profile,
    algorithm_management::AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings) const {
    if (!out_requested_descriptor_bindings) {
      return false;
    }
    *out_requested_descriptor_bindings = {};
    out_requested_descriptor_bindings->algorithm_name = algorithm_profile.algorithm_name;
    if (!schema_.valid) {
      return false;
    }

    for (const PackageDecomposerDescriptionEntry& entry : schema_.description_entries) {
      for (const PackageDecomposerDescriptionBinding& binding : entry.bindings) {
        if (!binding.target_container_names.empty()) {
          if (binding.from_names.size() != binding.target_container_names.size() &&
              binding.from_names.size() != 1u) {
            return false;
          }
          for (size_t i = 0; i < binding.target_container_names.size(); ++i) {
            const size_t from_index = binding.from_names.size() == 1u ? 0u : i;
            out_requested_descriptor_bindings->descriptor_slots.push_back(
              agent::AlgorithmRequestedDescriptorBindings::DescriptorSlot{
                .descriptor_name = binding.from_names[from_index],
                .container_name = binding.target_container_names[i],
                .array_index = 0u,
              });
          }
          continue;
        }

        if (binding.from_names.size() != binding.target_indices.size() &&
            binding.from_names.size() != 1u) {
          return false;
        }
        for (size_t i = 0; i < binding.target_indices.size(); ++i) {
          const uint32_t target_index = binding.target_indices[i];
          if (target_index == 0u) {
            return false;
          }
          const size_t from_index = binding.from_names.size() == 1u ? 0u : i;
          out_requested_descriptor_bindings->descriptor_slots.push_back(
            agent::AlgorithmRequestedDescriptorBindings::DescriptorSlot{
              .descriptor_name = binding.from_names[from_index],
              .container_name = binding.target_container_name,
              .array_index = target_index - 1u,
            });
        }
      }
    }
    out_requested_descriptor_bindings->valid = true;
    return true;
  }

  bool Decompose(
    const algorithm::AlgorithmProfile& algorithm_profile,
    const std::vector<algorithm_management::AlgorithmResourceBinding>& resource_bindings,
    const std::vector<algorithm_management::AlgorithmDescriptorValue>& descriptor_values,
    algorithm::AlgorithmContainerSet* container_set,
    std::string* out_error_message) const {
    if (!container_set) {
      _SetErrorMessage(out_error_message, "AlgorithmContainerSet output pointer is null.");
      return false;
    }
    if (!schema_.valid) {
      _SetErrorMessage(out_error_message, schema_.error_message);
      return false;
    }

    for (const PackageDecomposerMeshBinding& binding : schema_.mesh_bindings) {
      const agent::AlgorithmResourceBinding* resource_binding =
        _FindResourceBinding(resource_bindings, binding.resource_name);
      if (!resource_binding) {
        _SetErrorMessage(
          out_error_message,
          "Missing required resource binding '" + binding.resource_name + "' for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }
      if (resource_binding->source_path.empty()) {
        _SetErrorMessage(
          out_error_message,
          "Required resource '" + binding.resource_name + "' has no source path for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }

      std::ifstream file(resource_binding->source_path, std::ios::binary);
      if (!file) {
        _SetErrorMessage(
          out_error_message,
          "Required resource file '" + resource_binding->source_path + "' could not be opened for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }

      algorithm::AlgorithmContainer* container = FindAlgorithmContainer(container_set, binding.container_name);
      if (!container) {
        _SetErrorMessage(
          out_error_message,
          "Missing target container '" + binding.container_name + "' for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }
      std::string file_text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      const size_t copy_size = std::min(container->bytes.size(), file_text.size());
      if (copy_size > 0u) {
        std::memcpy(container->bytes.data(), file_text.data(), copy_size);
      }
    }

    for (const PackageDecomposerDescriptionEntry& entry : schema_.description_entries) {
      for (const PackageDecomposerDescriptionBinding& binding : entry.bindings) {
        if (!binding.target_container_names.empty()) {
          const size_t from_count = binding.from_names.size();
          const size_t target_count = binding.target_container_names.size();
          if (from_count != target_count && from_count != 1u) {
            _SetErrorMessage(
              out_error_message,
              "Description entry '" + entry.name + "' has mismatched from/to list sizes for '" +
                algorithm_profile.algorithm_name + "'.");
            return false;
          }

          for (size_t i = 0; i < target_count; ++i) {
            const size_t from_index = from_count == 1u ? 0u : i;
            const agent::AlgorithmDescriptorValue* value =
              _FindDescriptorValue(descriptor_values, binding.from_names[from_index]);
            if (!value) {
              _SetErrorMessage(
                out_error_message,
                "Missing descriptor value '" + binding.from_names[from_index] + "' for '" +
                  algorithm_profile.algorithm_name + "'.");
              return false;
            }

            algorithm::AlgorithmContainer* container = FindAlgorithmContainer(container_set, binding.target_container_names[i]);
            if (!container) {
              _SetErrorMessage(
                out_error_message,
                "Missing target container '" + binding.target_container_names[i] + "' for '" +
                  algorithm_profile.algorithm_name + "'.");
              return false;
            }
            if (!_WriteFloatValue(container, value->scalar_value)) {
              _SetErrorMessage(
                out_error_message,
                "Target container '" + binding.target_container_names[i] + "' has insufficient storage for '" +
                  algorithm_profile.algorithm_name + "'.");
              return false;
            }
          }
          continue;
        }

        const size_t from_count = binding.from_names.size();
        const size_t target_count = binding.target_indices.size();
        if (from_count != target_count && from_count != 1u) {
          _SetErrorMessage(
            out_error_message,
            "Description entry '" + entry.name + "' has mismatched from/to list sizes for '" +
              algorithm_profile.algorithm_name + "'.");
          return false;
        }

        algorithm::AlgorithmContainer* container = FindAlgorithmContainer(container_set, binding.target_container_name);
        if (!container) {
          _SetErrorMessage(
            out_error_message,
            "Missing target container '" + binding.target_container_name + "' for '" +
              algorithm_profile.algorithm_name + "'.");
          return false;
        }
        if (container->storage_kind != algorithm::AlgorithmContainerStorageKind::Array) {
          _SetErrorMessage(
            out_error_message,
            "Target container '" + binding.target_container_name + "' is not an array for '" +
              algorithm_profile.algorithm_name + "'.");
          return false;
        }

        for (size_t i = 0; i < target_count; ++i) {
          const uint32_t target_index = binding.target_indices[i];
          const size_t from_index = from_count == 1u ? 0u : i;
          const agent::AlgorithmDescriptorValue* value =
            _FindDescriptorValue(descriptor_values, binding.from_names[from_index]);
          if (!value) {
            _SetErrorMessage(
              out_error_message,
              "Missing descriptor value '" + binding.from_names[from_index] + "' for '" +
                algorithm_profile.algorithm_name + "'.");
            return false;
          }
          if (target_index == 0u) {
            _SetErrorMessage(
              out_error_message,
              "Unsupported descriptor mapping '" + binding.from_names[from_index] + "' for container '" +
                binding.target_container_name + "'.");
            return false;
          }
          if (!_WriteFloatValueAtIndex(container, target_index - 1u, value->scalar_value)) {
            _SetErrorMessage(
              out_error_message,
              "Target container '" + binding.target_container_name + "' has insufficient storage for '" +
                algorithm_profile.algorithm_name + "'.");
            return false;
          }
        }
      }
    }

    return true;
  }

 private:
  PackageDecomposerSchema schema_{};
};

bool _CopyRuntimeMappedContainer(
  const algorithm::AlgorithmContainerSet& source_container_set,
  const algorithm::AlgorithmContainer& source_container,
  const algorithm::AlgorithmContainerSet& target_container_set,
  algorithm::AlgorithmContainer* target_container,
  const std::string& source_stage_name,
  const std::string& target_stage_name,
  const std::string& source_container_name,
  const std::string& target_container_name,
  std::string* out_error_message) {
  if (!target_container) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer target container is null for '" + source_stage_name + "->" + target_stage_name + "'.");
    return false;
  }
  if (source_container.bytes.empty() || target_container->bytes.empty()) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer container has no data for '" + source_stage_name + "->" + target_stage_name +
        "': '" + source_container_name + "' to '" + target_container_name + "'.");
    return false;
  }

  const bool source_is_standard_slot =
    algorithm::IsStandardContainerSlotName(source_container_set, source_container_name);
  const bool target_is_standard_slot =
    algorithm::IsStandardContainerSlotName(target_container_set, target_container_name);
  if (source_is_standard_slot != target_is_standard_slot) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer cannot mix standard slots with custom containers for '" +
        source_stage_name + "->" + target_stage_name + "': '" + source_container_name +
        "' to '" + target_container_name + "'.");
    return false;
  }

  if (!source_is_standard_slot && source_container_name != target_container_name) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer requires same-name custom containers for '" + source_stage_name + "->" +
        target_stage_name + "': '" + source_container_name + "' to '" + target_container_name + "'.");
    return false;
  }

  if (!algorithm::HasSameContainerStructure(source_container, *target_container)) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer container structure mismatch for '" + source_stage_name + "->" + target_stage_name +
        "': '" + source_container_name + "' to '" + target_container_name + "'.");
    return false;
  }

  std::memcpy(target_container->bytes.data(), source_container.bytes.data(), source_container.bytes.size());
  return true;
}

bool _ApplyRuntimeTransferEdgeToTarget(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const algorithm::AlgorithmRuntimeTransferEdge& edge,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  algorithm::AlgorithmContainerSet* target_container_set,
  std::unordered_set<std::string>* written_target_names,
  std::string* out_error_message) {
  (void)transfer_map;
  if (!written_target_names) {
    _SetErrorMessage(out_error_message, "Runtime transfer target tracking set is null.");
    return false;
  }
  if (!target_container_set) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer target container set is null for stage '" + edge.target_stage_name + "'.");
    return false;
  }

  const auto source_stage_it = stage_container_sets.find(edge.source_stage_name);
  if (source_stage_it == stage_container_sets.end() || !source_stage_it->second) {
    if (edge.bindings.empty()) {
      return true;
    }
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer source stage is unavailable: " + edge.source_stage_name);
    return false;
  }

  const algorithm::AlgorithmContainerSet& source_container_set = *source_stage_it->second;
  for (const algorithm::AlgorithmRuntimeTransferBinding& binding : edge.bindings) {
    const algorithm::AlgorithmContainer* source_container =
      algorithm::FindAlgorithmContainer(source_container_set, binding.from_name);
    if (!source_container) {
      if (!binding.required) {
        continue;
      }
      _SetErrorMessage(
        out_error_message,
        "Runtime transfer source container is missing: " + edge.source_stage_name + "." + binding.from_name);
      return false;
    }

    algorithm::AlgorithmContainer* target_container =
      algorithm::FindAlgorithmContainer(target_container_set, binding.to_name);
    if (!target_container) {
      if (!binding.required) {
        continue;
      }
      _SetErrorMessage(
        out_error_message,
        "Runtime transfer target container is missing: " + edge.target_stage_name + "." + binding.to_name);
      return false;
    }

    if (written_target_names->find(binding.to_name) != written_target_names->end()) {
      _SetErrorMessage(
        out_error_message,
        "Runtime transfer target container is written more than once: " +
          edge.target_stage_name + "." + binding.to_name);
      return false;
    }

    if (!_CopyRuntimeMappedContainer(
          source_container_set,
          *source_container,
          *target_container_set,
          target_container,
          edge.source_stage_name,
          edge.target_stage_name,
          binding.from_name,
          binding.to_name,
          out_error_message)) {
      return false;
    }

    written_target_names->insert(binding.to_name);
  }

  return true;
}

bool _ApplyRuntimeTransferEdgeFromSource(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const algorithm::AlgorithmRuntimeTransferEdge& edge,
  const algorithm::AlgorithmContainerSet& source_container_set,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
  std::unordered_map<std::string, std::unordered_set<std::string>>* written_target_names_by_stage,
  std::string* out_error_message) {
  (void)transfer_map;
  if (!stage_container_sets || !written_target_names_by_stage) {
    _SetErrorMessage(out_error_message, "Runtime transfer stage container set map is null.");
    return false;
  }

  for (const algorithm::AlgorithmRuntimeTransferBinding& binding : edge.bindings) {
    const algorithm::AlgorithmContainer* source_container =
      algorithm::FindAlgorithmContainer(source_container_set, binding.from_name);
    if (!source_container) {
      if (!binding.required) {
        continue;
      }
      _SetErrorMessage(
        out_error_message,
        "Runtime transfer source container is missing: " + edge.source_stage_name + "." + binding.from_name);
      return false;
    }

    auto target_stage_it = stage_container_sets->find(edge.target_stage_name);
    if (target_stage_it == stage_container_sets->end() || !target_stage_it->second) {
      if (!binding.required) {
        continue;
      }
      _SetErrorMessage(
        out_error_message,
        "Runtime transfer target stage is unavailable: " + edge.target_stage_name);
      return false;
    }

    algorithm::AlgorithmContainerSet* target_container_set = target_stage_it->second.get();
    algorithm::AlgorithmContainer* target_container =
      algorithm::FindAlgorithmContainer(target_container_set, binding.to_name);
    if (!target_container) {
      if (!binding.required) {
        continue;
      }
      _SetErrorMessage(
        out_error_message,
        "Runtime transfer target container is missing: " + edge.target_stage_name + "." + binding.to_name);
      return false;
    }

    std::unordered_set<std::string>& written_target_names = (*written_target_names_by_stage)[edge.target_stage_name];
    if (written_target_names.find(binding.to_name) != written_target_names.end()) {
      _SetErrorMessage(
        out_error_message,
        "Runtime transfer target container is written more than once: " +
          edge.target_stage_name + "." + binding.to_name);
      return false;
    }

    if (!_CopyRuntimeMappedContainer(
          source_container_set,
          *source_container,
          *target_container_set,
          target_container,
          edge.source_stage_name,
          edge.target_stage_name,
          binding.from_name,
          binding.to_name,
          out_error_message)) {
      return false;
    }

    written_target_names.insert(binding.to_name);
  }

  return true;
}

void _CopyBridgeDebugBindings(
  const algorithm::AlgorithmRuntimeTransferEdge* edge,
  std::vector<algorithm_management::PipelineStageBridgeDebugBinding>* out_bindings) {
  if (!out_bindings) {
    return;
  }
  out_bindings->clear();
  if (!edge) {
    return;
  }
  out_bindings->reserve(edge->bindings.size());
  for (const algorithm::AlgorithmRuntimeTransferBinding& binding : edge->bindings) {
    out_bindings->push_back(algorithm_management::PipelineStageBridgeDebugBinding{
      .source_stage_name = edge->source_stage_name,
      .target_stage_name = edge->target_stage_name,
      .source_container_name = binding.from_name,
      .target_container_name = binding.to_name,
      .required = binding.required,
    });
  }
}

const algorithm::AlgorithmRuntimeTransferStageLayout* _FindRuntimeTransferStageLayout(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& stage_name) {
  return transfer_map.FindStageLayout(stage_name);
}

bool _ReadArrayScalarAt(
  const algorithm::AlgorithmContainer& container,
  uint32_t index,
  float* out_value,
  std::string* out_error_message) {
  if (!out_value) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer read output pointer is null.");
    return false;
  }
  if (container.storage_kind != algorithm::AlgorithmContainerStorageKind::Array) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer must be an array container.");
    return false;
  }
  if (container.element_stride < sizeof(float) || index >= container.element_count) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer scalar read is out of range.");
    return false;
  }
  const size_t byte_offset = static_cast<size_t>(index) * static_cast<size_t>(container.element_stride);
  if (byte_offset + sizeof(float) > container.bytes.size()) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer scalar read exceeds container storage.");
    return false;
  }
  std::memcpy(out_value, container.bytes.data() + byte_offset, sizeof(float));
  return true;
}

bool _WriteArrayScalarAt(
  algorithm::AlgorithmContainer* container,
  uint32_t index,
  float value,
  std::string* out_error_message) {
  if (!container) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer write target is null.");
    return false;
  }
  if (container->storage_kind != algorithm::AlgorithmContainerStorageKind::Array) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer must be an array container.");
    return false;
  }
  if (container->element_stride < sizeof(float) || index >= container->element_count) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer scalar write is out of range.");
    return false;
  }
  const size_t byte_offset = static_cast<size_t>(index) * static_cast<size_t>(container->element_stride);
  if (byte_offset + sizeof(float) > container->bytes.size()) {
    _SetErrorMessage(out_error_message, "Pipeline stage buffer scalar write exceeds container storage.");
    return false;
  }
  std::memcpy(container->bytes.data() + byte_offset, &value, sizeof(float));
  return true;
}

bool _ReadCpuInterStageScalarAt(
  const algorithm_management::CpuPipelineInterStageBufferRuntimeState& inter_stage_buffer,
  uint32_t index,
  float* out_value,
  std::string* out_error_message) {
  if (!inter_stage_buffer.valid) {
    _SetErrorMessage(out_error_message, "CPU pipeline inter-stage buffer is invalid.");
    return false;
  }
  if (!out_value) {
    _SetErrorMessage(out_error_message, "CPU pipeline inter-stage buffer read output pointer is null.");
    return false;
  }
  if (index >= inter_stage_buffer.scalar_slots.size() ||
      index >= inter_stage_buffer.scalar_slot_count) {
    _SetErrorMessage(out_error_message, "CPU pipeline inter-stage buffer scalar read is out of range.");
    return false;
  }
  *out_value = inter_stage_buffer.scalar_slots[index];
  return true;
}

bool _WriteCpuInterStageScalarAt(
  algorithm_management::CpuPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  uint32_t index,
  float value,
  std::string* out_error_message) {
  if (!inter_stage_buffer) {
    _SetErrorMessage(out_error_message, "CPU pipeline inter-stage buffer write target is null.");
    return false;
  }
  if (!inter_stage_buffer->valid) {
    _SetErrorMessage(out_error_message, "CPU pipeline inter-stage buffer is invalid.");
    return false;
  }
  if (index >= inter_stage_buffer->scalar_slots.size() ||
      index >= inter_stage_buffer->scalar_slot_count) {
    _SetErrorMessage(out_error_message, "CPU pipeline inter-stage buffer scalar write is out of range.");
    return false;
  }
  inter_stage_buffer->scalar_slots[index] = value;
  return true;
}

bool _ApplyImplicitStageBufferIngress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& target_stage_name,
  const algorithm_management::CpuPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  algorithm::AlgorithmContainerSet* target_container_set,
  std::unordered_set<std::string>* written_target_names,
  std::string* out_error_message) {
  if (!target_container_set || !written_target_names) {
    _SetErrorMessage(out_error_message, "Pipeline implicit stage buffer ingress inputs are null.");
    return false;
  }
  const algorithm::AlgorithmRuntimeTransferStageLayout* stage_layout =
    _FindRuntimeTransferStageLayout(transfer_map, target_stage_name);
  if (!stage_layout) {
    _SetErrorMessage(
      out_error_message,
      "Pipeline implicit stage buffer ingress layout is missing for stage '" + target_stage_name + "'.");
    return false;
  }
  if (stage_layout->extra_array_count != 0u) {
    _SetErrorMessage(
      out_error_message,
      "Pipeline implicit stage buffer ingress does not support extra standard arrays for stage '" +
        target_stage_name + "'.");
    return false;
  }
  if (stage_layout->extra_variable_count == 0u) {
    return true;
  }

  for (uint32_t extra_index = 0u; extra_index < stage_layout->extra_variable_count; ++extra_index) {
    const std::string variable_name =
      target_container_set->standard_layout.MakeVariableName(stage_layout->shared_variable_count + extra_index);
    if (written_target_names->find(variable_name) != written_target_names->end()) {
      _SetErrorMessage(
        out_error_message,
        "Pipeline implicit stage buffer ingress target variable is written more than once: " +
          target_stage_name + "." + variable_name);
      return false;
    }

    algorithm::AlgorithmContainer* target_variable =
      algorithm::FindAlgorithmContainer(target_container_set, variable_name);
    if (!target_variable) {
      _SetErrorMessage(
        out_error_message,
        "Pipeline implicit stage buffer ingress target variable is missing: " +
          target_stage_name + "." + variable_name);
      return false;
    }
    if (target_variable->storage_kind != algorithm::AlgorithmContainerStorageKind::TemporaryRegister ||
        target_variable->bytes.size() < sizeof(float)) {
      _SetErrorMessage(
        out_error_message,
        "Pipeline implicit stage buffer ingress target variable must be a scalar register: " +
          target_stage_name + "." + variable_name);
      return false;
    }

    float value = 0.0f;
    if (inter_stage_buffer) {
      if (!_ReadCpuInterStageScalarAt(
            *inter_stage_buffer,
            stage_layout->extra_variable_offset + extra_index,
            &value,
            out_error_message)) {
        return false;
      }
    } else {
      algorithm::AlgorithmContainer* stage_buffer =
        algorithm::FindAlgorithmContainer(target_container_set, transfer_map.pipeline_shared_stage_buffer_slot_name);
      if (!stage_buffer) {
        _SetErrorMessage(
          out_error_message,
          "Pipeline implicit stage buffer ingress cannot find shared stage buffer '" +
            transfer_map.pipeline_shared_stage_buffer_slot_name + "' for stage '" + target_stage_name + "'.");
        return false;
      }
      if (!_ReadArrayScalarAt(
            *stage_buffer,
            stage_layout->extra_variable_offset + extra_index,
            &value,
            out_error_message)) {
        return false;
      }
    }
    std::memcpy(target_variable->bytes.data(), &value, sizeof(float));
    written_target_names->insert(variable_name);
  }

  return true;
}

bool _ApplyImplicitStageBufferEgress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  algorithm_management::CpuPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  std::string* out_error_message) {
  const algorithm::AlgorithmRuntimeTransferStageLayout* stage_layout =
    _FindRuntimeTransferStageLayout(transfer_map, source_stage_name);
  if (!stage_layout) {
    _SetErrorMessage(
      out_error_message,
      "Pipeline implicit stage buffer egress layout is missing for stage '" + source_stage_name + "'.");
    return false;
  }
  if (stage_layout->extra_array_count != 0u) {
    _SetErrorMessage(
      out_error_message,
      "Pipeline implicit stage buffer egress does not support extra standard arrays for stage '" +
        source_stage_name + "'.");
    return false;
  }
  if (stage_layout->extra_variable_count == 0u) {
    return true;
  }

  for (uint32_t extra_index = 0u; extra_index < stage_layout->extra_variable_count; ++extra_index) {
    const std::string variable_name =
      source_container_set.standard_layout.MakeVariableName(stage_layout->shared_variable_count + extra_index);
    const algorithm::AlgorithmContainer* source_variable =
      algorithm::FindAlgorithmContainer(source_container_set, variable_name);
    if (!source_variable) {
      _SetErrorMessage(
        out_error_message,
        "Pipeline implicit stage buffer egress source variable is missing: " +
          source_stage_name + "." + variable_name);
      return false;
    }
    if (source_variable->storage_kind != algorithm::AlgorithmContainerStorageKind::TemporaryRegister ||
        source_variable->bytes.size() < sizeof(float)) {
      _SetErrorMessage(
        out_error_message,
        "Pipeline implicit stage buffer egress source variable must be a scalar register: " +
          source_stage_name + "." + variable_name);
      return false;
    }

    float value = 0.0f;
    std::memcpy(&value, source_variable->bytes.data(), sizeof(float));
    if (inter_stage_buffer) {
      if (!_WriteCpuInterStageScalarAt(
            inter_stage_buffer,
            stage_layout->extra_variable_offset + extra_index,
            value,
            out_error_message)) {
        return false;
      }
    } else {
      algorithm::AlgorithmContainer* stage_buffer =
        algorithm::FindAlgorithmContainer(
          const_cast<algorithm::AlgorithmContainerSet*>(&source_container_set),
          transfer_map.pipeline_shared_stage_buffer_slot_name);
      if (!stage_buffer) {
        _SetErrorMessage(
          out_error_message,
          "Pipeline implicit stage buffer egress cannot find shared stage buffer '" +
            transfer_map.pipeline_shared_stage_buffer_slot_name + "' for stage '" + source_stage_name + "'.");
        return false;
      }
      if (!_WriteArrayScalarAt(
            stage_buffer,
            stage_layout->extra_variable_offset + extra_index,
            value,
            out_error_message)) {
        return false;
      }
    }
  }

  return true;
}

}  // namespace

bool QueryAlgorithmPackageRequestedBindingsFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  algorithm_management::AlgorithmRequestedResources* out_requested_resources,
  algorithm_management::AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings,
  std::string* out_error_message) {
  if (!package_location.valid) {
    _SetErrorMessage(out_error_message, "Algorithm package location is invalid.");
    return false;
  }
  const PackageSchemaDecomposer decomposer(package_location);
  if (!decomposer.valid()) {
    _SetErrorMessage(out_error_message, decomposer.error_message());
    return false;
  }
  const algorithm::AlgorithmProfile profile{
    .algorithm_name = package_location.algorithm_name,
    .container_manifest_name = package_location.manifest_name,
  };
  if (!decomposer.GetRequestedResources(profile, out_requested_resources) ||
      !decomposer.GetRequestedDescriptorBindings(profile, out_requested_descriptor_bindings)) {
    _SetErrorMessage(out_error_message, "Failed to query requested bindings from package JSON.");
    return false;
  }
  _SetErrorMessage(out_error_message, {});
  return true;
}

bool _PipelineStageBridgeIngressImpl(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  const algorithm_management::CpuPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  algorithm::AlgorithmContainerSet* out_target_container_set,
  std::string* out_error_message) {
  if (!transfer_map.valid) {
    _SetErrorMessage(out_error_message, "Algorithm runtime transfer map is invalid.");
    return false;
  }
  if (!out_target_container_set) {
    _SetErrorMessage(out_error_message, "Runtime transfer target container set is null.");
    return false;
  }

  const std::vector<const algorithm::AlgorithmRuntimeTransferEdge*> incoming_edges =
    transfer_map.FindIncomingEdges(target_stage_name);
  if (incoming_edges.size() > 1u) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer map is not linear: stage '" + target_stage_name + "' has multiple predecessors.");
    return false;
  }

  std::unordered_set<std::string> written_target_names{};
  for (const algorithm::AlgorithmRuntimeTransferEdge* edge : incoming_edges) {
    if (!edge) {
      continue;
    }
    if (!_ApplyRuntimeTransferEdgeToTarget(
          transfer_map,
          *edge,
          stage_container_sets,
          out_target_container_set,
          &written_target_names,
          out_error_message)) {
      return false;
    }
  }
  if (!_ApplyImplicitStageBufferIngress(
        transfer_map,
        target_stage_name,
        inter_stage_buffer,
        out_target_container_set,
        &written_target_names,
        out_error_message)) {
    return false;
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool PipelineStageBridgeIngress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  algorithm::AlgorithmContainerSet* out_target_container_set,
  std::string* out_error_message) {
  return _PipelineStageBridgeIngressImpl(
    transfer_map,
    target_stage_name,
    stage_container_sets,
    nullptr,
    out_target_container_set,
    out_error_message);
}

bool PipelineStageBridgeIngress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  const algorithm_management::CpuPipelineInterStageBufferRuntimeState& inter_stage_buffer,
  algorithm::AlgorithmContainerSet* out_target_container_set,
  std::string* out_error_message) {
  return _PipelineStageBridgeIngressImpl(
    transfer_map,
    target_stage_name,
    stage_container_sets,
    &inter_stage_buffer,
    out_target_container_set,
    out_error_message);
}

bool _PipelineStageBridgeEgressImpl(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  algorithm_management::CpuPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
  std::string* out_error_message) {
  if (!transfer_map.valid) {
    _SetErrorMessage(out_error_message, "Algorithm runtime transfer map is invalid.");
    return false;
  }
  if (!stage_container_sets) {
    _SetErrorMessage(out_error_message, "Runtime transfer stage container set map is null.");
    return false;
  }
  if (!_ApplyImplicitStageBufferEgress(
        transfer_map,
        source_stage_name,
        source_container_set,
        inter_stage_buffer,
        out_error_message)) {
    return false;
  }

  const std::vector<const algorithm::AlgorithmRuntimeTransferEdge*> outgoing_edges =
    transfer_map.FindOutgoingEdges(source_stage_name);
  if (outgoing_edges.size() > 1u) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer map is not linear: stage '" + source_stage_name + "' has multiple successors.");
    return false;
  }
  if (outgoing_edges.empty()) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  std::unordered_map<std::string, std::unordered_set<std::string>> written_target_names_by_stage{};
  for (const algorithm::AlgorithmRuntimeTransferEdge* edge : outgoing_edges) {
    if (!edge) {
      continue;
    }
    if (!_ApplyRuntimeTransferEdgeFromSource(
          transfer_map,
          *edge,
          source_container_set,
          stage_container_sets,
          &written_target_names_by_stage,
          out_error_message)) {
      return false;
    }
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool PipelineStageBridgeEgress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
  std::string* out_error_message) {
  return _PipelineStageBridgeEgressImpl(
    transfer_map,
    source_stage_name,
    source_container_set,
    nullptr,
    stage_container_sets,
    out_error_message);
}

bool PipelineStageBridgeEgress(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  algorithm_management::CpuPipelineInterStageBufferRuntimeState* inter_stage_buffer,
  std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>* stage_container_sets,
  std::string* out_error_message) {
  return _PipelineStageBridgeEgressImpl(
    transfer_map,
    source_stage_name,
    source_container_set,
    inter_stage_buffer,
    stage_container_sets,
    out_error_message);
}

bool PipelineStageBridgeCaptureIngressDebugSet(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& pipeline_name,
  const std::string& target_stage_name,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  const algorithm::AlgorithmContainerSet& target_container_set,
  algorithm_management::PipelineStageBridgeDebugSet* out_debug_set,
  std::string* out_error_message) {
  (void)stage_container_sets;
  if (!transfer_map.valid) {
    _SetErrorMessage(out_error_message, "Algorithm runtime transfer map is invalid.");
    return false;
  }
  if (!out_debug_set) {
    _SetErrorMessage(out_error_message, "Pipeline bridge debug set output pointer is null.");
    return false;
  }

  out_debug_set->Clear();
  out_debug_set->pipeline_name = pipeline_name.empty() ? transfer_map.algorithm_name : pipeline_name;
  out_debug_set->stage_name = target_stage_name;

  const std::vector<const algorithm::AlgorithmRuntimeTransferEdge*> incoming_edges =
    transfer_map.FindIncomingEdges(target_stage_name);
  if (incoming_edges.size() > 1u) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer map is not linear: stage '" + target_stage_name + "' has multiple predecessors.");
    return false;
  }

  if (!incoming_edges.empty() && incoming_edges.front()) {
    out_debug_set->previous_stage_name = incoming_edges.front()->source_stage_name;
    _CopyBridgeDebugBindings(incoming_edges.front(), &out_debug_set->ingress_bindings);
  }

  algorithm::CopyAlgorithmContainerSet(
    target_container_set,
    &out_debug_set->stage_input_container_set);
  out_debug_set->has_stage_input_container_set = true;
  out_debug_set->valid = true;

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool PipelineStageBridgeCaptureEgressDebugSet(
  const algorithm::AlgorithmRuntimeTransferMap& transfer_map,
  const std::string& pipeline_name,
  const std::string& source_stage_name,
  const algorithm::AlgorithmContainerSet& source_container_set,
  const std::unordered_map<std::string, std::shared_ptr<algorithm::AlgorithmContainerSet>>& stage_container_sets,
  algorithm_management::PipelineStageBridgeDebugSet* in_out_debug_set,
  std::string* out_error_message) {
  if (!transfer_map.valid) {
    _SetErrorMessage(out_error_message, "Algorithm runtime transfer map is invalid.");
    return false;
  }
  if (!in_out_debug_set) {
    _SetErrorMessage(out_error_message, "Pipeline bridge debug set output pointer is null.");
    return false;
  }

  if (!in_out_debug_set->valid) {
    in_out_debug_set->Clear();
  }
  if (in_out_debug_set->pipeline_name.empty()) {
    in_out_debug_set->pipeline_name = pipeline_name.empty() ? transfer_map.algorithm_name : pipeline_name;
  }
  in_out_debug_set->stage_name = source_stage_name;

  algorithm::CopyAlgorithmContainerSet(
    source_container_set,
    &in_out_debug_set->stage_output_container_set);
  in_out_debug_set->has_stage_output_container_set = true;

  const std::vector<const algorithm::AlgorithmRuntimeTransferEdge*> outgoing_edges =
    transfer_map.FindOutgoingEdges(source_stage_name);
  if (outgoing_edges.size() > 1u) {
    _SetErrorMessage(
      out_error_message,
      "Runtime transfer map is not linear: stage '" + source_stage_name + "' has multiple successors.");
    return false;
  }

  in_out_debug_set->next_stage_name.clear();
  in_out_debug_set->egress_bindings.clear();
  in_out_debug_set->next_stage_input_container_set = {};
  in_out_debug_set->has_next_stage_input_container_set = false;

  if (!outgoing_edges.empty() && outgoing_edges.front()) {
    const algorithm::AlgorithmRuntimeTransferEdge* outgoing_edge = outgoing_edges.front();
    in_out_debug_set->next_stage_name = outgoing_edge->target_stage_name;
    _CopyBridgeDebugBindings(outgoing_edge, &in_out_debug_set->egress_bindings);

    const auto target_stage_it = stage_container_sets.find(outgoing_edge->target_stage_name);
    if (target_stage_it == stage_container_sets.end() || !target_stage_it->second) {
      _SetErrorMessage(
        out_error_message,
        "Runtime transfer target stage is unavailable for bridge debug capture: " +
          outgoing_edge->target_stage_name);
      return false;
    }

    algorithm::CopyAlgorithmContainerSet(
      *target_stage_it->second,
      &in_out_debug_set->next_stage_input_container_set);
    in_out_debug_set->has_next_stage_input_container_set = true;
  }

  in_out_debug_set->valid = true;
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool DecomposeAlgorithmPackageFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  const std::vector<algorithm_management::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<algorithm_management::AlgorithmDescriptorValue>& descriptor_values,
  algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message) {
  if (!package_location.valid) {
    _SetErrorMessage(out_error_message, "Algorithm package location is invalid.");
    return false;
  }
  const PackageSchemaDecomposer decomposer(package_location);
  if (!decomposer.valid()) {
    _SetErrorMessage(out_error_message, decomposer.error_message());
    return false;
  }
  const algorithm::AlgorithmProfile profile{
    .algorithm_name = package_location.algorithm_name,
    .container_manifest_name = package_location.manifest_name,
  };
  const bool ok = decomposer.Decompose(
    profile,
    resource_bindings,
    descriptor_values,
    container_set,
    out_error_message);
  return ok;
}

}  // namespace algorithm_support
