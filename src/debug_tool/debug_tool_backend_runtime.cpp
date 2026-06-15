#include "debug_tool_backend_runtime.h"

#include "algorithm_management/algorithm_manager.h"
#include "cJSON.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace debug_tool_backend {

namespace {

#ifndef NDEBUG
#define DEBUG_TOOL_ASSERT(condition, message) assert((condition) && (message))
#else
#define DEBUG_TOOL_ASSERT(condition, message) ((void)0)
#endif

std::string _AlgorithmCatalogPath() {
  const std::filesystem::path candidates[] = {
    "src/capabilities/algorithm_library/algorithm_catalog.json",
    "../src/capabilities/algorithm_library/algorithm_catalog.json",
    "../../src/capabilities/algorithm_library/algorithm_catalog.json",
    "../../../src/capabilities/algorithm_library/algorithm_catalog.json",
  };

  std::error_code ec;
  for (const std::filesystem::path& candidate : candidates) {
    if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec)) {
      return candidate.string();
    }
  }
  return candidates[0].string();
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

std::string _GetJsonStringField(const cJSON* object, const char* key) {
  if (!object || !key) {
    return {};
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item || !cJSON_IsString(item) || !item->valuestring) {
    return {};
  }
  return item->valuestring;
}

std::string _AlgorithmLibraryRootPath() {
  const std::filesystem::path candidates[] = {
    "src/capabilities/algorithm_library",
    "../src/capabilities/algorithm_library",
    "../../src/capabilities/algorithm_library",
    "../../../src/capabilities/algorithm_library",
  };

  std::error_code ec;
  for (const std::filesystem::path& candidate : candidates) {
    if (std::filesystem::exists(candidate, ec) && std::filesystem::is_directory(candidate, ec)) {
      return candidate.string();
    }
  }
  return candidates[0].string();
}

std::string _ResolveAlgorithmShaderPath(
  const std::string& algorithm_name,
  const std::string& shader_path) {
  if (shader_path.empty()) {
    return {};
  }

  const std::filesystem::path path(shader_path);
  if (path.is_absolute()) {
    return path.string();
  }

  return (std::filesystem::path(_AlgorithmLibraryRootPath()) / algorithm_name / shader_path).string();
}

const agent::AlgorithmReflectionValue* _FindReflectionValue(
  const agent::AlgorithmReflectionSnapshot& snapshot,
  const std::string& container_name) {
  for (const agent::AlgorithmReflectionValue& value : snapshot.variables) {
    if (value.container_name == container_name) {
      return &value;
    }
  }
  for (const agent::AlgorithmReflectionValue& value : snapshot.variable_arrays) {
    if (value.container_name == container_name) {
      return &value;
    }
  }
  return nullptr;
}

std::string _AlgorithmContainerStorageKindToString(AlgorithmContainerStorageKind storage_kind) {
  switch (storage_kind) {
    case AlgorithmContainerStorageKind::Array: return "array";
    case AlgorithmContainerStorageKind::TemporaryRegister: return "temporary_register";
    case AlgorithmContainerStorageKind::TemporaryCache: return "temporary_cache";
  }
  return "unknown";
}

debug_tool::AlgorithmAssemblyState _ToDebugToolAlgorithmAssemblyState(agent::AlgorithmAssemblyState state) {
  switch (state) {
    case agent::AlgorithmAssemblyState::Pending: return debug_tool::AlgorithmAssemblyState::Pending;
    case agent::AlgorithmAssemblyState::Assembling: return debug_tool::AlgorithmAssemblyState::Assembling;
    case agent::AlgorithmAssemblyState::Ready: return debug_tool::AlgorithmAssemblyState::Ready;
    case agent::AlgorithmAssemblyState::Failed: return debug_tool::AlgorithmAssemblyState::Failed;
  }
  return debug_tool::AlgorithmAssemblyState::Failed;
}

