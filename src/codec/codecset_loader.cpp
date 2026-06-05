#include "codec_manager.h"

#include "capabilities/agent/agent.h"
#include "cJSON.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string_view>
#include <utility>

namespace codec {

namespace {

constexpr char ktemporaryTestLineMotionAlgorithmName[] = "temporary_test_line_motion";
constexpr char ktemporaryTestLineMotionContainerName[] = "pos";
constexpr float ktemporaryTestMinX = -100.0f;
constexpr float ktemporaryTestMaxX = 100.0f;
constexpr float ktemporaryTestVelocityX = 120.0f;

#ifndef ALGORITHM_LIBRARY_RESOURCE_ROOT
#define ALGORITHM_LIBRARY_RESOURCE_ROOT "src/capabilities/algorithm_library"
#endif

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

bool _IstemporaryTestLineMotion(std::string_view algorithm_name) {
  return algorithm_name == ktemporaryTestLineMotionAlgorithmName;
}

void _SetErrorMessage(std::string* out_error_message, std::string message) {
  if (out_error_message) {
    *out_error_message = std::move(message);
  }
}

bool _RejectUnsupportedAlgorithm(
  const std::string& algorithm_name,
  std::string* out_error_message) {
  _SetErrorMessage(
    out_error_message,
    "Codec layer does not have a creator registered for algorithm '" + algorithm_name + "'.");
  return false;
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
  if (cJSON_IsBool(item)) {
    return cJSON_IsTrue(item);
  }
  return fallback;
}

temporaryTestDecomposerSchema _LoadtemporaryTestDecomposerSchema() {
  temporaryTestDecomposerSchema schema{};
  const std::string path =
    std::string(ALGORITHM_LIBRARY_RESOURCE_ROOT) + "/temporary_test_line_motion_decomposer.json";
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
        schema.error_message = "Invalid required_resources entry in temporary_test_line_motion_decomposer.json.";
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
        schema.error_message = "Invalid descriptor_to_container entry in temporary_test_line_motion_decomposer.json.";
        cJSON_Delete(root);
        return schema;
      }
      schema.descriptor_entries.push_back(std::move(entry));
    }
  }

  cJSON_Delete(root);
  schema.valid = !schema.descriptor_entries.empty();
  if (!schema.valid && schema.error_message.empty()) {
    schema.error_message =
      "temporary_test_line_motion_decomposer.json does not contain descriptor_to_container entries.";
  }
  return schema;
}

