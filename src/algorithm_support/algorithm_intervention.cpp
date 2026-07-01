#include "algorithm_support/algorithm_intervention.h"
#include "algorithm_support/algorithm_library_paths.h"
#include "algorithm_support/algorithm_protocol.h"
#include "algorithm_support/algorithm_package_location.h"
#include "algorithm_support/algorithm_package_paths.h"
#include "algorithm_support/algorithm_json_utils.h"

#include "algorithm_support/algorithm_abi.h"
#include "algorithm_management/algorithm_manager.h"
#define RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "runtime_systems/runtime_systems.h"
#undef RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE
#include "cJSON.h"

#include <cstddef>
#include <algorithm>
#include <filesystem>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace algorithm_support {

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

struct _InterventionSchema {
  std::vector<agent::AlgorithmInterventionStageSpec> stage_specs;
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

bool _ParseInterventionStageKind(
  const std::string& stage_name,
  const std::string& stage_kind_text,
  agent::AlgorithmInterventionStageKind* out_stage_kind) {
  if (!out_stage_kind) {
    return false;
  }

  const std::string kind = !stage_kind_text.empty() ? stage_kind_text : stage_name;
  if (kind == "pretick" || kind == "preTick") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::Pretick;
    return true;
  }
  if (kind == "exec") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::Exec;
    return true;
  }
  if (kind == "aftertick" || kind == "afterTick" || kind == "postExecution") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::AfterTick;
    return true;
  }
  if (kind == "renderresult" || kind == "renderResult" || kind == "resultRender") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::RenderResult;
    return true;
  }
  if (kind == "reflect") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::Reflect;
    return true;
  }

  *out_stage_kind = agent::AlgorithmInterventionStageKind::Custom;
  return true;
}