debug_tool::AlgorithmReflectionValue _ToDebugToolReflectionValue(const agent::AlgorithmReflectionValue& value) {
  debug_tool::AlgorithmReflectionValue out_value{};
  out_value.reflection_object_name = value.reflection_object_name;
  out_value.container_name = value.container_name;
  out_value.filter_name = value.filter_name;
  out_value.storage_kind = _AlgorithmContainerStorageKindToString(value.storage_kind);
  out_value.bytes = value.bytes;
  return out_value;
}

debug_tool::AlgorithmReflectionSnapshot _ToDebugToolReflectionSnapshot(
  const agent::AlgorithmReflectionSnapshot& snapshot) {
  debug_tool::AlgorithmReflectionSnapshot out_snapshot{};
  out_snapshot.algorithm_name = snapshot.algorithm_name;
  out_snapshot.valid = snapshot.valid;
  out_snapshot.variables.reserve(snapshot.variables.size());
  for (const agent::AlgorithmReflectionValue& value : snapshot.variables) {
    out_snapshot.variables.push_back(_ToDebugToolReflectionValue(value));
  }
  out_snapshot.variable_arrays.reserve(snapshot.variable_arrays.size());
  for (const agent::AlgorithmReflectionValue& value : snapshot.variable_arrays) {
    out_snapshot.variable_arrays.push_back(_ToDebugToolReflectionValue(value));
  }
  return out_snapshot;
}

debug_tool::AlgorithmRuntimeSummary _ToDebugToolAlgorithmRuntimeSummary(
  const agent::Agent& managed_agent,
  size_t algorithm_index) {
  debug_tool::AlgorithmRuntimeSummary summary{};
  const agent::AlgorithmObject* object = managed_agent.algorithm_object(algorithm_index);
  const agent::AgentAlgorithmRuntimeState* runtime_state = managed_agent.algorithm_runtime_state(algorithm_index);
  if (!object) {
    return summary;
  }

  summary.algorithm_name = object->algorithm_profile.algorithm_name;
  summary.assembly_state = _ToDebugToolAlgorithmAssemblyState(managed_agent.algorithm_assembly_state(algorithm_index));
  summary.resource_bindings.reserve(object->resource_bindings.size());
  for (const agent::AlgorithmResourceBinding& binding : object->resource_bindings) {
    summary.resource_bindings.push_back(debug_tool::AlgorithmResourceBinding{
      .resource_name = binding.resource_name,
      .resource_kind = binding.resource_kind,
      .source_path = binding.source_path,
      .required = true,
    });
  }
  summary.descriptor_values.reserve(object->descriptor_values.size());
  for (const agent::AlgorithmDescriptorValue& value : object->descriptor_values) {
    summary.descriptor_values.push_back(debug_tool::AlgorithmDescriptorValue{
      .descriptor_name = value.descriptor_name,
      .scalar_value = value.scalar_value,
    });
  }
  summary.cpu_symbol = object->cpu_symbol;
  summary.gpu_symbol = object->gpu_symbol;
  summary.mount_mode = static_cast<debug_tool::AlgorithmMountMode>(static_cast<int>(object->mount_mode));
  summary.execution_preference =
    static_cast<debug_tool::AlgorithmExecutionPreference>(static_cast<int>(object->execution_preference));
  if (runtime_state) {
    summary.agent_to_algorithm_signal = runtime_state->agent_to_algorithm_signal;
    summary.algorithm_to_agent_signal = runtime_state->algorithm_to_agent_signal;
  }
  if (runtime_state && runtime_state->reflection_snapshot.valid) {
    summary.reflection_snapshot = _ToDebugToolReflectionSnapshot(runtime_state->reflection_snapshot);
  }
  return summary;
}

