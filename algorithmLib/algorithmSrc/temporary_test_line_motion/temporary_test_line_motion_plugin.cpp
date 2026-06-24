#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "agent/agent.h"
#include "../algorithm_plugin_api.h"

#include "cJSON.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string_view>

namespace {

struct TemporaryTestResourceEntry {
  std::string resource_name;
  std::string resource_kind;
  std::string container_name;
  bool required{true};
};

struct TemporaryTestDescriptionBinding {
  std::string name;
  std::vector<std::string> from_names;
  std::string target_container_name;
  std::vector<uint32_t> target_indices;
  std::vector<std::string> target_container_names;
};

struct TemporaryTestDescriptionEntry {
  std::string name;
  std::vector<TemporaryTestDescriptionBinding> bindings;
};

struct TemporaryTestSchema {
  std::vector<TemporaryTestResourceEntry> required_resources;
  std::vector<TemporaryTestDescriptionEntry> description_entries;
  bool valid{false};
  std::string error_message;
};

struct TemporaryTestInterventionSchema {
  std::vector<agent::AlgorithmInterventionStageSpec> stage_specs;
  bool valid{false};
  std::string error_message;
};

std::string _ReadTextFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

std::string _GetStringField(const cJSON* object, const char* key) {
  if (!object || !key) {
    return {};
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item || !cJSON_IsString(item) || !item->valuestring) {
    return {};
  }
  return item->valuestring;
}

std::vector<std::string> _GetStringList(const cJSON* item) {
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

std::vector<std::string> _GetStringListField(
  const cJSON* object,
  std::initializer_list<const char*> keys) {
  if (!object || !cJSON_IsObject(object)) {
    return {};
  }

  for (const char* key : keys) {
    if (!key) {
      continue;
    }
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    std::vector<std::string> values = _GetStringList(item);
    if (!values.empty()) {
      return values;
    }
  }

  return {};
}

std::vector<uint32_t> _GetUintList(const cJSON* item) {
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

uint32_t _GetUintField(const cJSON* object, const char* key, uint32_t fallback = 0u) {
  if (!object || !key) {
    return fallback;
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item || !cJSON_IsNumber(item) || item->valuedouble < 0.0) {
    return fallback;
  }
  return static_cast<uint32_t>(item->valuedouble);
}

float _GetFloatField(const cJSON* object, const char* key, float fallback = 0.0f) {
  if (!object || !key) {
    return fallback;
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item || !cJSON_IsNumber(item)) {
    return fallback;
  }
  return static_cast<float>(item->valuedouble);
}

bool _GetBoolField(const cJSON* object, const char* key, bool fallback = false) {
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

std::string _BuildPath(
  const algorithm_library_plugin::AlgorithmPluginRequest& request,
  const std::string& suffix) {
  const std::string root = request.algorithm_library_root ? request.algorithm_library_root : "";
  const std::string folder = request.algorithm_folder && *request.algorithm_folder
    ? request.algorithm_folder
    : (request.algorithm_name ? request.algorithm_name : "");
  return root + "/" + folder + "/" + folder + suffix;
}

bool _ParseInterventionStageKind(
  const std::string& stage_name,
  const std::string& stage_kind_text,
  agent::AlgorithmInterventionStageKind* out_stage_kind) {
  if (!out_stage_kind) {
    return false;
  }

  const std::string kind = !stage_kind_text.empty() ? stage_kind_text : stage_name;
  if (kind == "resultRender") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::ResultRender;
    return true;
  }
  if (kind == "preTick") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::PreExecution;
    return true;
  }
  if (kind == "postExecution") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::PostExecution;
    return true;
  }
  if (kind == "inExecution") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::InExecution;
    return true;
  }

  *out_stage_kind = agent::AlgorithmInterventionStageKind::Custom;
  return true;
}

