#include "algomanager/catalog/algorithm_intervention.h"
#include "algomanager/catalog/algorithm_library_paths.h"
#include "algomanager/bridge/algorithm_protocol.h"
#include "algomanager/bridge/algorithm_package_location.h"
#include "algomanager/catalog/algorithm_package_paths.h"
#include "algomanager/catalog/algorithm_json_utils.h"

#include "algomanager/bridge/algorithm_abi.h"
#include "cJSON.h"

#include <cstddef>
#include <algorithm>
#include <filesystem>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace algomanager { namespace algocatalog {

namespace {

struct _PackedAlgorithmInterventionEntry {
  int32_t mode{};
  float radius{};
  float velocity_magnitude{};
  uint32_t velocity_delay_frames{};
  uint32_t velocity_duration_frames{};
  float force_magnitude{};
  uint32_t force_delay_frames{};
  uint32_t force_duration_frames{};
};

struct _InterventionResourceEntry {
  std::string resource_name;
  std::string resource_kind;
  bool required{true};
};

struct _InterventionDescriptorEntry {
  std::string descriptor_name;
  std::string container_name;
  uint32_t array_index{0u};
};

struct _PhaseSchema {
  std::vector<algomanager::algoscheduler::AlgorithmPhaseSpec> phase_specs;
  bool valid{false};
  std::string error_message;
};

template <typename T>
void _AppendPod(std::vector<std::byte>* bytes, const T& value) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (!bytes) return;
  const size_t offset = bytes->size();
  bytes->resize(offset + sizeof(T));
  std::memcpy(bytes->data() + offset, &value, sizeof(T));
}

template <typename T>
bool _ReadPod(const std::vector<std::byte>& bytes, size_t* offset, T* value) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (!offset || !value) return false;
  if (*offset + sizeof(T) > bytes.size()) return false;
  std::memcpy(value, bytes.data() + *offset, sizeof(T));
  *offset += sizeof(T);
  return true;
}

std::string _AlgorithmNameFromLocation(const algorithm::AlgorithmPackageLocation& package_location) {
  if (!package_location.algorithm_name.empty()) {
    return package_location.algorithm_name;
  }
  return package_location.manifest_name;
}

bool _ParsePhaseKind(
  const std::string& phase_name,
  const std::string& phase_kind_text,
  algomanager::algoscheduler::AlgorithmPhaseKind* out_phase_kind) {
  if (!out_phase_kind) {
    return false;
  }

  const std::string kind = !phase_kind_text.empty() ? phase_kind_text : phase_name;
  if (kind == "pretick" || kind == "preTick") {
    *out_phase_kind = algomanager::algoscheduler::AlgorithmPhaseKind::Pretick;
    return true;
  }
  if (kind == "exec") {
    *out_phase_kind = algomanager::algoscheduler::AlgorithmPhaseKind::Exec;
    return true;
  }
  if (kind == "aftertick" || kind == "afterTick" || kind == "postExecution") {
    *out_phase_kind = algomanager::algoscheduler::AlgorithmPhaseKind::AfterTick;
    return true;
  }
  if (kind == "renderresult" || kind == "renderResult" || kind == "resultRender") {
    *out_phase_kind = algomanager::algoscheduler::AlgorithmPhaseKind::RenderResult;
    return true;
  }
  if (kind == "reflect") {
    *out_phase_kind = algomanager::algoscheduler::AlgorithmPhaseKind::Reflect;
    return true;
  }

  *out_phase_kind = algomanager::algoscheduler::AlgorithmPhaseKind::Custom;
  return true;
}