std::vector<agent::AlgorithmResourceBinding> _ToAgentResourceBindings(
  const std::vector<debug_tool::AlgorithmResourceBinding>& bindings) {
  std::vector<agent::AlgorithmResourceBinding> result;
  result.reserve(bindings.size());
  for (const debug_tool::AlgorithmResourceBinding& binding : bindings) {
    result.push_back(agent::AlgorithmResourceBinding{
      .resource_name = binding.resource_name,
      .resource_kind = binding.resource_kind,
      .source_path = binding.source_path,
    });
  }
  return result;
}

std::vector<agent::AlgorithmDescriptorValue> _ToAgentDescriptorValues(
  const std::vector<debug_tool::AlgorithmDescriptorValue>& values) {
  std::vector<agent::AlgorithmDescriptorValue> result;
  result.reserve(values.size());
  for (const debug_tool::AlgorithmDescriptorValue& value : values) {
    result.push_back(agent::AlgorithmDescriptorValue{
      .descriptor_name = value.descriptor_name,
      .scalar_value = value.scalar_value,
    });
  }
  return result;
}

}  // namespace

DebugToolBackendRuntime::~DebugToolBackendRuntime() {
  Destroy();
}

bool DebugToolBackendRuntime::Init(const char* window_title, int width, int height) {
  agent_manager_.Destroy();
  ui_status_message_.clear();
  if (!runtime_environment_.Init(window_title ? window_title : "debugTool", width, height)) {
    return false;
  }

  AgentCreateSpec default_agent_spec{};
  default_agent_spec.agent_name = "debug_agent";
  default_agent_spec.limit_fps_flag = 120u;
  if (!CreateAgent(default_agent_spec.agent_name.c_str(), default_agent_spec.limit_fps_flag)) {
    runtime_environment_.Destroy();
    return false;
  }

  PauseTicking();
  frame_dt_ = 0.0f;
  last_frame_time_ = std::chrono::steady_clock::now();
  return true;
}

bool DebugToolBackendRuntime::Tick() {
  if (!runtime_environment_.has_window()) {
    return false;
  }

  const auto now = std::chrono::steady_clock::now();
  frame_dt_ = std::chrono::duration<float>(now - last_frame_time_).count();
  last_frame_time_ = now;

  agent_manager_.Tick(runtime_environment_.input(), runtime_environment_.MousePosition(), frame_dt_);
  return runtime_environment_.Tick();
}

void DebugToolBackendRuntime::Destroy() {
  agent_manager_.Destroy();
  runtime_environment_.Destroy();
  ui_status_message_.clear();
  last_frame_time_ = {};
  frame_dt_ = 0.0f;
}

bool DebugToolBackendRuntime::CreateAgent(const char* agent_name, uint32_t limit_fps_flag, size_t* out_agent_index) {
  AgentCreateSpec spec{};
  spec.agent_name = agent_name ? agent_name : "";
  spec.limit_fps_flag = limit_fps_flag;
  return agent_manager_.CreateAgent(std::move(spec), out_agent_index);
}

bool DebugToolBackendRuntime::has_agents() const {
  return agent_manager_.has_agents();
}

size_t DebugToolBackendRuntime::agent_count() const {
  return agent_manager_.agent_count();
}

bool DebugToolBackendRuntime::AttachAlgorithmToAgent(
  size_t agent_index,
  const std::string& algorithm_name,
  const std::vector<debug_tool::AlgorithmResourceBinding>& resource_bindings,
  const std::vector<debug_tool::AlgorithmDescriptorValue>& descriptor_values,
  size_t* out_algorithm_index,
  std::string* out_error_message,
  debug_tool::AlgorithmMountMode mount_mode,
  debug_tool::AlgorithmExecutionPreference execution_preference) {
  const bool attached = agent_manager_.AttachAlgorithmToAgent(
    agent_index,
    algorithm_name,
    _ToAgentResourceBindings(resource_bindings),
    _ToAgentDescriptorValues(descriptor_values),
    out_algorithm_index,
    out_error_message,
    static_cast<agent::AlgorithmMountMode>(static_cast<int>(mount_mode)),
    static_cast<agent::AlgorithmExecutionPreference>(static_cast<int>(execution_preference)));
  if (!attached) {
    DEBUG_TOOL_ASSERT(false, "Failed to attach algorithm to the built-in agent.");
  }
  return attached;
}