TemporaryTestInterventionSchema _LoadInterventionSchema(const algorithm_library_plugin::AlgorithmPluginRequest& request) {
  TemporaryTestInterventionSchema schema{};
  const std::string path = _BuildPath(request, "_package.json");
  const std::string json_text = _ReadTextFile(path);
  if (json_text.empty()) {
    schema.error_message = "Failed to read intervention JSON file: " + path;
    return schema;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    schema.error_message = "Failed to parse intervention JSON file: " + path;
    return schema;
  }

  const cJSON* intervention = cJSON_GetObjectItemCaseSensitive(root, "intervention");
  if (!intervention || !cJSON_IsObject(intervention)) {
    cJSON_Delete(root);
    return schema;
  }

  const cJSON* stages = cJSON_GetObjectItemCaseSensitive(intervention, "stages");
  if (!stages || !cJSON_IsObject(stages)) {
    cJSON_Delete(root);
    return schema;
  }

  for (const cJSON* stage_item = stages->child; stage_item; stage_item = stage_item->next) {
    if (!stage_item || !cJSON_IsObject(stage_item) || !stage_item->string) {
      continue;
    }

    const std::string stage_key = stage_item->string;
    agent::AlgorithmInterventionStageSpec stage_spec{};
    stage_spec.stage_name = _GetStringField(stage_item, "stage_name");
    if (stage_spec.stage_name.empty()) {
      stage_spec.stage_name = stage_key;
    }

    const std::string stage_kind_text = _GetStringField(stage_item, "stage_kind");
    if (!_ParseInterventionStageKind(stage_spec.stage_name, stage_kind_text, &stage_spec.stage_kind)) {
      schema.error_message = "Invalid stage kind in intervention JSON file: " + path;
      cJSON_Delete(root);
      return schema;
    }

    const cJSON* used_containers = cJSON_GetObjectItemCaseSensitive(stage_item, "used_algorithm_containers");
    if (used_containers && cJSON_IsObject(used_containers)) {
      const cJSON* arrays = cJSON_GetObjectItemCaseSensitive(used_containers, "arrays");
      if (arrays && cJSON_IsArray(arrays)) {
        const int container_count = cJSON_GetArraySize(arrays);
        stage_spec.used_algorithm_containers.reserve(container_count > 0 ? static_cast<size_t>(container_count) : 0u);
        for (int i = 0; i < container_count; ++i) {
          const cJSON* item = cJSON_GetArrayItem(arrays, i);
          if (!item) {
            continue;
          }

          agent::AlgorithmInterventionContainerBinding binding{};
          if (cJSON_IsString(item) && item->valuestring) {
            binding.container_name = item->valuestring;
            binding.container_kind = "array";
            binding.tuple_width = 3u;
            binding.required = true;
          } else if (cJSON_IsObject(item)) {
            binding.container_name = _GetStringField(item, "name");
            if (binding.container_name.empty()) {
              binding.container_name = _GetStringField(item, "container");
            }
            binding.container_kind = _GetStringField(item, "kind");
            if (binding.container_kind.empty()) {
              binding.container_kind = "array";
            }
            binding.tuple_width = _GetUintField(item, "tuple_width", 3u);
            if (binding.tuple_width == 0u) {
              binding.tuple_width = 3u;
            }
            binding.required = _GetBoolField(item, "required", true);
          }

          if (binding.container_name.empty()) {
            schema.error_message = "Invalid array container binding in intervention JSON file: " + path;
            cJSON_Delete(root);
            return schema;
          }
          stage_spec.used_algorithm_containers.push_back(std::move(binding));
        }
      }
    }

    const cJSON* shader = cJSON_GetObjectItemCaseSensitive(stage_item, "shader");
    if (shader && cJSON_IsObject(shader)) {
      stage_spec.shader.vertex_shader_path = _GetStringField(shader, "vertex");
      stage_spec.shader.fragment_shader_path = _GetStringField(shader, "fragment");
      stage_spec.shader.pipeline_kind = _GetStringField(shader, "pipeline");
    }

    if (stage_spec.stage_kind == agent::AlgorithmInterventionStageKind::ResultRender) {
      if (stage_spec.used_algorithm_containers.empty()) {
        schema.error_message = "Result-render stage in intervention JSON file must bind at least one array container: " + path;
        cJSON_Delete(root);
        return schema;
      }
      if (stage_spec.shader.vertex_shader_path.empty() || stage_spec.shader.fragment_shader_path.empty()) {
        schema.error_message = "Result-render stage in intervention JSON file is missing shader paths: " + path;
        cJSON_Delete(root);
        return schema;
      }
    }

    schema.stage_specs.push_back(std::move(stage_spec));
  }

  cJSON_Delete(root);
  schema.valid = !schema.stage_specs.empty();
  return schema;
}

