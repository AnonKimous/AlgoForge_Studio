#include "algorithm_support/algorithm_protocol.h"
#include "algorithm_management/algorithm_abi.h"

#include "algorithm_support/algorithm_container_manifest.h"
#include "algorithm_support/algorithm_package_location.h"

#include "cJSON.h"

#include <algorithm>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>

namespace algorithm_support {

namespace {

struct temporaryTestDecomposerResourceEntry {
  std::string resource_name;
  std::string resource_kind;
  bool required{true};
};

struct temporaryTestDecomposerDescriptorEntry {
  std::string descriptor_name;
  std::string container_name;
  uint32_t array_index{0u};
};

struct temporaryTestDecomposerSchema {
  std::vector<temporaryTestDecomposerResourceEntry> required_resources;
  std::vector<temporaryTestDecomposerDescriptorEntry> descriptor_entries;
  bool valid{false};
  std::string error_message;
};

void _SetErrorMessage(std::string* out_error_message, std::string message) {
  if (out_error_message) {
    *out_error_message = std::move(message);
  }
}

std::string _ReadtemporaryTestTextFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

std::string _GettemporaryTestStringField(const cJSON* object, const char* key) {
  if (!object || !key) {
    return {};
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item || !cJSON_IsString(item) || !item->valuestring) {
    return {};
  }
  return item->valuestring;
}

uint32_t _GettemporaryTestUintField(const cJSON* object, const char* key, uint32_t fallback = 0u) {
  if (!object || !key) {
    return fallback;
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item || !cJSON_IsNumber(item) || item->valuedouble < 0.0) {
    return fallback;
  }
  return static_cast<uint32_t>(item->valuedouble);
}

bool _GettemporaryTestBoolField(const cJSON* object, const char* key, bool fallback = false) {
  if (!object || !key) {
    return fallback;
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item) {
    return fallback;
  }
  if (item->type & (cJSON_True | cJSON_False)) {
    return (item->type & cJSON_True) != 0;
  }
  return fallback;
}

std::string _AlgorithmNameFromLocation(const algorithm::AlgorithmPackageLocation& package_location) {
  if (!package_location.algorithm_name.empty()) {
    return package_location.algorithm_name;
  }
  return package_location.manifest_name;
}

std::filesystem::path _BuildDecomposerJsonPath(const algorithm::AlgorithmPackageLocation& package_location) {
  const std::string algorithm_name = _AlgorithmNameFromLocation(package_location);
  if (algorithm_name.empty()) {
    return {};
  }

  std::filesystem::path package_root = package_location.package_root;
  if (package_root.empty()) {
    package_root = package_location.manifest_path.parent_path();
  }
  if (package_root.empty()) {
    return {};
  }

  return package_root / (algorithm_name + "_decomposer.json");
}

bool _ResolveDecomposerJsonPath(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::string* out_path) {
  if (!out_path) {
    return false;
  }

  const std::string algorithm_name = _AlgorithmNameFromLocation(package_location);
  const std::filesystem::path primary_path = _BuildDecomposerJsonPath(package_location);
  if (!primary_path.empty()) {
    std::ifstream primary_file(primary_path.string(), std::ios::binary);
    if (primary_file) {
      *out_path = primary_path.string();
      return true;
    }
  }

  if (!algorithm_name.empty() && !package_location.package_root.empty()) {
    const std::filesystem::path legacy_path =
      package_location.package_root.parent_path() / (algorithm_name + "_decomposer.json");
    std::ifstream legacy_file(legacy_path.string(), std::ios::binary);
    if (legacy_file) {
      *out_path = legacy_path.string();
      return true;
    }
  }

  if (!primary_path.empty()) {
    *out_path = primary_path.string();
  } else if (!algorithm_name.empty()) {
    *out_path = algorithm_name + "_decomposer.json";
  } else {
    out_path->clear();
  }
  return false;
}

temporaryTestDecomposerSchema _LoadtemporaryTestDecomposerSchema(
  const algorithm::AlgorithmPackageLocation& package_location) {
  temporaryTestDecomposerSchema schema{};
  std::string path;
  if (!_ResolveDecomposerJsonPath(package_location, &path)) {
    const std::string algorithm_name = _AlgorithmNameFromLocation(package_location);
    schema.error_message = algorithm_name.empty()
      ? "Failed to resolve decomposer JSON file."
      : "Failed to resolve decomposer JSON file for '" + algorithm_name + "'.";
    return schema;
  }

  const std::string json_text = _ReadtemporaryTestTextFile(path);
  if (json_text.empty()) {
    schema.error_message = "Failed to read decomposer JSON file: " + path;
    return schema;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    schema.error_message = "Failed to parse decomposer JSON file: " + path;
    return schema;
  }

  const cJSON* required_resources = cJSON_GetObjectItemCaseSensitive(root, "required_resources");
  if (required_resources && cJSON_IsArray(required_resources)) {
    const int resource_count = cJSON_GetArraySize(required_resources);
    schema.required_resources.reserve(resource_count > 0 ? static_cast<size_t>(resource_count) : 0u);
    for (int i = 0; i < resource_count; ++i) {
      const cJSON* item = cJSON_GetArrayItem(required_resources, i);
      if (!item || !cJSON_IsObject(item)) {
        continue;
      }

      temporaryTestDecomposerResourceEntry entry{};
      entry.resource_name = _GettemporaryTestStringField(item, "name");
      entry.resource_kind = _GettemporaryTestStringField(item, "kind");
      entry.required = _GettemporaryTestBoolField(item, "required", true);
      if (entry.resource_name.empty() || entry.resource_kind.empty()) {
        schema.error_message = "Invalid required_resources entry in " + path + ".";
        cJSON_Delete(root);
        return schema;
      }
      schema.required_resources.push_back(std::move(entry));
    }
  }

  const cJSON* descriptor_entries = cJSON_GetObjectItemCaseSensitive(root, "descriptor_to_container");
  if (descriptor_entries && cJSON_IsArray(descriptor_entries)) {
    const int descriptor_count = cJSON_GetArraySize(descriptor_entries);
    schema.descriptor_entries.reserve(descriptor_count > 0 ? static_cast<size_t>(descriptor_count) : 0u);
    for (int i = 0; i < descriptor_count; ++i) {
      const cJSON* item = cJSON_GetArrayItem(descriptor_entries, i);
      if (!item || !cJSON_IsObject(item)) {
        continue;
      }

      temporaryTestDecomposerDescriptorEntry entry{};
      entry.descriptor_name = _GettemporaryTestStringField(item, "descriptor");
      entry.container_name = _GettemporaryTestStringField(item, "container");
      entry.array_index = _GettemporaryTestUintField(item, "array_index");
      if (entry.descriptor_name.empty() || entry.container_name.empty()) {
        schema.error_message = "Invalid descriptor_to_container entry in " + path + ".";
        cJSON_Delete(root);
        return schema;
      }
      schema.descriptor_entries.push_back(std::move(entry));
    }
  }

  cJSON_Delete(root);
  schema.valid = !schema.descriptor_entries.empty();
  if (!schema.valid && schema.error_message.empty()) {
    schema.error_message = path + " does not contain descriptor_to_container entries.";
  }
  return schema;
}

const agent::AlgorithmDescriptorValue* _FindtemporaryTestDescriptorValue(
  const std::vector<agent::AlgorithmDescriptorValue>& descriptor_values,
  std::string_view descriptor_name) {
  for (const agent::AlgorithmDescriptorValue& value : descriptor_values) {
    if (value.descriptor_name == descriptor_name) {
      return &value;
    }
  }
  return nullptr;
}

const agent::AlgorithmResourceBinding* _FindtemporaryTestResourceBinding(
  const std::vector<agent::AlgorithmResourceBinding>& resource_bindings,
  std::string_view resource_name) {
  for (const agent::AlgorithmResourceBinding& binding : resource_bindings) {
    if (binding.resource_name == resource_name) {
      return &binding;
    }
  }
  return nullptr;
}

class AlgorithmLibrarySchemaDecomposer final : public agent::IAlgorithmPackageDecomposer {
 public:
  explicit AlgorithmLibrarySchemaDecomposer(const algorithm::AlgorithmPackageLocation& package_location)
    : schema_(_LoadtemporaryTestDecomposerSchema(package_location)) {}