bool _ParseExecutionPreference(
  const std::string& preference_text,
  algomanager::algoscheduler::AlgorithmExecutionPreference* out_preference) {
  if (!out_preference) {
    return false;
  }
  if (preference_text == "jobs" || preference_text == "Jobs" || preference_text == "JOBS") {
    *out_preference = algomanager::algoscheduler::AlgorithmExecutionPreference::Jobs;
    return true;
  }
  if (preference_text == "vk" || preference_text == "Vk" || preference_text == "VK") {
    *out_preference = algomanager::algoscheduler::AlgorithmExecutionPreference::Vk;
    return true;
  }
  if (preference_text == "cuda" || preference_text == "Cuda" || preference_text == "CUDA") {
    *out_preference = algomanager::algoscheduler::AlgorithmExecutionPreference::Cuda;
    return true;
  }
  if (preference_text == "compatibility" || preference_text == "Compatibility" || preference_text == "COMPATIBILITY" ||
      preference_text == "compat") {
    *out_preference = algomanager::algoscheduler::AlgorithmExecutionPreference::Compatibility;
    return true;
  }
  return false;
}

algomanager::algoscheduler::AlgorithmExecutionPreference _DefaultExecutionPreferenceForPhaseKind(
  algomanager::algoscheduler::AlgorithmPhaseKind phase_kind) {
  switch (phase_kind) {
    case algomanager::algoscheduler::AlgorithmPhaseKind::ResultRender:
      return algomanager::algoscheduler::AlgorithmExecutionPreference::Vk;
    case algomanager::algoscheduler::AlgorithmPhaseKind::Reflect:
      return algomanager::algoscheduler::AlgorithmExecutionPreference::Jobs;
    case algomanager::algoscheduler::AlgorithmPhaseKind::Pretick:
    case algomanager::algoscheduler::AlgorithmPhaseKind::Exec:
    case algomanager::algoscheduler::AlgorithmPhaseKind::AfterTick:
    case algomanager::algoscheduler::AlgorithmPhaseKind::Custom:
      return algomanager::algoscheduler::AlgorithmExecutionPreference::Jobs;
  }
  return algomanager::algoscheduler::AlgorithmExecutionPreference::Jobs;
}

