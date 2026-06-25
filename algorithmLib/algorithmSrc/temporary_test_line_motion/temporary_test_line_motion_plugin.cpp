#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../algorithm_plugin_api.h"

#include <cstring>

namespace {

constexpr float kLeftStep = -0.10f;
constexpr float kUpStep = 0.03f;

bool AdvanceScalar(algorithm::AlgorithmContainer* container, float delta) {
  if (!container) {
    return false;
  }
  if (container->storage_kind != algorithm::AlgorithmContainerStorageKind::TemporaryRegister) {
    return false;
  }
  if (container->element_stride < sizeof(float)) {
    return false;
  }
  if (container->bytes.size() < sizeof(float)) {
    return false;
  }
  float value = 0.0f;
  std::memcpy(&value, container->bytes.data(), sizeof(value));
  value += delta;
  std::memcpy(container->bytes.data(), &value, sizeof(value));
  return true;
}

<<<<<<< HEAD
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
=======
class LineMotionCpuExecutor final : public agent::IAlgorithmCpuExecutor {
>>>>>>> 0e5193b (preciser control of digital)
 public:
  bool ExecuteCpuAlgorithm(
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

    algorithm::AlgorithmContainer* point_x =
      algorithm::FindAlgorithmContainer(algorithm_container_set, "point_x");
    algorithm::AlgorithmContainer* point_y =
      algorithm::FindAlgorithmContainer(algorithm_container_set, "point_y");
    algorithm::AlgorithmContainer* point_z =
      algorithm::FindAlgorithmContainer(algorithm_container_set, "point_z");
    if (!AdvanceScalar(point_x, kLeftStep) ||
        !AdvanceScalar(point_y, kUpStep) ||
        !AdvanceScalar(point_z, 0.0f)) {
      return false;
    }

    if (algorithm_to_agent_signal) {
      *algorithm_to_agent_signal = {};
    }
    if (debug_state) {
      debug_state->signals.push_back(algorithm_management::AdvancedAlgorithmDebugSignal{
        .name = "temporary_test_line_motion.body",
        .payload = "Moved point_x/point_y toward the upper-left corner.",
      });
    }
    return true;
  }
};

void DestroyCpuExecutor(agent::IAlgorithmCpuExecutor* executor) {
  delete executor;
}

}  // namespace

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algorithmManager::support::AlgorithmPluginRequest* request,
  algorithmManager::support::AlgorithmPluginBundle* out_bundle) {
  if (!request || !out_bundle) {
    return false;
  }

  out_bundle->Clear();
  out_bundle->cpu_symbol = true;
<<<<<<< HEAD
  out_bundle->gpu_symbol = true;
  out_bundle->temporary_test_executor = new TemporaryTestLineMotionMainThreadExecutor();
  out_bundle->destroy_temporary_test_executor = &_DestroyTemporaryTestExecutor;
  return true;
}
=======
  out_bundle->gpu_symbol = false;
  out_bundle->reflector = true;
  out_bundle->intervention = true;
  out_bundle->cpu_executor = new LineMotionCpuExecutor();
  out_bundle->destroy_cpu_executor = &DestroyCpuExecutor;
  return true;
}

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
  const algorithmManager::support::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector) {
  if (!request || !out_reflector) {
    return false;
  }

  std::shared_ptr<algorithm::AlgorithmReflector> runtime_reflector{};
  algorithm::AlgorithmPackageLocation package_location{};
  if (!algorithm::TryResolveAlgorithmPackageLocationForPluginCompile(
        request->algorithm_name ? request->algorithm_name : "",
        &package_location,
        nullptr)) {
    return false;
  }
  if (!algorithmManager::support::LoadAlgorithmPackageReflectorFromLocation(
        package_location,
        &runtime_reflector,
        nullptr) || !runtime_reflector) {
    return false;
  }
  *out_reflector = *runtime_reflector;
  return true;
}
>>>>>>> 0e5193b (preciser control of digital)