TemporaryTestSchema _LoadSchema(const algorithm_library_plugin::AlgorithmPluginRequest& request) {
  TemporaryTestSchema schema{};
  const std::string path = _BuildPath(request, "_package.json");
  const std::string json_text = _ReadTextFile(path);
  if (json_text.empty()) {
    schema.error_message = "Failed to read decomposer JSON file: " + path;
    return schema;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    schema.error_message = "Failed to parse decomposer JSON file: " + path;
    return schema;
  }

  const cJSON* decomposer = cJSON_GetObjectItemCaseSensitive(root, "decomposer");
  if (!decomposer || !cJSON_IsObject(decomposer)) {
    schema.error_message = "Package JSON file " + path + " is missing a 'decomposer' object.";
    cJSON_Delete(root);
    return schema;
  }

  const cJSON* resources = cJSON_GetObjectItemCaseSensitive(decomposer, "res");
  if (!resources || !cJSON_IsObject(resources)) {
    schema.error_message = "Package JSON file " + path + " is missing a 'res' object.";
    cJSON_Delete(root);
    return schema;
  }
  for (const cJSON* resource_group = resources->child; resource_group; resource_group = resource_group->next) {
    if (!resource_group->string || !cJSON_IsObject(resource_group)) {
      schema.error_message = "Invalid resource group in " + path + ".";
      cJSON_Delete(root);
      return schema;
    }

    const std::string resource_kind = resource_group->string;
    for (const cJSON* resource_item = resource_group->child; resource_item; resource_item = resource_item->next) {
      if (!resource_item->string || !cJSON_IsString(resource_item) || !resource_item->valuestring) {
        schema.error_message = "Invalid resource binding in " + path + ".";
        cJSON_Delete(root);
        return schema;
      }

      TemporaryTestResourceEntry entry{};
      entry.resource_kind = resource_kind;
      entry.resource_name = resource_item->string;
      entry.container_name = resource_item->valuestring;
      entry.required = true;
      schema.required_resources.push_back(std::move(entry));
    }
  }

  const cJSON* description_entries = cJSON_GetObjectItemCaseSensitive(decomposer, "description");
  if (!description_entries || !cJSON_IsArray(description_entries)) {
    schema.error_message = "Package JSON file " + path + " is missing a 'description' array.";
    cJSON_Delete(root);
    return schema;
  }

  const int descriptor_count = cJSON_GetArraySize(description_entries);
  schema.description_entries.reserve(descriptor_count > 0 ? static_cast<size_t>(descriptor_count) : 0u);
  for (int i = 0; i < descriptor_count; ++i) {
    const cJSON* item = cJSON_GetArrayItem(description_entries, i);
    if (!item || !cJSON_IsObject(item)) {
      schema.error_message = "Invalid description entry in " + path + ".";
      cJSON_Delete(root);
      return schema;
    }

    TemporaryTestDescriptionEntry entry{};
    entry.name = _GetStringField(item, "name");
    TemporaryTestDescriptionBinding binding{};
    binding.name = entry.name;
    binding.from_names = _GetStringListField(item, {"from"});
    const cJSON* to_item = cJSON_GetObjectItemCaseSensitive(item, "to");
    if (to_item && cJSON_IsObject(to_item)) {
      binding.target_container_name = _GetStringField(to_item, "container");
      binding.target_indices = _GetUintList(cJSON_GetObjectItemCaseSensitive(to_item, "indices"));
    } else {
      binding.target_container_names = _GetStringList(to_item);
    }

    if (binding.from_names.empty()) {
      schema.error_message = "Description entry is missing source names in " + path + ".";
      cJSON_Delete(root);
      return schema;
    }
    if (binding.target_container_name.empty() &&
        binding.target_container_names.empty() &&
        binding.target_indices.empty()) {
      schema.error_message = "Description entry is missing targets in " + path + ".";
      cJSON_Delete(root);
      return schema;
    }

    entry.bindings.push_back(std::move(binding));
    schema.description_entries.push_back(std::move(entry));
  }

  cJSON_Delete(root);
  schema.valid = !schema.required_resources.empty() && !schema.description_entries.empty();
  if (!schema.valid && schema.error_message.empty()) {
    schema.error_message = path + " does not contain res or description entries.";
  }
  return schema;
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

class TemporaryTestLineMotionDecomposer final {
 public:
  explicit TemporaryTestLineMotionDecomposer(TemporaryTestSchema schema)
    : schema_(std::move(schema)) {}

  bool GetRequestedResources(
    const algorithm::AlgorithmProfile& algorithm_profile,
    agent::AlgorithmRequestedResources* out_requested_resources) const {
    if (!out_requested_resources) {
      return false;
    }
    *out_requested_resources = {};
    out_requested_resources->algorithm_name = algorithm_profile.algorithm_name;
    if (!schema_.valid) {
      return false;
    }
    out_requested_resources->required_resources.reserve(schema_.required_resources.size());
    for (const TemporaryTestResourceEntry& entry : schema_.required_resources) {
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
    agent::AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings) const {
    if (!out_requested_descriptor_bindings) {
      return false;
    }
    *out_requested_descriptor_bindings = {};
    out_requested_descriptor_bindings->algorithm_name = algorithm_profile.algorithm_name;
    if (!schema_.valid) {
      return false;
    }
    for (const TemporaryTestDescriptionEntry& entry : schema_.description_entries) {
      for (const TemporaryTestDescriptionBinding& binding : entry.bindings) {
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
    const std::vector<agent::AlgorithmResourceBinding>& resource_bindings,
    const std::vector<agent::AlgorithmDescriptorValue>& descriptor_values,
    algorithm::AlgorithmContainerSet* container_set,
    std::string* out_error_message) const {
    if (!container_set) {
      if (out_error_message) {
        *out_error_message = "AlgorithmContainerSet output pointer is null.";
      }
      return false;
    }
    if (!schema_.valid) {
      if (out_error_message) {
        *out_error_message = schema_.error_message;
      }
      return false;
    }

    for (const TemporaryTestResourceEntry& entry : schema_.required_resources) {
      if (!entry.required) {
        continue;
      }
      const agent::AlgorithmResourceBinding* binding = _FindResourceBinding(resource_bindings, entry.resource_name);
      if (!binding) {
        if (out_error_message) {
          *out_error_message = "Missing required resource binding '" + entry.resource_name + "' for '" +
            algorithm_profile.algorithm_name + "'.";
        }
        return false;
      }
      if (entry.resource_kind == "mesh") {
        if (binding->source_path.empty()) {
          if (out_error_message) {
            *out_error_message = "Required mesh resource '" + entry.resource_name + "' has no source path for '" +
              algorithm_profile.algorithm_name + "'.";
          }
          return false;
        }
        std::ifstream file(binding->source_path, std::ios::binary);
        if (!file) {
          if (out_error_message) {
            *out_error_message = "Required mesh resource file '" + binding->source_path + "' could not be opened for '" +
              algorithm_profile.algorithm_name + "'.";
          }
          return false;
        }
      }
    }

    for (const TemporaryTestDescriptionEntry& entry : schema_.description_entries) {
      for (const TemporaryTestDescriptionBinding& binding : entry.bindings) {
        if (!binding.target_container_names.empty()) {
          if (binding.from_names.size() != binding.target_container_names.size() &&
              binding.from_names.size() != 1u) {
            if (out_error_message) {
              *out_error_message = "Description entry '" + entry.name + "' has mismatched source and target counts for '" +
                algorithm_profile.algorithm_name + "'.";
            }
            return false;
          }
          for (size_t i = 0; i < binding.target_container_names.size(); ++i) {
            const size_t from_index = binding.from_names.size() == 1u ? 0u : i;
            const agent::AlgorithmDescriptorValue* value = _FindDescriptorValue(descriptor_values, binding.from_names[from_index]);
            if (!value) {
              if (out_error_message) {
                *out_error_message = "Missing descriptor value '" + binding.from_names[from_index] + "' for '" +
                  algorithm_profile.algorithm_name + "'.";
              }
              return false;
            }

            AlgorithmContainer* container = FindAlgorithmContainer(container_set, binding.target_container_names[i]);
            if (!container) {
              if (out_error_message) {
                *out_error_message = "Missing target container '" + binding.target_container_names[i] + "' for '" +
                  algorithm_profile.algorithm_name + "'.";
              }
              return false;
            }
            if (!_WriteFloatValue(container, value->scalar_value)) {
              if (out_error_message) {
                *out_error_message = "Failed to write descriptor value into container '" + binding.target_container_names[i] + "' for '" +
                  algorithm_profile.algorithm_name + "'.";
              }
              return false;
            }
          }
          continue;
        }

        if (binding.target_container_name.empty() || binding.target_indices.empty()) {
          if (out_error_message) {
            *out_error_message = "Description entry '" + entry.name + "' is missing a target container for '" +
              algorithm_profile.algorithm_name + "'.";
          }
          return false;
        }
        if (binding.from_names.size() != binding.target_indices.size() &&
            binding.from_names.size() != 1u) {
          if (out_error_message) {
            *out_error_message = "Description entry '" + entry.name + "' has mismatched source and target counts for '" +
              algorithm_profile.algorithm_name + "'.";
          }
          return false;
        }

        AlgorithmContainer* container = FindAlgorithmContainer(container_set, binding.target_container_name);
        if (!container) {
          if (out_error_message) {
            *out_error_message = "Missing target container '" + binding.target_container_name + "' for '" +
              algorithm_profile.algorithm_name + "'.";
          }
          return false;
        }
        if (container->storage_kind != AlgorithmContainerStorageKind::Array) {
          if (out_error_message) {
            *out_error_message = "Target container '" + binding.target_container_name + "' is not an array for '" +
              algorithm_profile.algorithm_name + "'.";
          }
          return false;
        }

        for (size_t i = 0; i < binding.target_indices.size(); ++i) {
          const uint32_t target_index = binding.target_indices[i];
          if (target_index == 0u) {
            if (out_error_message) {
              *out_error_message = "Description entry '" + entry.name + "' uses an invalid target index for '" +
                algorithm_profile.algorithm_name + "'.";
            }
            return false;
          }
          const size_t from_index = binding.from_names.size() == 1u ? 0u : i;
          const agent::AlgorithmDescriptorValue* value = _FindDescriptorValue(descriptor_values, binding.from_names[from_index]);
          if (!value) {
            if (out_error_message) {
              *out_error_message = "Missing descriptor value '" + binding.from_names[from_index] + "' for '" +
                algorithm_profile.algorithm_name + "'.";
            }
            return false;
          }
          if (!_WriteFloatValueAtIndex(container, target_index - 1u, value->scalar_value)) {
            if (out_error_message) {
              *out_error_message = "Failed to write descriptor value into container '" + binding.target_container_name + "' for '" +
                algorithm_profile.algorithm_name + "'.";
            }
            return false;
          }
        }
      }
    }

    return true;
  }

 private:
  TemporaryTestSchema schema_{};
};

class TemporaryTestLineMotionIntervention final : public agent::IAlgorithmIntervention {
 public:
  explicit TemporaryTestLineMotionIntervention(TemporaryTestInterventionSchema schema)
    : schema_(std::move(schema)) {}

  bool SupportsIntervention() const override {
    return schema_.valid && !schema_.stage_specs.empty();
  }

  void FillAgentToAlgorithmSignal(
    const agent::AgentTickContext& context,
    AgentToAlgorithmSignal* out_signal) const override {
    if (!out_signal) {
      return;
    }

    *out_signal = {};
    out_signal->needs_intervention = context.intervention_request && context.intervention_request->enabled;
  }

  bool GetInterventionStageSpecs(
    std::vector<agent::AlgorithmInterventionStageSpec>* out_stage_specs) const override {
    if (!out_stage_specs) {
      return false;
    }
    *out_stage_specs = schema_.stage_specs;
    return schema_.valid && !schema_.stage_specs.empty();
  }

 private:
  TemporaryTestInterventionSchema schema_{};
};

void _AdvancePositionContainer(algorithm::AlgorithmContainer* container) {
  if (!container || container->storage_kind != algorithm::AlgorithmContainerStorageKind::Array) {
    return;
  }
  if (container->element_stride < sizeof(float) * 3u) {
    return;
  }
  if (container->bytes.size() < sizeof(float) * 3u) {
    return;
  }

  const size_t element_count = container->element_stride == 0u
    ? 0u
    : container->bytes.size() / container->element_stride;
  for (size_t index = 0; index < element_count; ++index) {
    const size_t byte_offset = index * container->element_stride;
    float xyz[3]{};
    std::memcpy(xyz, container->bytes.data() + byte_offset, sizeof(xyz));
    xyz[0] += 0.10f;
    xyz[1] += 0.03f;
    std::memcpy(container->bytes.data() + byte_offset, xyz, sizeof(xyz));
  }
}

class TemporaryTestLineMotionMainThreadExecutor final : public agent::IAlgorithmtemporaryTestMainThreadExecutor {
 public:
  bool temporaryTestExecuteOnMainThread(
    const agent::AgentTickContext& context,
    const algorithm::AlgorithmProfile& algorithm_profile,
    const AgentToAlgorithmSignal& agent_to_algorithm_signal,
    algorithm::AlgorithmContainerSet* algorithm_container_set,
    AlgorithmToAgentSignal* algorithm_to_agent_signal,
    agent::AlgorithmPackageDebugState* debug_state) override {
    (void)context;
    (void)algorithm_profile;
    (void)agent_to_algorithm_signal;
    if (!algorithm_container_set) {
      return false;
    }

    algorithm::AlgorithmContainer* container = algorithm::FindAlgorithmContainer(algorithm_container_set, "a1");
    if (!container) {
      return false;
    }

    _AdvancePositionContainer(container);
    if (algorithm_to_agent_signal) {
      *algorithm_to_agent_signal = {};
    }
    if (debug_state) {
      debug_state->signals.push_back(algorithm_management::AdvancedAlgorithmDebugSignal{
        .name = "temporary_test_line_motion.body",
        .payload = "Advanced a1 on the main thread.",
      });
    }
    return true;
  }
};

void _DestroyIntervention(agent::IAlgorithmIntervention* intervention) {
  delete intervention;
}

void _DestroyTemporaryTestExecutor(agent::IAlgorithmtemporaryTestMainThreadExecutor* executor) {
  delete executor;
}

}  // namespace

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algorithm_library_plugin::AlgorithmPluginRequest* request,
  algorithm_library_plugin::AlgorithmPluginBundle* out_bundle) {
  if (!request || !out_bundle) {
    return false;
  }

  out_bundle->Clear();
  out_bundle->cpu_symbol = true;
  out_bundle->gpu_symbol = true;
  out_bundle->temporary_test_executor = new TemporaryTestLineMotionMainThreadExecutor();
  out_bundle->destroy_temporary_test_executor = &_DestroyTemporaryTestExecutor;
  return true;
}