_PhaseSchema _LoadPhaseSchema(const algorithm::AlgorithmPackageLocation& package_location) {
  _PhaseSchema schema{};
  const std::string algorithm_name = _AlgorithmNameFromLocation(package_location);
  const std::filesystem::path path = algorithm::package_paths::ResolvePackageJsonPath(
    package_location.package_root,
    package_location.manifest_path,
    algorithm_name);
  if (path.empty()) {
    schema.error_message = "Failed to resolve package JSON file.";
    return schema;
  }

  const std::string json_text = json_utils::ReadTextFile(path);
  if (json_text.empty()) {
    schema.error_message = "Failed to read package JSON file: " + path.string();
    return schema;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    schema.error_message = "Failed to parse package JSON file: " + path.string();
    return schema;
  }

  const cJSON* intervention = cJSON_GetObjectItemCaseSensitive(root, "intervention");
  if (!intervention || !cJSON_IsObject(intervention)) {
    cJSON_Delete(root);
    return schema;
  }

  const cJSON* singular_stage = cJSON_GetObjectItemCaseSensitive(intervention, "stage");
  const cJSON* plural_stages = cJSON_GetObjectItemCaseSensitive(intervention, "stages");
  if (singular_stage && plural_stages) {
    schema.error_message =
      "Intervention section must not declare both 'stage' and 'stages': " + path.string();
    cJSON_Delete(root);
    return schema;
  }

  const cJSON* stages = singular_stage ? singular_stage : plural_stages;
  if (!stages) {
    cJSON_Delete(root);
    return schema;
  }
  if (!cJSON_IsObject(stages)) {
    schema.error_message = "Intervention phase section is invalid: " + path.string();
    cJSON_Delete(root);
    return schema;
  }

  for (const cJSON* stage_item = stages->child; stage_item; stage_item = stage_item->next) {
    if (!stage_item || !cJSON_IsObject(stage_item) || !stage_item->string) {
      continue;
    }

    const std::string stage_key = stage_item->string;
    algomanager::algoscheduler::AlgorithmPhaseSpec phase_spec{};
    phase_spec.stage_name = json_utils::GetStringField(stage_item, "stage_name");
    if (phase_spec.stage_name.empty()) {
      phase_spec.stage_name = stage_key;
    }

    const std::string stage_kind_text = json_utils::GetStringField(stage_item, "stage_kind");
    if (!_ParsePhaseKind(phase_spec.stage_name, stage_kind_text, &phase_spec.stage_kind)) {
      schema.error_message = "Invalid phase kind in package JSON file: " + path.string();
      cJSON_Delete(root);
      return schema;
    }

    phase_spec.execution_preference = _DefaultExecutionPreferenceForPhaseKind(phase_spec.stage_kind);
    const std::string execution_preference_text = json_utils::GetStringField(stage_item, "execution_preference");
    const std::string execution_preference_alias = json_utils::GetStringField(stage_item, "executionPreference");
    const std::string stage_execution_preference_text =
      !execution_preference_text.empty() ? execution_preference_text : execution_preference_alias;
    if (!stage_execution_preference_text.empty()) {
      if (!_ParseExecutionPreference(stage_execution_preference_text, &phase_spec.execution_preference)) {
        schema.error_message = "Invalid phase execution preference in package JSON file: " + path.string();
        cJSON_Delete(root);
        return schema;
      }
    }
    if (phase_spec.stage_kind == algomanager::algoscheduler::AlgorithmPhaseKind::ResultRender &&
        phase_spec.execution_preference != algomanager::algoscheduler::AlgorithmExecutionPreference::Vk) {
      schema.error_message = "Result-render phase must use VK execution preference: " + path.string();
      cJSON_Delete(root);
      return schema;
    }
    if (phase_spec.stage_kind == algomanager::algoscheduler::AlgorithmPhaseKind::Reflect &&
        phase_spec.execution_preference != algomanager::algoscheduler::AlgorithmExecutionPreference::Jobs) {
      schema.error_message = "Reflect phase must use Jobs execution preference: " + path.string();
      cJSON_Delete(root);
      return schema;
    }

    const cJSON* used_containers = cJSON_GetObjectItemCaseSensitive(stage_item, "used_algorithm_containers");
    if (used_containers && cJSON_IsObject(used_containers)) {
      const cJSON* arrays = cJSON_GetObjectItemCaseSensitive(used_containers, "arrays");
      if (arrays && cJSON_IsArray(arrays)) {
        const int container_count = cJSON_GetArraySize(arrays);
        phase_spec.used_algorithm_containers.reserve(phase_spec.used_algorithm_containers.size() + (container_count > 0 ? static_cast<size_t>(container_count) : 0u));
        for (int i = 0; i < container_count; ++i) {
          const cJSON* item = cJSON_GetArrayItem(arrays, i);
          if (!item) {
            continue;
          }

          algomanager::algoscheduler::AlgorithmPhaseContainerBinding binding{};
          if (cJSON_IsString(item) && item->valuestring) {
            binding.container_name = item->valuestring;
            binding.container_kind = "array";
            binding.tuple_width = 3u;
            binding.required = true;
          } else if (cJSON_IsObject(item)) {
            binding.container_name = json_utils::GetStringField(item, "name");
            if (binding.container_name.empty()) {
              binding.container_name = json_utils::GetStringField(item, "container");
            }
            binding.container_kind = json_utils::GetStringField(item, "kind");
            if (binding.container_kind.empty()) {
              binding.container_kind = "array";
            }
            binding.tuple_width = json_utils::GetUintField(item, "tuple_width", 3u);
            if (binding.tuple_width == 0u) {
              binding.tuple_width = 3u;
            }
            binding.required = json_utils::GetBoolField(item, "required", true);
          }

          if (binding.container_name.empty()) {
            schema.error_message = "Invalid array container binding in package JSON file: " + path.string();
            cJSON_Delete(root);
            return schema;
          }
          phase_spec.used_algorithm_containers.push_back(std::move(binding));
        }
      }

      const cJSON* variables = cJSON_GetObjectItemCaseSensitive(used_containers, "variables");
      if (variables && cJSON_IsArray(variables)) {
        const int container_count = cJSON_GetArraySize(variables);
        phase_spec.used_algorithm_containers.reserve(
          phase_spec.used_algorithm_containers.size() +
          (container_count > 0 ? static_cast<size_t>(container_count) : 0u));
        for (int i = 0; i < container_count; ++i) {
          const cJSON* item = cJSON_GetArrayItem(variables, i);
          if (!item) {
            continue;
          }

          algomanager::algoscheduler::AlgorithmPhaseContainerBinding binding{};
          if (cJSON_IsString(item) && item->valuestring) {
            binding.container_name = item->valuestring;
            binding.container_kind = "variable";
            binding.tuple_width = 1u;
            binding.required = true;
          } else if (cJSON_IsObject(item)) {
            binding.container_name = json_utils::GetStringField(item, "name");
            if (binding.container_name.empty()) {
              binding.container_name = json_utils::GetStringField(item, "container");
            }
            binding.container_kind = json_utils::GetStringField(item, "kind");
            if (binding.container_kind.empty()) {
              binding.container_kind = "variable";
            }
            binding.tuple_width = json_utils::GetUintField(item, "tuple_width", 1u);
            if (binding.tuple_width == 0u) {
              binding.tuple_width = 1u;
            }
            binding.required = json_utils::GetBoolField(item, "required", true);
          }

          if (binding.container_name.empty()) {
            schema.error_message = "Invalid variable container binding in package JSON file: " + path.string();
            cJSON_Delete(root);
            return schema;
          }
          phase_spec.used_algorithm_containers.push_back(std::move(binding));
        }
      }
    }

    const cJSON* functions = cJSON_GetObjectItemCaseSensitive(stage_item, "functions");
    if (functions && cJSON_IsArray(functions)) {
      const int function_count = cJSON_GetArraySize(functions);
      phase_spec.functions.reserve(function_count > 0 ? static_cast<size_t>(function_count) : 0u);
      for (int i = 0; i < function_count; ++i) {
        const cJSON* function_item = cJSON_GetArrayItem(functions, i);
        if (function_item && cJSON_IsString(function_item) && function_item->valuestring) {
          phase_spec.functions.emplace_back(function_item->valuestring);
        }
      }
    }

    const cJSON* shader = cJSON_GetObjectItemCaseSensitive(stage_item, "shader");
    if (shader && cJSON_IsObject(shader)) {
      phase_spec.shader.vertex_shader_path = json_utils::GetStringField(shader, "vertex");
      phase_spec.shader.fragment_shader_path = json_utils::GetStringField(shader, "fragment");
      phase_spec.shader.pipeline_kind = json_utils::GetStringField(shader, "pipeline");
    }

    if (phase_spec.stage_kind == algomanager::algoscheduler::AlgorithmPhaseKind::ResultRender) {
      if (phase_spec.used_algorithm_containers.empty()) {
        schema.error_message =
          "Result-render phase in package JSON file must bind at least one array container: " + path.string();
        cJSON_Delete(root);
        return schema;
      }
      if (phase_spec.shader.vertex_shader_path.empty() || phase_spec.shader.fragment_shader_path.empty()) {
        schema.error_message =
          "Result-render phase in package JSON file is missing shader paths: " + path.string();
        cJSON_Delete(root);
        return schema;
      }
    }

    schema.phase_specs.push_back(std::move(phase_spec));
  }

  cJSON_Delete(root);
  schema.valid = !schema.phase_specs.empty();
  return schema;
}

