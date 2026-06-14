#include "algorithm_support/algorithm_protocol.h"
#include "algorithm_management/algorithm_abi.h"

#include "algorithm_support/algorithm_container_manifest.h"
#include "algorithm_support/algorithm_package_location.h"

#include "cJSON.h"

#include <algorithm>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
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

std::string _ReadPackageTextFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

std::string _GetPackageStringField(const cJSON* object, const char* key) {
  if (!object || !key) {
    return {};
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item || !cJSON_IsString(item) || !item->valuestring) {
    return {};
  }
  return item->valuestring;
}

std::vector<std::string> _GetPackageStringList(const cJSON* item) {
  std::vector<std::string> values;
  if (!item) {
    return values;
  }
  if (cJSON_IsString(item) && item->valuestring) {
    values.push_back(item->valuestring);
    return values;
  }
  if (!cJSON_IsArray(item)) {
    return values;
  }
  const int count = cJSON_GetArraySize(item);
  values.reserve(count > 0 ? static_cast<size_t>(count) : 0u);
  for (int i = 0; i < count; ++i) {
    const cJSON* entry = cJSON_GetArrayItem(item, i);
    if (!entry || !cJSON_IsString(entry) || !entry->valuestring) {
      continue;
    }
    values.emplace_back(entry->valuestring);
  }
  return values;
}

std::vector<uint32_t> _GetPackageUintList(const cJSON* item) {
  std::vector<uint32_t> values;
  if (!item || !cJSON_IsArray(item)) {
    return values;
  }
  const int count = cJSON_GetArraySize(item);
  values.reserve(count > 0 ? static_cast<size_t>(count) : 0u);
  for (int i = 0; i < count; ++i) {
    const cJSON* entry = cJSON_GetArrayItem(item, i);
    if (!entry || !cJSON_IsNumber(entry) || entry->valuedouble < 0.0) {
      continue;
    }
    values.push_back(static_cast<uint32_t>(entry->valuedouble));
  }
  return values;
}

std::filesystem::path _BuildPackageJsonPath(const algorithm::AlgorithmPackageLocation& package_location) {
  const std::string algorithm_name = package_location.algorithm_name.empty()
    ? package_location.manifest_name
    : package_location.algorithm_name;
  if (algorithm_name.empty()) {
    return {};
  }

  fs::path package_root = package_location.package_root;
  if (package_root.empty()) {
    package_root = package_location.manifest_path.parent_path();
  }
  if (package_root.empty()) {
    return {};
  }

  return package_root / (algorithm_name + "_package.json");
}

PackageDecomposerSchema _LoadPackageDecomposerSchema(
  const algorithm::AlgorithmPackageLocation& package_location) {
  PackageDecomposerSchema schema{};
  const fs::path package_json_path = _BuildPackageJsonPath(package_location);
  if (package_json_path.empty()) {
    schema.error_message = "Failed to resolve package JSON file for decomposer.";
    return schema;
  }

  const std::string json_text = _ReadPackageTextFile(package_json_path.generic_string());
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
    cJSON_Delete(root);
    schema.valid = true;
    return schema;
  }

  const cJSON* res = cJSON_GetObjectItemCaseSensitive(decomposer, "res");
  if (res && cJSON_IsObject(res)) {
    for (const cJSON* kind_item = res->child; kind_item; kind_item = kind_item->next) {
      if (!kind_item->string || !*kind_item->string || !cJSON_IsObject(kind_item)) {
        schema.error_message = "Invalid resource group in package JSON: " + package_json_path.generic_string();
        cJSON_Delete(root);
        return schema;
      }
      for (const cJSON* child = kind_item->child; child; child = child->next) {
        if (!child->string || !*child->string || !cJSON_IsString(child) || !child->valuestring) {
          schema.error_message = "Invalid resource binding in package JSON: " + package_json_path.generic_string();
          cJSON_Delete(root);
          return schema;
        }
        schema.mesh_bindings.push_back(PackageDecomposerMeshBinding{
          .resource_kind = kind_item->string,
          .resource_name = child->string,
          .container_name = child->valuestring,
        });
      }
    }
  }

  const cJSON* description = cJSON_GetObjectItemCaseSensitive(decomposer, "description");
  if (description && cJSON_IsArray(description)) {
    const int description_count = cJSON_GetArraySize(description);
    schema.description_entries.reserve(description_count > 0 ? static_cast<size_t>(description_count) : 0u);
    for (int i = 0; i < description_count; ++i) {
      const cJSON* entry_item = cJSON_GetArrayItem(description, i);
      if (!entry_item || !cJSON_IsObject(entry_item)) {
        schema.error_message = "Invalid description entry in package JSON: " + package_json_path.generic_string();
        cJSON_Delete(root);
        return schema;
      }

      PackageDecomposerDescriptionEntry entry{};
      entry.name = _GetPackageStringField(entry_item, "name");
      PackageDecomposerDescriptionBinding binding{};
      binding.from_names = _GetPackageStringList(cJSON_GetObjectItemCaseSensitive(entry_item, "from"));
      const cJSON* to_item = cJSON_GetObjectItemCaseSensitive(entry_item, "to");
      if (to_item && cJSON_IsObject(to_item)) {
        binding.target_container_name = _GetPackageStringField(to_item, "container");
        binding.target_indices = _GetPackageUintList(cJSON_GetObjectItemCaseSensitive(to_item, "indices"));
      } else {
        binding.target_container_names = _GetPackageStringList(to_item);
      }

      if (binding.from_names.empty()) {
        schema.error_message = "Description entry is missing source names: " + package_json_path.generic_string();
        cJSON_Delete(root);
        return schema;
      }
      if (binding.target_container_name.empty() &&
          binding.target_container_names.empty() &&
          binding.target_indices.empty()) {
        schema.error_message = "Description entry is missing targets: " + package_json_path.generic_string();
        cJSON_Delete(root);
        return schema;
      }

      entry.bindings.push_back(std::move(binding));

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