  bool valid() const { return schema_.valid; }
  const std::string& error_message() const { return schema_.error_message; }

  bool GetRequestedResources(
    const algorithm::AlgorithmProfile& algorithm_profile,
    agent::AlgorithmRequestedResources* out_requested_resources) const override {
    if (!out_requested_resources) {
      return false;
    }

    *out_requested_resources = {};
    out_requested_resources->algorithm_name = algorithm_profile.algorithm_name;
    if (!schema_.valid) {
      return false;
    }

    out_requested_resources->required_resources.reserve(schema_.required_resources.size());
    for (const temporaryTestDecomposerResourceEntry& entry : schema_.required_resources) {
      out_requested_resources->required_resources.push_back(agent::AlgorithmRequestedResources::RequiredResource{
        .resource_name = entry.resource_name,
        .resource_kind = entry.resource_kind,
        .required = entry.required,
      });
    }
    out_requested_resources->valid = true;
    return true;
  }

  bool GetRequestedDescriptorBindings(
    const algorithm::AlgorithmProfile& algorithm_profile,
    agent::AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings) const override {
    if (!out_requested_descriptor_bindings) {
      return false;
    }

    *out_requested_descriptor_bindings = {};
    out_requested_descriptor_bindings->algorithm_name = algorithm_profile.algorithm_name;
    if (!schema_.valid) {
      return false;
    }

    out_requested_descriptor_bindings->descriptor_slots.reserve(schema_.descriptor_entries.size());
    for (const temporaryTestDecomposerDescriptorEntry& entry : schema_.descriptor_entries) {
      out_requested_descriptor_bindings->descriptor_slots.push_back(agent::AlgorithmRequestedDescriptorBindings::DescriptorSlot{
        .descriptor_name = entry.descriptor_name,
        .container_name = entry.container_name,
        .array_index = entry.array_index,
      });
    }
    out_requested_descriptor_bindings->valid = true;
    return true;
  }