class JsonAlgorithmIntervention final : public algomanager::algoscheduler::IAlgorithmIntervention {
 public:
  explicit JsonAlgorithmIntervention(_PhaseSchema schema)
    : schema_(std::move(schema)) {}

  bool SupportsIntervention() const override {
    return schema_.valid && !schema_.phase_specs.empty();
  }

  void FillAgentToAlgorithmSignal(
    const algomanager::algoscheduler::AgentTickContext& context,
    AgentToAlgorithmSignal* out_signal) const override {
    if (!out_signal) {
      return;
    }

    *out_signal = {};
    out_signal->needs_intervention = context.intervention_request && context.intervention_request->enabled;
    out_signal->control_bits = context.intervention_request ? context.intervention_request->control_bits : 0u;
  }

  bool GetInterventionPhaseSpecs(
    std::vector<algomanager::algoscheduler::AlgorithmPhaseSpec>* out_phase_specs) const override {
    if (!out_phase_specs) {
      return false;
    }
    *out_phase_specs = schema_.phase_specs;
    return schema_.valid && !schema_.phase_specs.empty();
  }

 private:
  _PhaseSchema schema_{};
};

IoSignalBufferEntry _BuildSignalEntry(
  const char* name,
  uint32_t data_count,
  const std::string& source_module_name,
  uint32_t source_buffer_id,
  const std::string& target_module_name,
  uint32_t target_buffer_id,
  bool lock_required) {
  IoSignalBufferEntry entry{};
  entry.name = name;
  entry.data_offset = 0u;
  entry.data_length = data_count;
  entry.source_module_name = source_module_name;
  entry.source_buffer_id = source_buffer_id;
  entry.target_module_name = target_module_name;
  entry.target_buffer_id = target_buffer_id;
  entry.lock_required = lock_required;
  return entry;
}