const temporaryTestDecomposerSchema& _GettemporaryTestDecomposerSchema() {
  static const temporaryTestDecomposerSchema schema = _LoadtemporaryTestDecomposerSchema();
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

bool _ReadtemporaryTestPosition(
  const AlgorithmContainerSet& container_set,
  std::array<float, 3>* out_position) {
  if (!out_position) {
    return false;
  }

  const AlgorithmContainer* container =
    FindAlgorithmContainer(container_set, ktemporaryTestLineMotionContainerName);
  if (!container) {
    return false;
  }
  if (container->storage_kind != AlgorithmContainerStorageKind::Array) {
    return false;
  }
  if (container->element_count < 1u) {
    return false;
  }
  if (container->element_stride != sizeof(float) * 3u) {
    return false;
  }
  if (container->bytes.size() < sizeof(float) * 3u) {
    return false;
  }

  std::memcpy(out_position->data(), container->bytes.data(), sizeof(float) * 3u);
  return true;
}

bool _WritetemporaryTestPosition(
  AlgorithmContainerSet* container_set,
  const std::array<float, 3>& position) {
  if (!container_set) {
    return false;
  }

  AlgorithmContainer* container =
    FindAlgorithmContainer(container_set, ktemporaryTestLineMotionContainerName);
  if (!container) {
    return false;
  }
  if (container->storage_kind != AlgorithmContainerStorageKind::Array) {
    return false;
  }
  if (container->element_count < 1u) {
    return false;
  }
  if (container->element_stride != sizeof(float) * 3u) {
    return false;
  }
  if (container->bytes.size() < sizeof(float) * 3u) {
    return false;
  }

  std::memcpy(container->bytes.data(), position.data(), sizeof(float) * 3u);
  return true;
}

void _SynctemporaryTestMeshPosition(Mesh* mesh, const std::array<float, 3>& position) {
  if (!mesh) {
    return;
  }

  const Vec3 point{
    position[0],
    position[1],
    position[2],
  };
  if (mesh->positions.empty()) {
    mesh->positions.push_back(point);
  } else {
    mesh->positions[0] = point;
    mesh->positions.resize(1u);
  }
  mesh->normals.assign(1u, Vec3{0.0f, 0.0f, 1.0f});
  mesh->triangles.clear();
  mesh->edges.clear();
  mesh->triangle_material_gpa.clear();
}

class temporaryTestLineMotionDecomposer final
  : public agent::IAlgorithmPackageDecomposer {
 public:
  bool ReflectRequiredResources(
    const AlgorithmProfile& algorithm_profile,
    agent::AlgorithmResourceReflection* out_reflection) const override {
    if (!out_reflection) {
      return false;
    }

    *out_reflection = {};
    out_reflection->algorithm_name = algorithm_profile.algorithm_name;
    const temporaryTestDecomposerSchema& schema = _GettemporaryTestDecomposerSchema();
    if (!schema.valid) {
      return false;
    }

    out_reflection->required_resources.reserve(schema.required_resources.size());
    for (const temporaryTestDecomposerResourceEntry& entry : schema.required_resources) {
      out_reflection->required_resources.push_back(agent::AlgorithmResourceReflection::RequiredResource{
        .resource_name = entry.resource_name,
        .resource_kind = entry.resource_kind,
        .required = entry.required,
      });
    }
    out_reflection->valid = true;
    return true;
  }

  bool ReflectDescriptorBindings(
    const AlgorithmProfile& algorithm_profile,
    agent::AlgorithmDescriptorReflection* out_reflection) const override {
    if (!out_reflection) {
      return false;
    }

    *out_reflection = {};
    out_reflection->algorithm_name = algorithm_profile.algorithm_name;
    const temporaryTestDecomposerSchema& schema = _GettemporaryTestDecomposerSchema();
    if (!schema.valid) {
      return false;
    }

    out_reflection->descriptor_slots.reserve(schema.descriptor_entries.size());
    for (const temporaryTestDecomposerDescriptorEntry& entry : schema.descriptor_entries) {
      out_reflection->descriptor_slots.push_back(agent::AlgorithmDescriptorReflection::DescriptorSlot{
        .descriptor_name = entry.descriptor_name,
        .container_name = entry.container_name,
        .array_index = entry.array_index,
      });
    }
    out_reflection->valid = true;
    return true;
  }

  bool Decompose(
    const AlgorithmProfile& algorithm_profile,
    const std::vector<agent::AlgorithmResourceBinding>& resource_bindings,
    const std::vector<agent::AlgorithmDescriptorValue>& descriptor_values,
    AlgorithmContainerSet* container_set,
    std::string* out_error_message) const override {
    if (!container_set) {
      _SetErrorMessage(out_error_message, "AlgorithmContainerSet output pointer is null.");
      return false;
    }

    const temporaryTestDecomposerSchema& schema = _GettemporaryTestDecomposerSchema();
    if (!schema.valid) {
      _SetErrorMessage(out_error_message, schema.error_message);
      return false;
    }

    for (const temporaryTestDecomposerResourceEntry& entry : schema.required_resources) {
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
      if (entry.resource_kind == "mesh" && !binding->has_mesh) {
        _SetErrorMessage(
          out_error_message,
          "Required mesh resource '" + entry.resource_name + "' was not loaded for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }
    }

    std::array<float, 3> position{
      ktemporaryTestMinX,
      0.0f,
      0.0f,
    };
    for (const temporaryTestDecomposerDescriptorEntry& entry : schema.descriptor_entries) {
      const agent::AlgorithmDescriptorValue* value =
        _FindtemporaryTestDescriptorValue(descriptor_values, entry.descriptor_name);
      if (!value) {
        _SetErrorMessage(
          out_error_message,
          "Missing descriptor value '" + entry.descriptor_name + "' for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }
      if (entry.container_name != ktemporaryTestLineMotionContainerName || entry.array_index >= position.size()) {
        _SetErrorMessage(
          out_error_message,
          "Unsupported descriptor mapping '" + entry.descriptor_name + "' for container '" +
            entry.container_name + "'.");
        return false;
      }
      position[entry.array_index] = value->scalar_value;
    }

    if (!_WritetemporaryTestPosition(container_set, position)) {
      _SetErrorMessage(
        out_error_message,
        "Failed to write decomposed data into container '" +
          std::string(ktemporaryTestLineMotionContainerName) + "'.");
      return false;
    }

    return true;
  }
};

class temporaryTestLineMotionMainThreadExecutor final
  : public agent::IAlgorithmtemporaryTestMainThreadExecutor {
 public:
  bool temporaryTestExecuteOnMainThread(
    const agent::AgentTickContext& context,
    const AlgorithmProfile& algorithm_profile,
    const AgentToAlgorithmSignal& agent_to_algorithm_signal,
    AlgorithmContainerSet* algorithm_container_set,
    AlgorithmToAgentSignal* algorithm_to_agent_signal,
    agent::AlgorithmPackageDebugState* debug_state) override {
    (void)algorithm_profile;
    (void)agent_to_algorithm_signal;

    if (!context.mesh || !algorithm_container_set || !algorithm_to_agent_signal || !debug_state) {
      return false;
    }

    std::array<float, 3> position{
      ktemporaryTestMinX,
      0.0f,
      0.0f,
    };
    if (!initialized_) {
      velocity_x_ = ktemporaryTestVelocityX;
      initialized_ = true;
    } else if (!_ReadtemporaryTestPosition(*algorithm_container_set, &position)) {
      return false;
    }

    const float dt_seconds = std::max(0.0f, context.dt_seconds);
    position[0] += velocity_x_ * dt_seconds;
    if (position[0] >= ktemporaryTestMaxX) {
      position[0] = ktemporaryTestMaxX;
      velocity_x_ = -std::abs(velocity_x_);
    } else if (position[0] <= ktemporaryTestMinX) {
      position[0] = ktemporaryTestMinX;
      velocity_x_ = std::abs(velocity_x_);
    }

    if (!_WritetemporaryTestPosition(algorithm_container_set, position)) {
      return false;
    }

    _SynctemporaryTestMeshPosition(context.mesh, position);

    std::ostringstream signal_stream;
    signal_stream << "x=" << position[0] << ", vx=" << velocity_x_;
    debug_state->signals.push_back(AdvancedAlgorithmDebugSignal{
      .name = "temporaryTest_line_motion",
      .payload = signal_stream.str(),
    });

    *algorithm_to_agent_signal = {};
    return true;
  }

 private:
  bool initialized_{false};
  float velocity_x_{ktemporaryTestVelocityX};
};

}  // namespace

bool CreateAlgorithmPackageReflectorByName(
  const std::string& algorithm_name,
  std::shared_ptr<agent::IAlgorithmPackageCodec>* out_reflector,
  std::string* out_error_message) {
  if (!out_reflector) {
    _SetErrorMessage(out_error_message, "Reflector output pointer is null.");
    return false;
  }

  out_reflector->reset();
  if (_IstemporaryTestLineMotion(algorithm_name)) {
    _SetErrorMessage(out_error_message, {});
    return true;
  }
  return _RejectUnsupportedAlgorithm(algorithm_name, out_error_message);
}

bool CreateAlgorithmPackageDecomposerByName(
  const std::string& algorithm_name,
  std::shared_ptr<agent::IAlgorithmPackageDecomposer>* out_decomposer,
  std::string* out_error_message) {
  if (!out_decomposer) {
    _SetErrorMessage(out_error_message, "Decomposer output pointer is null.");
    return false;
  }

  out_decomposer->reset();
  if (_IstemporaryTestLineMotion(algorithm_name)) {
    *out_decomposer = std::make_shared<temporaryTestLineMotionDecomposer>();
    _SetErrorMessage(out_error_message, {});
    return true;
  }
  return _RejectUnsupportedAlgorithm(algorithm_name, out_error_message);
}

bool CreateAlgorithmtemporaryTestMainThreadExecutorByName(
  const std::string& algorithm_name,
  std::shared_ptr<agent::IAlgorithmtemporaryTestMainThreadExecutor>* out_executor,
  std::string* out_error_message) {
  if (!out_executor) {
    _SetErrorMessage(out_error_message, "temporaryTest executor output pointer is null.");
    return false;
  }

  out_executor->reset();
  if (_IstemporaryTestLineMotion(algorithm_name)) {
    *out_executor = std::make_shared<temporaryTestLineMotionMainThreadExecutor>();
    _SetErrorMessage(out_error_message, {});
    return true;
  }
  return _RejectUnsupportedAlgorithm(algorithm_name, out_error_message);
}

bool CreateAlgorithmInterventionPackageCodecByName(
  const std::string& algorithm_name,
  std::shared_ptr<agent::IAlgorithmInterventionPackageCodec>* out_codec,
  std::string* out_error_message) {
  if (!out_codec) {
    _SetErrorMessage(out_error_message, "Intervention codec output pointer is null.");
    return false;
  }

  out_codec->reset();
  if (_IstemporaryTestLineMotion(algorithm_name)) {
    _SetErrorMessage(out_error_message, {});
    return true;
  }
  return _RejectUnsupportedAlgorithm(algorithm_name, out_error_message);
}

bool CreateAlgorithmInterventionPackageAgentByName(
  const std::string& algorithm_name,
  std::shared_ptr<agent::IAlgorithmInterventionPackageAgent>* out_agent,
  std::string* out_error_message) {
  if (!out_agent) {
    _SetErrorMessage(out_error_message, "Intervention agent output pointer is null.");
    return false;
  }

  out_agent->reset();
  if (_IstemporaryTestLineMotion(algorithm_name)) {
    _SetErrorMessage(out_error_message, {});
    return true;
  }
  return _RejectUnsupportedAlgorithm(algorithm_name, out_error_message);
}

bool CreateAlgorithmInterventionPackageAlgorithmByName(
  const std::string& algorithm_name,
  std::shared_ptr<agent::IAlgorithmInterventionPackageAlgorithm>* out_algorithm,
  std::string* out_error_message) {
  if (!out_algorithm) {
    _SetErrorMessage(out_error_message, "Intervention algorithm output pointer is null.");
    return false;
  }

  out_algorithm->reset();
  if (_IstemporaryTestLineMotion(algorithm_name)) {
    _SetErrorMessage(out_error_message, {});
    return true;
  }
  return _RejectUnsupportedAlgorithm(algorithm_name, out_error_message);
}

bool CreateAlgorithmCodecGroupByName(
  const std::string& algorithm_name,
  agent::AgentAlgorithmCodecGroup* out_group,
  std::string* out_error_message) {
  if (!out_group) {
    _SetErrorMessage(out_error_message, "AgentAlgorithmCodecGroup output pointer is null.");
    return false;
  }
  if (algorithm_name.empty()) {
    _SetErrorMessage(out_error_message, "Algorithm name must not be empty.");
    return false;
  }

  agent::AgentAlgorithmCodecGroup group{};
  group.algorithm_profile.algorithm_name = algorithm_name;
  group.algorithm_profile.container_manifest_name = algorithm_name;

  if (!CreateAlgorithmPackageReflectorByName(
        algorithm_name,
        &group.reflector,
        out_error_message)) {
    return false;
  }
  if (!CreateAlgorithmPackageDecomposerByName(
        algorithm_name,
        &group.decomposer,
        out_error_message)) {
    return false;
  }
  if (!CreateAlgorithmtemporaryTestMainThreadExecutorByName(
        algorithm_name,
        &group.temporaryTest_main_thread_executor,
        out_error_message)) {
    return false;
  }
  if (!CreateAlgorithmInterventionPackageCodecByName(
        algorithm_name,
        &group.intervention_codec,
        out_error_message)) {
    return false;
  }
  if (!CreateAlgorithmInterventionPackageAgentByName(
        algorithm_name,
        &group.intervention_agent,
        out_error_message)) {
    return false;
  }
  if (!CreateAlgorithmInterventionPackageAlgorithmByName(
        algorithm_name,
        &group.intervention_algorithm,
        out_error_message)) {
    return false;
  }

  *out_group = std::move(group);
  _SetErrorMessage(out_error_message, {});
  return true;
}

}  // namespace codec