  bool Decompose(
    const algorithm::AlgorithmProfile& algorithm_profile,
    const std::vector<agent::AlgorithmResourceBinding>& resource_bindings,
    const std::vector<agent::AlgorithmDescriptorValue>& descriptor_values,
    algorithm::AlgorithmContainerSet* container_set,
    std::string* out_error_message) const override {
    if (!container_set) {
      _SetErrorMessage(out_error_message, "AlgorithmContainerSet output pointer is null.");
      return false;
    }

    if (!schema_.valid) {
      _SetErrorMessage(out_error_message, schema_.error_message);
      return false;
    }

    for (const temporaryTestDecomposerResourceEntry& entry : schema_.required_resources) {
      if (!entry.required) {
        continue;
      }

      const agent::AlgorithmResourceBinding* binding =
        _FindtemporaryTestResourceBinding(resource_bindings, entry.resource_name);
      if (!binding) {
        _SetErrorMessage(
          out_error_message,
          "Missing required resource binding '" + entry.resource_name + "' for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }
      if (entry.resource_kind == "mesh") {
        if (binding->source_path.empty()) {
          _SetErrorMessage(
            out_error_message,
            "Required mesh resource '" + entry.resource_name + "' has no source path for '" +
              algorithm_profile.algorithm_name + "'.");
          return false;
        }
        std::ifstream file(binding->source_path, std::ios::binary);
        if (!file) {
          _SetErrorMessage(
            out_error_message,
            "Required mesh resource file '" + binding->source_path + "' could not be opened for '" +
              algorithm_profile.algorithm_name + "'.");
          return false;
        }
      }
    }

    uint32_t tuple_width = 0u;
    for (const temporaryTestDecomposerDescriptorEntry& entry : schema_.descriptor_entries) {
      tuple_width = std::max(tuple_width, entry.array_index + 1u);
    }
    if (tuple_width == 0u) {
      _SetErrorMessage(
        out_error_message,
        "No descriptor-to-container mapping available for '" + algorithm_profile.algorithm_name + "'.");
      return false;
    }

    std::vector<float> tuple_values(tuple_width, 0.0f);
    for (const temporaryTestDecomposerDescriptorEntry& entry : schema_.descriptor_entries) {
      const agent::AlgorithmDescriptorValue* value =
        _FindtemporaryTestDescriptorValue(descriptor_values, entry.descriptor_name);
      if (!value) {
        _SetErrorMessage(
          out_error_message,
          "Missing descriptor value '" + entry.descriptor_name + "' for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }
      if (entry.array_index >= tuple_values.size()) {
        _SetErrorMessage(
          out_error_message,
          "Unsupported descriptor mapping '" + entry.descriptor_name + "' for container '" +
            entry.container_name + "'.");
        return false;
      }
      tuple_values[entry.array_index] = value->scalar_value;
    }

    for (const temporaryTestDecomposerDescriptorEntry& entry : schema_.descriptor_entries) {
      algorithm::AlgorithmContainer* container = FindAlgorithmContainer(container_set, entry.container_name);
      if (!container) {
        _SetErrorMessage(
          out_error_message,
          "Missing target container '" + entry.container_name + "' for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }
      if (container->storage_kind != algorithm::AlgorithmContainerStorageKind::Array) {
        _SetErrorMessage(
          out_error_message,
          "Target container '" + entry.container_name + "' is not an array for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }
      if (container->element_stride < sizeof(float) * tuple_width) {
        _SetErrorMessage(
          out_error_message,
          "Target container '" + entry.container_name + "' has insufficient element stride for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }
      if (container->bytes.size() < sizeof(float) * tuple_width) {
        _SetErrorMessage(
          out_error_message,
          "Target container '" + entry.container_name + "' has insufficient storage for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }
      std::memcpy(container->bytes.data(), tuple_values.data(), sizeof(float) * tuple_width);
    }

    return true;
  }

 private:
  temporaryTestDecomposerSchema schema_{};
};

}  // namespace

bool CreateAlgorithmPackageDecomposerFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<agent::IAlgorithmPackageDecomposer>* out_decomposer,
  std::string* out_error_message) {
  if (!out_decomposer) {
    _SetErrorMessage(out_error_message, "Decomposer output pointer is null.");
    return false;
  }

  out_decomposer->reset();
  if (!package_location.valid) {
    _SetErrorMessage(out_error_message, "Algorithm package location is invalid.");
    return false;
  }

  const auto decomposer = std::make_shared<AlgorithmLibrarySchemaDecomposer>(package_location);
  if (!decomposer) {
    _SetErrorMessage(out_error_message, "Failed to create decomposer object.");
    return false;
  }
  if (!decomposer->valid()) {
    _SetErrorMessage(out_error_message, decomposer->error_message());
    return false;
  }
  *out_decomposer = std::move(decomposer);
  _SetErrorMessage(out_error_message, {});
  return true;
}

}  // namespace algorithm_support