bool _DecodeInterventionMode(int32_t raw_mode, AlgorithmInterventionMode* mode) {
  if (!mode) return false;
  switch (raw_mode) {
    case static_cast<int32_t>(AlgorithmInterventionMode::Displacement):
      *mode = AlgorithmInterventionMode::Displacement;
      return true;
    case static_cast<int32_t>(AlgorithmInterventionMode::Velocity):
      *mode = AlgorithmInterventionMode::Velocity;
      return true;
    case static_cast<int32_t>(AlgorithmInterventionMode::Force):
      *mode = AlgorithmInterventionMode::Force;
      return true;
    default:
      return false;
  }
}

AlgorithmInterventionDescriptor _ToAlgorithmInterventionDescriptor(const InteractionInterventionRequest& request) {
  AlgorithmInterventionDescriptor descriptor{};
  descriptor.mode = static_cast<AlgorithmInterventionMode>(request.mode);
  descriptor.radius = request.radius;
  descriptor.velocity_magnitude = request.velocity_magnitude;
  descriptor.velocity_delay_frames = request.velocity_delay_frames;
  descriptor.velocity_duration_frames = request.velocity_duration_frames;
  descriptor.force_magnitude = request.force_magnitude;
  descriptor.force_delay_frames = request.force_delay_frames;
  descriptor.force_duration_frames = request.force_duration_frames;
  descriptor.source_module_name = request.source_module_name;
  descriptor.source_buffer_id = request.source_buffer_id;
  descriptor.target_module_name = request.target_module_name;
  descriptor.target_buffer_id = request.target_buffer_id;
  descriptor.lock_required = request.lock_required;
  return descriptor;
}

InteractionInterventionRequest _ToInteractionInterventionRequest(const AlgorithmInterventionDescriptor& descriptor) {
  InteractionInterventionRequest request{};
  request.enabled = true;
  request.mode = static_cast<InteractionInterventionMode>(descriptor.mode);
  request.radius = descriptor.radius;
  request.velocity_magnitude = descriptor.velocity_magnitude;
  request.velocity_delay_frames = descriptor.velocity_delay_frames;
  request.velocity_duration_frames = descriptor.velocity_duration_frames;
  request.force_magnitude = descriptor.force_magnitude;
  request.force_delay_frames = descriptor.force_delay_frames;
  request.force_duration_frames = descriptor.force_duration_frames;
  request.source_module_name = descriptor.source_module_name;
  request.source_buffer_id = descriptor.source_buffer_id;
  request.target_module_name = descriptor.target_module_name;
  request.target_buffer_id = descriptor.target_buffer_id;
  request.lock_required = descriptor.lock_required;
  return request;
}

IoDataBufferEntry _CreateAlgorithmInterventionDataBufferEntry(const AlgorithmInterventionDescriptor& descriptor) {
  IoDataBufferEntry entry{};
  entry.name = "algorithm_intervention_data";
  _AppendPod(&entry.bytes, _PackedAlgorithmInterventionEntry{
    static_cast<int32_t>(descriptor.mode),
    descriptor.radius,
    std::max(0.0f, descriptor.velocity_magnitude),
    descriptor.velocity_delay_frames,
    std::max(1u, descriptor.velocity_duration_frames),
    std::max(0.0f, descriptor.force_magnitude),
    descriptor.force_delay_frames,
    std::max(1u, descriptor.force_duration_frames),
  });
  return entry;
}

