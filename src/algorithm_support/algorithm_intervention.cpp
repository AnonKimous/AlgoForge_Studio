#include "algorithm_support/algorithm_intervention.h"
#include "algorithm_support/algorithm_protocol.h"
#include "algorithm_support/algorithm_package_location.h"

#include "algorithm_management/algorithm_abi.h"
#include "algorithm_management/algorithm_manager.h"
#include "cJSON.h"

#include <cstddef>
#include <algorithm>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <sstream>
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

std::string _AlgorithmNameFromLocation(const algorithm::AlgorithmPackageLocation& package_location) {
  if (!package_location.algorithm_name.empty()) {
    return package_location.algorithm_name;
  }
  return package_location.manifest_name;
}

std::filesystem::path _BuildPackageJsonPath(const algorithm::AlgorithmPackageLocation& package_location) {
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

  return package_root / (algorithm_name + "_package.json");
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
  if (kind == "afterTick") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::PostExecution;
    return true;
  }

  *out_stage_kind = agent::AlgorithmInterventionStageKind::Custom;
  return true;
}

_InterventionSchema _LoadInterventionSchema(const algorithm::AlgorithmPackageLocation& package_location) {
  _InterventionSchema schema{};
  const std::filesystem::path path = _BuildPackageJsonPath(package_location);
  if (path.empty()) {
    schema.error_message = "Failed to resolve package JSON file.";
    return schema;
  }

  const std::string json_text = _ReadTextFile(path.string());
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
    schema.error_message = "Package JSON file " + path.string() + " is missing an 'intervention' object.";
    cJSON_Delete(root);
    return schema;
  }

  const cJSON* stages = cJSON_GetObjectItemCaseSensitive(intervention, "stages");
  if (!stages || !cJSON_IsObject(stages)) {
    schema.error_message = "Intervention section in package JSON file " + path.string() + " is missing a 'stages' object.";
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
      stage_spec.shader.vertex_shader_path = _GetStringField(shader, "vertex");
      stage_spec.shader.fragment_shader_path = _GetStringField(shader, "fragment");
      stage_spec.shader.pipeline_kind = _GetStringField(shader, "pipeline");
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
  if (!schema.valid && schema.error_message.empty()) {
    schema.error_message = path.string() + " does not contain any intervention stages.";
  }
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

bool CreateAlgorithmInterventionByName(
  const std::string& algorithm_name,
  std::shared_ptr<agent::IAlgorithmIntervention>* out_intervention,
  std::string* out_error_message) {
  if (!out_intervention) {
    if (out_error_message) {
      *out_error_message = "Intervention output pointer is null.";
    }
    return false;
  }
  algorithm::AlgorithmPackageLocation package_location{};
  std::string location_error_message;
  if (!algorithm_management::TryResolveAlgorithmPackageLocation(
        algorithm_name,
        &package_location,
        &location_error_message)) {
    if (out_error_message) {
      *out_error_message = location_error_message.empty()
        ? ("Failed to resolve algorithm package location for '" + algorithm_name + "'.")
        : std::move(location_error_message);
    }
    return false;
  }

  const _InterventionSchema schema = _LoadInterventionSchema(package_location);
  if (!schema.valid) {
    if (out_error_message) {
      *out_error_message = schema.error_message.empty()
        ? ("Failed to load intervention schema for '" + algorithm_name + "'.")
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
