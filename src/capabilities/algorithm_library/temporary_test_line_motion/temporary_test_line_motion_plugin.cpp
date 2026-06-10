#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "capabilities/agent/agent.h"
#include "capabilities/algorithm_library/algorithm_plugin_api.h"

#include "cJSON.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>

namespace {

struct TemporaryTestResourceEntry {
  std::string resource_name;
  std::string resource_kind;
  bool required{true};
};

struct TemporaryTestDescriptorEntry {
  std::string descriptor_name;
  std::string container_name;
  uint32_t array_index{0u};
};

struct TemporaryTestSchema {
  std::vector<TemporaryTestResourceEntry> required_resources;
  std::vector<TemporaryTestDescriptorEntry> descriptor_entries;
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
  if (kind == "result_render" || kind == "algorithm_result_render") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::ResultRender;
    return true;
  }
  if (kind == "fill_signal" || kind == "signal_fill") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::FillSignal;
    return true;
  }
  if (kind == "resource_refill" || kind == "resource_rebind") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::ResourceRefill;
    return true;
  }

  *out_stage_kind = agent::AlgorithmInterventionStageKind::Custom;
  return true;
}

TemporaryTestInterventionSchema _LoadInterventionSchema(const algorithm_library_plugin::AlgorithmPluginRequest& request) {
  TemporaryTestInterventionSchema schema{};
  const std::string path = _BuildPath(request, "_intervention.json");
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

  const cJSON* stages = cJSON_GetObjectItemCaseSensitive(root, "stages");
  if (!stages || !cJSON_IsObject(stages)) {
    schema.error_message = "Intervention JSON file " + path + " is missing a 'stages' object.";
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
  if (!schema.valid && schema.error_message.empty()) {
    schema.error_message = path + " does not contain any intervention stages.";
  }
  return schema;
}

TemporaryTestSchema _LoadSchema(const algorithm_library_plugin::AlgorithmPluginRequest& request) {
  TemporaryTestSchema schema{};
  const std::string path = _BuildPath(request, "_decomposer.json");
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

  const cJSON* required_resources = cJSON_GetObjectItemCaseSensitive(root, "required_resources");
  if (required_resources && cJSON_IsArray(required_resources)) {
    const int resource_count = cJSON_GetArraySize(required_resources);
    schema.required_resources.reserve(resource_count > 0 ? static_cast<size_t>(resource_count) : 0u);
    for (int i = 0; i < resource_count; ++i) {
      const cJSON* item = cJSON_GetArrayItem(required_resources, i);
      if (!item || !cJSON_IsObject(item)) {
        continue;
      }
      TemporaryTestResourceEntry entry{};
      entry.resource_name = _GetStringField(item, "name");
      entry.resource_kind = _GetStringField(item, "kind");
      entry.required = _GetBoolField(item, "required", true);
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
      TemporaryTestDescriptorEntry entry{};
      entry.descriptor_name = _GetStringField(item, "descriptor");
      entry.container_name = _GetStringField(item, "container");
      entry.array_index = _GetUintField(item, "array_index");
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

class TemporaryTestLineMotionDecomposer final : public agent::IAlgorithmPackageDecomposer {
 public:
  explicit TemporaryTestLineMotionDecomposer(TemporaryTestSchema schema)
    : schema_(std::move(schema)) {}

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
    for (const TemporaryTestDescriptorEntry& entry : schema_.descriptor_entries) {
      out_requested_descriptor_bindings->descriptor_slots.push_back(
        agent::AlgorithmRequestedDescriptorBindings::DescriptorSlot{
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

    uint32_t tuple_width = 0u;
    for (const TemporaryTestDescriptorEntry& entry : schema_.descriptor_entries) {
      tuple_width = std::max(tuple_width, entry.array_index + 1u);
    }
    if (tuple_width == 0u) {
      if (out_error_message) {
        *out_error_message = "No descriptor-to-container mapping available for '" +
          algorithm_profile.algorithm_name + "'.";
      }
      return false;
    }

    std::vector<float> tuple_values(tuple_width, 0.0f);
    for (const TemporaryTestDescriptorEntry& entry : schema_.descriptor_entries) {
      const agent::AlgorithmDescriptorValue* value = _FindDescriptorValue(descriptor_values, entry.descriptor_name);
      if (!value) {
        if (out_error_message) {
          *out_error_message = "Missing descriptor value '" + entry.descriptor_name + "' for '" +
            algorithm_profile.algorithm_name + "'.";
        }
        return false;
      }
      if (entry.array_index >= tuple_values.size()) {
        if (out_error_message) {
          *out_error_message = "Unsupported descriptor mapping '" + entry.descriptor_name + "' for container '" +
            entry.container_name + "'.";
        }
        return false;
      }
      tuple_values[entry.array_index] = value->scalar_value;
    }

    for (const TemporaryTestDescriptorEntry& entry : schema_.descriptor_entries) {
      AlgorithmContainer* container = FindAlgorithmContainer(container_set, entry.container_name);
      if (!container) {
        if (out_error_message) {
          *out_error_message = "Missing target container '" + entry.container_name + "' for '" +
            algorithm_profile.algorithm_name + "'.";
        }
        return false;
      }
      if (container->storage_kind != AlgorithmContainerStorageKind::Array) {
        if (out_error_message) {
          *out_error_message = "Target container '" + entry.container_name + "' is not an array for '" +
            algorithm_profile.algorithm_name + "'.";
        }
        return false;
      }
      if (container->element_stride < sizeof(float) * tuple_width) {
        if (out_error_message) {
          *out_error_message = "Target container '" + entry.container_name + "' has insufficient element stride for '" +
            algorithm_profile.algorithm_name + "'.";
        }
        return false;
      }
      if (container->bytes.size() < sizeof(float) * tuple_width) {
        if (out_error_message) {
          *out_error_message = "Target container '" + entry.container_name + "' has insufficient storage for '" +
            algorithm_profile.algorithm_name + "'.";
        }
        return false;
      }
      std::memcpy(container->bytes.data(), tuple_values.data(), sizeof(float) * tuple_width);
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

    algorithm::AlgorithmContainer* container = algorithm::FindAlgorithmContainer(algorithm_container_set, "pos");
    if (!container) {
      return false;
    }

    _AdvancePositionContainer(container);
    if (algorithm_to_agent_signal) {
      *algorithm_to_agent_signal = {};
    }
    if (debug_state) {
      debug_state->signals.push_back(codec::AdvancedAlgorithmDebugSignal{
        .name = "temporary_test_line_motion.cpu_tick",
        .payload = "Advanced pos on the main thread.",
      });
    }
    return true;
  }
};

void _DestroyDecomposer(agent::IAlgorithmPackageDecomposer* decomposer) {
  delete decomposer;
}

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

  const TemporaryTestSchema schema = _LoadSchema(*request);
  if (!schema.valid) {
    return false;
  }

  const TemporaryTestInterventionSchema intervention_schema = _LoadInterventionSchema(*request);
  if (!intervention_schema.valid) {
    return false;
  }

  out_bundle->decomposer = new TemporaryTestLineMotionDecomposer(schema);
  out_bundle->destroy_decomposer = &_DestroyDecomposer;
  out_bundle->intervention = new TemporaryTestLineMotionIntervention(intervention_schema);
  out_bundle->destroy_intervention = &_DestroyIntervention;
  out_bundle->temporary_test_executor = new TemporaryTestLineMotionMainThreadExecutor();
  out_bundle->destroy_temporary_test_executor = &_DestroyTemporaryTestExecutor;
  return true;
}

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
  const algorithm_library_plugin::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector) {
  if (!request || !out_reflector) {
    return false;
  }

  return algorithm_management::TryCreateAlgorithmReflectorFromAlgorithmName(
    request->algorithm_name ? request->algorithm_name : "",
    out_reflector,
    nullptr);
}