bool _DecodeAlgorithmInterventionData(
  const IoDataBufferEntry& entry,
  DecodedAlgorithmIntervention* decoded) {
  if (!decoded) return false;

  size_t offset = 0;
  _PackedAlgorithmInterventionEntry packed{};
  if (!_ReadPod(entry.bytes, &offset, &packed)) return false;
  if (offset != entry.bytes.size()) return false;

  AlgorithmInterventionMode mode = AlgorithmInterventionMode::Displacement;
  if (!_DecodeInterventionMode(packed.mode, &mode)) return false;
  decoded->mode = mode;
  decoded->radius = std::max(0.0f, packed.radius);
  decoded->velocity_magnitude = std::max(0.0f, packed.velocity_magnitude);
  decoded->velocity_delay_frames = packed.velocity_delay_frames;
  decoded->velocity_duration_frames = std::max(1u, packed.velocity_duration_frames);
  decoded->force_magnitude = std::max(0.0f, packed.force_magnitude);
  decoded->force_delay_frames = packed.force_delay_frames;
  decoded->force_duration_frames = std::max(1u, packed.force_duration_frames);
  return true;
}

}  // namespace

bool LoadAlgorithmInterventionFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algomanager::algoscheduler::IAlgorithmIntervention>* out_intervention,
  std::string* out_error_message) {
  if (!out_intervention) {
    if (out_error_message) {
      *out_error_message = "Intervention output pointer is null.";
    }
    return false;
  }

  const _PhaseSchema schema = _LoadPhaseSchema(package_location);
  if (!schema.valid) {
    if (schema.error_message.empty()) {
      *out_intervention = nullptr;
      if (out_error_message) {
        out_error_message->clear();
      }
      return true;
    }
    if (out_error_message) {
      *out_error_message = schema.error_message.empty()
        ? "Failed to load intervention schema from package location."
        : std::move(schema.error_message);
    }
    return false;
  }

  *out_intervention = std::make_shared<JsonAlgorithmIntervention>(schema);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

IoBufferPacket BuildAlgorithmInterventionPacket(const AlgorithmInterventionDescriptor& descriptor) {
  IoBufferPacket packet{};
  packet.protocol.name = kAlgorithmInterventionIoProtocolName;

  IoDataBufferEntry entry = _CreateAlgorithmInterventionDataBufferEntry(descriptor);
  entry.buffer_id = descriptor.target_buffer_id;
  entry.source_buffer_id = descriptor.source_buffer_id;
  packet.data_buffer.push_back(std::move(entry));
  packet.signal_buffer.push_back(_BuildSignalEntry(
    "algorithm_intervention",
    1u,
    descriptor.source_module_name,
    descriptor.source_buffer_id,
    descriptor.target_module_name,
    descriptor.target_buffer_id,
    descriptor.lock_required));

  return packet;
}

bool DecodeAlgorithmInterventionPacket(const IoBufferPacket& packet, DecodedAlgorithmIntervention* decoded) {
  if (!decoded) return false;
  if (packet.protocol.name != kAlgorithmInterventionIoProtocolName) return false;
  if (packet.data_buffer.empty()) return false;

  for (const IoDataBufferEntry& entry : packet.data_buffer) {
    if (entry.name == "algorithm_intervention_data") {
      return _DecodeAlgorithmInterventionData(entry, decoded);
    }
  }
  return false;
}

IoBufferPacket BuildAlgorithmInterventionPacket(const InteractionInterventionRequest& request) {
  return BuildAlgorithmInterventionPacket(_ToAlgorithmInterventionDescriptor(request));
}

bool DecodeAlgorithmInterventionPacket(const IoBufferPacket& packet, InteractionInterventionRequest* request) {
  if (!request) return false;
  DecodedAlgorithmIntervention decoded{};
  if (!DecodeAlgorithmInterventionPacket(packet, &decoded)) {
    return false;
  }
  *request = _ToInteractionInterventionRequest(decoded);
  return true;
}

}  // namespace catalog
}  // namespace algomanager