_InterventionSchema _LoadInterventionSchema(const algorithm::AlgorithmPackageLocation& package_location) {
  _InterventionSchema schema{};
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
    schema.error_message = "Intervention stage section is invalid: " + path.string();
    cJSON_Delete(root);
    return schema;
  }

  for (const cJSON* stage_item = stages->child; stage_item; stage_item = stage_item->next) {
    if (!stage_item || !cJSON_IsObject(stage_item) || !stage_item->string) {
      continue;
    }

    const std::string stage_key = stage_item->string;
    agent::AlgorithmInterventionStageSpec stage_spec{};
    stage_spec.stage_name = json_utils::GetStringField(stage_item, "stage_name");
    if (stage_spec.stage_name.empty()) {
      stage_spec.stage_name = stage_key;
    }

    const std::string stage_kind_text = json_utils::GetStringField(stage_item, "stage_kind");
    if (!_ParseInterventionStageKind(stage_spec.stage_name, stage_kind_text, &stage_spec.stage_kind)) {
      schema.error_message = "Invalid stage kind in package JSON file: " + path.string();
      cJSON_Delete(root);
      return schema;
    }

    const cJSON* used_containers = cJSON_GetObjectItemCaseSensitive(stage_item, "used_algorithm_containers");
    if (used_containers && cJSON_IsObject(used_containers)) {
      const cJSON* arrays = cJSON_GetObjectItemCaseSensitive(used_containers, "arrays");
      if (arrays && cJSON_IsArray(arrays)) {
        const int container_count = cJSON_GetArraySize(arrays);
        stage_spec.used_algorithm_containers.reserve(stage_spec.used_algorithm_containers.size() + (container_count > 0 ? static_cast<size_t>(container_count) : 0u));
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
          stage_spec.used_algorithm_containers.push_back(std::move(binding));
        }
      }
    }

    const cJSON* functions = cJSON_GetObjectItemCaseSensitive(stage_item, "functions");
    if (functions && cJSON_IsArray(functions)) {
      const int function_count = cJSON_GetArraySize(functions);
      stage_spec.functions.reserve(function_count > 0 ? static_cast<size_t>(function_count) : 0u);
      for (int i = 0; i < function_count; ++i) {
        const cJSON* function_item = cJSON_GetArrayItem(functions, i);
        if (function_item && cJSON_IsString(function_item) && function_item->valuestring) {
          stage_spec.functions.emplace_back(function_item->valuestring);
        }
      }
    }

    const cJSON* shader = cJSON_GetObjectItemCaseSensitive(stage_item, "shader");
    if (shader && cJSON_IsObject(shader)) {
      stage_spec.shader.vertex_shader_path = json_utils::GetStringField(shader, "vertex");
      stage_spec.shader.fragment_shader_path = json_utils::GetStringField(shader, "fragment");
      stage_spec.shader.pipeline_kind = json_utils::GetStringField(shader, "pipeline");
    }

    if (stage_spec.stage_kind == agent::AlgorithmInterventionStageKind::ResultRender) {
      if (stage_spec.used_algorithm_containers.empty()) {
        schema.error_message =
          "Result-render stage in package JSON file must bind at least one array container: " + path.string();
        cJSON_Delete(root);
        return schema;
      }
      if (stage_spec.shader.vertex_shader_path.empty() || stage_spec.shader.fragment_shader_path.empty()) {
        schema.error_message =
          "Result-render stage in package JSON file is missing shader paths: " + path.string();
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

class JsonAlgorithmIntervention final : public agent::IAlgorithmIntervention {
 public:
  explicit JsonAlgorithmIntervention(_InterventionSchema schema)
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
    out_signal->control_bits = context.intervention_request ? context.intervention_request->control_bits : 0u;
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
  _InterventionSchema schema_{};
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
  std::shared_ptr<agent::IAlgorithmIntervention>* out_intervention,
  std::string* out_error_message) {
  if (!out_intervention) {
    if (out_error_message) {
      *out_error_message = "Intervention output pointer is null.";
    }
    return false;
  }

  const _InterventionSchema schema = _LoadInterventionSchema(package_location);
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

}  // namespace algorithm_support

namespace algorithm_management {

namespace {

runtime_systems::RuntimeJobPriority _ToRuntimeJobPriority(AlgorithmJobPriority priority) {
  switch (priority) {
    case AlgorithmJobPriority::High:
      return runtime_systems::RuntimeJobPriority::High;
    case AlgorithmJobPriority::Normal:
      return runtime_systems::RuntimeJobPriority::Normal;
    case AlgorithmJobPriority::Low:
      return runtime_systems::RuntimeJobPriority::Low;
  }
  return runtime_systems::RuntimeJobPriority::High;
}

void _ClearAlgorithmSchedulerForRuntimeShutdown() {
  ClearAlgorithmScheduler();
}

bool _IsGpuExecutionStage(const agent::AlgorithmInterventionStageSpec& stage) {
  return stage.stage_kind == agent::AlgorithmInterventionStageKind::ResultRender;
}

bool _ResolveRuntimeGpuShaderPath(
  const std::string& algorithm_name,
  const std::string& shader_path,
  std::string* out_resolved_path,
  std::string* out_error_message) {
  if (!out_resolved_path) {
    if (out_error_message) {
      *out_error_message = "Resolved GPU shader path output pointer is null.";
    }
    return false;
  }
  if (shader_path.empty()) {
    if (out_error_message) {
      *out_error_message = "GPU shader path must not be empty.";
    }
    return false;
  }

  const std::filesystem::path path(shader_path);
  if (path.is_absolute()) {
    *out_resolved_path = path.string();
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  ::algorithm::AlgorithmPackageLocation package_location{};
  std::string resolve_error_message;
  if (!TryResolveAlgorithmPackageLocation(
        algorithm_name,
        &package_location,
        &resolve_error_message)) {
    if (out_error_message) {
      *out_error_message = resolve_error_message.empty()
        ? ("Failed to resolve algorithm package location for '" + algorithm_name + "'.")
        : std::move(resolve_error_message);
    }
    return false;
  }
  if (package_location.runtime_package_root.empty()) {
    if (out_error_message) {
      *out_error_message = "Algorithm runtime package root is empty for '" + algorithm_name + "'.";
    }
    return false;
  }

  *out_resolved_path = algorithm::library_paths::ResolveAlgorithmRelativePath(
    algorithm::library_paths::ResolveAlgorithmLibraryRuntimeRoot(),
    algorithm::library_paths::ResolvePackageRelativePath(
      package_location.package_root,
      algorithm::library_paths::ResolveAlgorithmLibrarySourceRoot()),
    shader_path).string();
  if (out_resolved_path->empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to resolve GPU shader path for '" + algorithm_name + "'.";
    }
    return false;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _TryBuildRuntimeGpuStageJob(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  runtime_systems::RuntimeGpuStageJob* out_job,
  bool* out_has_gpu_stage,
  std::string* out_error_message) {
  if (!container_set || !out_job || !out_has_gpu_stage) {
    if (out_error_message) {
      *out_error_message = "Runtime GPU stage job output pointer is null.";
    }
    return false;
  }

  *out_job = {};
  *out_has_gpu_stage = false;
  if (!object.intervention) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  std::vector<agent::AlgorithmInterventionStageSpec> stage_specs;
  if (!object.intervention->GetInterventionStageSpecs(&stage_specs) || stage_specs.empty()) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  const agent::AlgorithmInterventionStageSpec* gpu_stage = nullptr;
  for (const agent::AlgorithmInterventionStageSpec& stage_spec : stage_specs) {
    if (_IsGpuExecutionStage(stage_spec)) {
      gpu_stage = &stage_spec;
      break;
    }
  }
  if (!gpu_stage) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  *out_has_gpu_stage = true;
  if (gpu_stage->shader.vertex_shader_path.empty() || gpu_stage->shader.fragment_shader_path.empty()) {
    if (out_error_message) {
      *out_error_message = "GPU tick stage is missing shader paths.";
    }
    return false;
  }
  if (gpu_stage->used_algorithm_containers.empty()) {
    if (out_error_message) {
      *out_error_message = "GPU tick stage does not bind any containers.";
    }
    return false;
  }

  out_job->debug_name = object.algorithm_profile.algorithm_name + "::" + gpu_stage->stage_name;
  out_job->shader_namespace = object.algorithm_profile.algorithm_name;
  out_job->stage_name = gpu_stage->stage_name;
  out_job->execution_key = container_set;
  if (!_ResolveRuntimeGpuShaderPath(
        object.algorithm_profile.algorithm_name,
        gpu_stage->shader.vertex_shader_path,
        &out_job->vertex_shader_path,
        out_error_message)) {
    return false;
  }
  if (!_ResolveRuntimeGpuShaderPath(
        object.algorithm_profile.algorithm_name,
        gpu_stage->shader.fragment_shader_path,
        &out_job->fragment_shader_path,
        out_error_message)) {
    return false;
  }

  out_job->buffer_bindings.reserve(gpu_stage->used_algorithm_containers.size());
  for (const agent::AlgorithmInterventionContainerBinding& binding : gpu_stage->used_algorithm_containers) {
    algorithm::AlgorithmContainer* container = FindAlgorithmContainer(container_set, binding.container_name);
    if (!container) {
      if (out_error_message) {
        *out_error_message = binding.required
          ? ("GPU tick stage is missing container '" + binding.container_name + "'.")
          : ("GPU tick optional container '" + binding.container_name +
              "' is not supported because runtime GPU bindings must stay positional.");
      }
      return false;
    }
    if (container->element_stride == 0u || container->bytes.empty()) {
      if (out_error_message) {
        *out_error_message = binding.required
          ? ("GPU tick stage container '" + binding.container_name + "' has no data.")
          : ("GPU tick optional container '" + binding.container_name +
              "' has no data, and sparse GPU bindings are not supported.");
      }
      return false;
    }

    runtime_systems::RuntimeGpuBufferBindingView binding_view{};
    binding_view.binding_name = binding.container_name;
    binding_view.bytes = container->bytes.data();
    binding_view.size_bytes = container->bytes.size();
    binding_view.element_stride = container->element_stride;
    binding_view.required = binding.required;
    out_job->buffer_bindings.push_back(std::move(binding_view));
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace

void SetAlgorithmRuntimeShutdownHook() {
  runtime_systems::SetRuntimeShutdownCallback(&_ClearAlgorithmSchedulerForRuntimeShutdown);
  runtime_systems::SetRuntimeGpuCacheClearCallback(&ClearAlgorithmExecutionCaches);
}

void ClearAlgorithmExecutionCaches() {
  runtime_systems::ClearRuntimeGpuJobCaches();
}

bool ExecuteCpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  const ::agent::AgentTickContext& context,
  const common_data::AgentToAlgorithmSignal& agent_to_algorithm_signal,
  ::algorithm::AlgorithmContainerSet* container_set,
  common_data::AlgorithmToAgentSignal* out_algorithm_to_agent_signal,
  ::agent::AlgorithmPackageDebugState* out_debug_state,
  std::string* out_error_message) {
  if (!container_set || !out_algorithm_to_agent_signal || !out_debug_state) {
    if (out_error_message) {
      *out_error_message = "CPU algorithm execution output pointer is null.";
    }
    return false;
  }
  if (!object.temporaryTest_main_thread_executor) {
    if (out_error_message) {
      *out_error_message = "CPU algorithm executor is unavailable.";
    }
    return false;
  }

  return runtime_systems::SubmitBlockingJob(
    _ToRuntimeJobPriority(context.job_priority),
    [
      &object,
      &context,
      &agent_to_algorithm_signal,
      container_set,
      out_algorithm_to_agent_signal,
      out_debug_state](std::string* out_job_error_message) {
      const bool ok = object.temporaryTest_main_thread_executor->temporaryTestExecuteOnMainThread(
        context,
        object.algorithm_profile,
        agent_to_algorithm_signal,
        container_set,
        out_algorithm_to_agent_signal,
        out_debug_state);
      if (!ok && out_job_error_message) {
        *out_job_error_message = "CPU algorithm execution failed.";
      }
    },
    out_error_message);
}

bool ExecuteGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::agent::AgentTickContext& context,
  std::string* out_error_message) {
  return runtime_systems::ExecuteRuntimeGpuAlgorithmObject(
    object,
    container_set,
    context,
    out_error_message);
}

bool SynchronizeGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message) {
  return runtime_systems::SynchronizeRuntimeGpuAlgorithmObject(
    object,
    container_set,
    out_error_message);
}

}  // namespace algorithm_management