bool DebugToolBackendRuntime::GetAgentSummary(
  size_t agent_index,
  debug_tool::AgentRuntimeSummary* out_summary) const {
  if (!out_summary) {
    return false;
  }

  *out_summary = {};

  const std::shared_ptr<agent::Agent> managed_agent = agent_manager_.agent(agent_index);
  if (!managed_agent) {
    return false;
  }

  out_summary->agent_name = managed_agent->agent_name();
  out_summary->algorithms.reserve(managed_agent->algorithm_count());
  for (size_t algorithm_index = 0; algorithm_index < managed_agent->algorithm_count(); ++algorithm_index) {
    out_summary->algorithms.push_back(_ToDebugToolAlgorithmRuntimeSummary(*managed_agent, algorithm_index));
  }
  return true;
}

const AlgorithmToAgentSignal& DebugToolBackendRuntime::combined_algorithm_to_agent_signal() const {
  return agent_manager_.combined_algorithm_to_agent_signal();
}

bool DebugToolBackendRuntime::LoadAlgorithmCatalog(
  std::vector<debug_tool::AlgorithmCatalogEntry>* out_entries,
  std::string* out_error_message) const {
  if (!out_entries) {
    if (out_error_message) {
      *out_error_message = "Algorithm catalog output pointer is null.";
    }
    return false;
  }

  out_entries->clear();

  const std::string catalog_path = _AlgorithmCatalogPath();
  const std::string json_text = _ReadTextFile(catalog_path);
  if (json_text.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to read algorithm catalog: " + catalog_path;
    }
    return false;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    if (out_error_message) {
      *out_error_message = "Failed to parse algorithm catalog: " + catalog_path;
    }
    return false;
  }

  const cJSON* algorithms = cJSON_GetObjectItemCaseSensitive(root, "algorithms");
  if (!algorithms || !cJSON_IsArray(algorithms)) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Algorithm catalog is missing the algorithms array.";
    }
    return false;
  }

  const int count = cJSON_GetArraySize(algorithms);
  out_entries->reserve(count > 0 ? static_cast<size_t>(count) : 0u);
  for (int i = 0; i < count; ++i) {
    const cJSON* item = cJSON_GetArrayItem(algorithms, i);
    if (!item || !cJSON_IsObject(item)) {
      continue;
    }

    debug_tool::AlgorithmCatalogEntry entry{};
    entry.algorithm_name = _GetJsonStringField(item, "name");
    entry.display_name = _GetJsonStringField(item, "display_name");
    entry.folder_name = _GetJsonStringField(item, "folder");
    entry.container_manifest_name = _GetJsonStringField(item, "container_manifest");
    entry.decomposer_name = _GetJsonStringField(item, "decomposer");
    entry.reflector_name = _GetJsonStringField(item, "reflector");
    entry.intervention_name = _GetJsonStringField(item, "intervention");

    if (entry.display_name.empty()) {
      entry.display_name = entry.algorithm_name;
    }
    if (!entry.algorithm_name.empty()) {
      out_entries->push_back(std::move(entry));
    }
  }

  cJSON_Delete(root);
  if (out_entries->empty()) {
    if (out_error_message) {
      *out_error_message = "Algorithm catalog does not contain any entries.";
    }
    return false;
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool DebugToolBackendRuntime::QueryAlgorithmRequestedBindings(
  const std::string& algorithm_name,
  std::vector<debug_tool::RequestedResourceEntry>* out_resources,
  std::vector<debug_tool::RequestedDescriptorEntry>* out_descriptors,
  std::string* out_error_message) const {
  if (!out_resources || !out_descriptors) {
    if (out_error_message) {
      *out_error_message = "Requested binding output pointers are null.";
    }
    return false;
  }

  out_resources->clear();
  out_descriptors->clear();

  algorithm_management::AlgorithmRequestedResources requested_resources{};
  algorithm_management::AlgorithmRequestedDescriptorBindings requested_descriptor_bindings{};
  std::string reflection_error_message;
  if (!algorithm_management::QueryAlgorithmRequestedBindings(
        algorithm_name,
        &requested_resources,
        &requested_descriptor_bindings,
        &reflection_error_message)) {
    if (out_error_message) {
      *out_error_message = reflection_error_message.empty()
        ? ("Failed to query requested bindings for '" + algorithm_name + "'.")
        : std::move(reflection_error_message);
    }
    return false;
  }

  for (const algorithm_management::AlgorithmRequestedResources::RequiredResource& resource :
       requested_resources.required_resources) {
    if (resource.resource_name.empty()) {
      DEBUG_TOOL_ASSERT(false, "Requested resource entry is missing a resource name.");
      if (out_error_message) {
        *out_error_message =
          "Backend returned an unreadable requested resource entry without a resource name for '" +
          algorithm_name + "'.";
      }
      return false;
    }
    if (resource.resource_kind.empty()) {
      DEBUG_TOOL_ASSERT(false, "Requested resource entry is missing a resource kind.");
      if (out_error_message) {
        *out_error_message =
          "Backend returned an unreadable requested resource entry without a resource kind for '" +
          algorithm_name + "'.";
      }
      return false;
    }
    out_resources->push_back(debug_tool::RequestedResourceEntry{
      .resource_name = resource.resource_name,
      .resource_kind = resource.resource_kind,
      .required = resource.required,
    });
  }
  for (const algorithm_management::AlgorithmRequestedDescriptorBindings::DescriptorSlot& descriptor :
       requested_descriptor_bindings.descriptor_slots) {
    if (descriptor.descriptor_name.empty()) {
      DEBUG_TOOL_ASSERT(false, "Requested descriptor entry is missing a descriptor name.");
      if (out_error_message) {
        *out_error_message =
          "Backend returned an unreadable requested descriptor entry without a descriptor name for '" +
          algorithm_name + "'.";
      }
      return false;
    }
    if (descriptor.container_name.empty()) {
      DEBUG_TOOL_ASSERT(false, "Requested descriptor entry is missing a container name.");
      if (out_error_message) {
        *out_error_message =
          "Backend returned an unreadable requested descriptor entry without a container name for '" +
          algorithm_name + "'.";
      }
      return false;
    }
    out_descriptors->push_back(debug_tool::RequestedDescriptorEntry{
      .descriptor_name = descriptor.descriptor_name,
      .container_name = descriptor.container_name,
      .array_index = descriptor.array_index,
    });
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool DebugToolBackendRuntime::LoadAlgorithmPackageDefaultBindings(
  const std::string& algorithm_name,
  std::vector<debug_tool::AlgorithmResourceBinding>* out_resource_bindings,
  std::vector<debug_tool::AlgorithmDescriptorValue>* out_descriptor_values,
  bool* out_has_default_file,
  std::string* out_error_message) const {
  if (!out_resource_bindings || !out_descriptor_values) {
    if (out_error_message) {
      *out_error_message = "Default binding output pointers are null.";
    }
    return false;
  }

  out_resource_bindings->clear();
  out_descriptor_values->clear();
  if (out_has_default_file) {
    *out_has_default_file = false;
  }

  std::vector<algorithm_management::AlgorithmResourceBinding> package_resource_bindings;
  std::vector<algorithm_management::AlgorithmDescriptorValue> package_descriptor_values;
  bool has_default_file = false;
  std::string default_error_message;
  if (!algorithm_management::LoadAlgorithmPackageDefaultBindings(
        algorithm_name,
        &package_resource_bindings,
        &package_descriptor_values,
        &has_default_file,
        &default_error_message)) {
    if (out_error_message) {
      *out_error_message = default_error_message.empty()
        ? ("Failed to load default bindings for '" + algorithm_name + "'.")
        : std::move(default_error_message);
    }
    return false;
  }

  if (!has_default_file) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  out_resource_bindings->reserve(package_resource_bindings.size());
  for (const algorithm_management::AlgorithmResourceBinding& binding : package_resource_bindings) {
    out_resource_bindings->push_back(debug_tool::AlgorithmResourceBinding{
      .resource_name = binding.resource_name,
      .resource_kind = binding.resource_kind,
      .source_path = binding.source_path,
      .required = true,
    });
  }
  out_descriptor_values->reserve(package_descriptor_values.size());
  for (const algorithm_management::AlgorithmDescriptorValue& value : package_descriptor_values) {
    out_descriptor_values->push_back(debug_tool::AlgorithmDescriptorValue{
      .descriptor_name = value.descriptor_name,
      .scalar_value = value.scalar_value,
    });
  }
  if (out_has_default_file) {
    *out_has_default_file = true;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool DebugToolBackendRuntime::BuildRenderPreviewRequest(
  size_t agent_index,
  size_t algorithm_index,
  runtime_systems::RenderPreviewRequest* out_request,
  std::string* out_error_message) const {
  if (!out_request) {
    DEBUG_TOOL_ASSERT(false, "Preview request output pointer is null.");
    if (out_error_message) {
      *out_error_message = "Preview request output pointer is null.";
    }
    return false;
  }

  out_request->Clear();

  const std::shared_ptr<agent::Agent> managed_agent = agent_manager_.agent(agent_index);
  if (!managed_agent) {
    DEBUG_TOOL_ASSERT(false, "Managed agent is unavailable.");
    if (out_error_message) {
      *out_error_message = "Managed agent is unavailable.";
    }
    return false;
  }

  const agent::AlgorithmObject* object = managed_agent->algorithm_object(algorithm_index);
  if (!object || !object->intervention) {
    DEBUG_TOOL_ASSERT(false, "Algorithm has no intervention for preview rendering.");
    return false;
  }
  const std::string algorithm_name = object->algorithm_profile.algorithm_name;

  std::vector<agent::AlgorithmInterventionStageSpec> stage_specs;
  if (!object->intervention->GetInterventionStageSpecs(&stage_specs) || stage_specs.empty()) {
    DEBUG_TOOL_ASSERT(false, "Algorithm intervention did not expose any stages.");
    return false;
  }

  const agent::AlgorithmInterventionStageSpec* result_stage = nullptr;
  for (const agent::AlgorithmInterventionStageSpec& stage_spec : stage_specs) {
    if (stage_spec.stage_kind == agent::AlgorithmInterventionStageKind::ResultRender) {
      result_stage = &stage_spec;
      break;
    }
  }
  if (!result_stage) {
    DEBUG_TOOL_ASSERT(false, "Algorithm intervention did not expose a result-render stage.");
    return false;
  }
  if (result_stage->shader.vertex_shader_path.empty() || result_stage->shader.fragment_shader_path.empty()) {
    DEBUG_TOOL_ASSERT(false, "Result-render stage is missing shader paths.");
    if (out_error_message) {
      *out_error_message = "Result-render stage is missing shader paths.";
    }
    return false;
  }

  const AlgorithmContainerSet* container_set = object->container_set();
  if (!container_set) {
    DEBUG_TOOL_ASSERT(false, "Algorithm container set is unavailable for preview rendering.");
    if (out_error_message) {
      *out_error_message = "Algorithm container set is unavailable.";
    }
    return false;
  }

  const agent::AlgorithmReflectionSnapshot* reflection_snapshot =
    managed_agent->algorithm_runtime_state(algorithm_index) &&
      managed_agent->algorithm_runtime_state(algorithm_index)->reflection_snapshot.valid
    ? &managed_agent->algorithm_runtime_state(algorithm_index)->reflection_snapshot
    : nullptr;

  out_request->stage_name = result_stage->stage_name;
  out_request->vertex_shader_path = _ResolveAlgorithmShaderPath(algorithm_name, result_stage->shader.vertex_shader_path);
  out_request->fragment_shader_path = _ResolveAlgorithmShaderPath(algorithm_name, result_stage->shader.fragment_shader_path);
  out_request->storage_buffers.reserve(result_stage->used_algorithm_containers.size());

  uint32_t instance_count = 0u;
  bool have_instance_count = false;
  for (const agent::AlgorithmInterventionContainerBinding& binding : result_stage->used_algorithm_containers) {
    const AlgorithmContainer* container = FindAlgorithmContainer(*container_set, binding.container_name);
    if (!container) {
      DEBUG_TOOL_ASSERT(false, "Required preview container is missing.");
      if (binding.required) {
        if (out_error_message) {
          *out_error_message = "Missing preview container: " + binding.container_name;
        }
        out_request->Clear();
        return false;
      }
      continue;
    }
    if (container->storage_kind != AlgorithmContainerStorageKind::Array) {
      DEBUG_TOOL_ASSERT(false, "Preview container is not an array.");
      if (out_error_message) {
        *out_error_message = "Preview container is not an array: " + binding.container_name;
      }
      out_request->Clear();
      return false;
    }
    const agent::AlgorithmReflectionValue* reflected_value = nullptr;
    if (reflection_snapshot) {
      reflected_value = _FindReflectionValue(*reflection_snapshot, binding.container_name);
    }

    const bool has_reflected_bytes = reflected_value && !reflected_value->bytes.empty();
    const bool has_container_bytes = !container->bytes.empty();
    if (container->element_stride == 0u || (!has_reflected_bytes && !has_container_bytes)) {
      DEBUG_TOOL_ASSERT(false, "Preview container has no drawable data.");
      if (binding.required) {
        if (out_error_message) {
          *out_error_message = "Preview container has no data: " + binding.container_name;
        }
        out_request->Clear();
        return false;
      }
      continue;
    }

    runtime_systems::RenderPreviewBuffer preview_buffer{};
    preview_buffer.binding_name = binding.container_name;
    preview_buffer.element_stride = container->element_stride;
    if (has_reflected_bytes && reflected_value->storage_kind == container->storage_kind) {
      preview_buffer.bytes = reflected_value->bytes;
    } else {
      preview_buffer.bytes.assign(container->bytes.begin(), container->bytes.end());
    }
    out_request->storage_buffers.push_back(std::move(preview_buffer));

    const uint32_t buffer_instances = static_cast<uint32_t>(container->bytes.size() / container->element_stride);
    if (!have_instance_count) {
      instance_count = buffer_instances;
      have_instance_count = true;
    } else {
      instance_count = std::min(instance_count, buffer_instances);
    }
  }

  if (out_request->storage_buffers.empty()) {
    DEBUG_TOOL_ASSERT(false, "Result-render stage did not expose any usable array container.");
    if (out_error_message) {
      *out_error_message = "Result-render stage does not expose any usable array container.";
    }
    out_request->Clear();
    return false;
  }

  out_request->instance_count = instance_count;
  out_request->valid = out_request->instance_count > 0u;
  if (!out_request->valid) {
    DEBUG_TOOL_ASSERT(false, "Preview request has no drawable instances.");
    if (out_error_message) {
      *out_error_message = "Preview request has no drawable instances.";
    }
  }
  if (out_request->valid) {
    DEBUG_TOOL_ASSERT(
      !out_request->storage_buffers.empty(),
      "Preview request must contain at least one storage buffer.");
    DEBUG_TOOL_ASSERT(
      !out_request->stage_name.empty(),
      "Preview request must contain a stage name.");
  }
  return out_request->valid;
}

}  // namespace debug_tool_backend
